// SDL2 Miyoo backend: software render target + MI_GFX / framebuffer present,
// Miyoo keysyms and gamepad input.

#include "plat_sdl2_internal.h"

#if defined(HGE_GFX_BLIT)
#include "gfx/gfx_blit.h"   // BlitFlush: the blitter needs an explicit frame boundary
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <mi_sys.h>
#include <mi_gfx.h>

#ifndef ALIGN4K
#define ALIGN4K(v) (((v) + 4095) & (~4095))
#endif

namespace plat {
namespace detail {
namespace {

SDL_Renderer*      g_ren = nullptr;
SDL_GameController* g_pad = nullptr;
SDL_Joystick*      g_joy = nullptr;
SDL_Surface*       g_off = nullptr;   // offscreen test surface only (HGE_OFFSCREEN)
SDL_Surface*       g_sw  = nullptr;   // software render target (MMA or malloc)
unsigned char      g_heldKeys[256];

int g_lw = 0;   // logical (render coordinate space) size
int g_lh = 0;

// Output path. The mmiyoo SDL2 video renderers are unusable for a full-screen
// 640x480 app (stock = no-op stubs, PICO-8 = narrow bezel view), so we render
// with SDL's software renderer and present ourselves. Preferred path mirrors
// RetroArch's miyoomini driver: the frame lives in an MI_SYS MMA buffer that
// MI_GFX_BitBlit copies/scales to the panel in hardware (no CPU memcpy).
int                g_fbFd = -1;
bool               g_useGfx = false;
unsigned char*     g_fbMem = nullptr;
size_t             g_fbFrameBytes = 0;
size_t             g_fbLineBytes = 0;
int                g_fbW = 0, g_fbH = 0;
int                g_fbVisibleY = 0;
struct fb_var_screeninfo g_fbVar;
MI_GFX_Surface_t   g_gfxSrc, g_gfxDst;
MI_GFX_Rect_t      g_gfxSrcRect, g_gfxDstRect;
MI_GFX_Opt_t       g_gfxOpt;
MI_PHY             g_gfxSrcPhy = 0;
void*              g_gfxSrcVir = nullptr;
size_t             g_gfxSrcSize = 0;
struct fb_fix_screeninfo g_gfxFinfo;
struct fb_var_screeninfo g_gfxVinfo;
uint32_t           g_gfxResX = 0, g_gfxResY = 0;

int padButtonToHge(Uint8 b)
{
    switch (b) {
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  return HGEK_LEFT;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return HGEK_RIGHT;
    case SDL_CONTROLLER_BUTTON_DPAD_UP:    return HGEK_UP;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  return HGEK_DOWN;
    case SDL_CONTROLLER_BUTTON_A:          return HGEK_SPACE;   // fire
    case SDL_CONTROLLER_BUTTON_B:          return HGEK_R;       // restart
    case SDL_CONTROLLER_BUTTON_Y:          return HGEK_R;       // restart alt
    default:                               return 0;
    }
}

void openFirstController()
{
    if (g_pad) return;
    const int n = SDL_NumJoysticks();
    fprintf(stderr, "[plat] joysticks=%d\n", n);
    for (int i = 0; i < n; ++i) {
        fprintf(stderr, "[plat]  joy[%d] name='%s' isGC=%d\n", i,
                SDL_JoystickNameForIndex(i), SDL_IsGameController(i));
        if (SDL_IsGameController(i)) {
            g_pad = SDL_GameControllerOpen(i);
            if (g_pad) {
                fprintf(stderr, "[plat] controller opened: %s\n",
                        SDL_GameControllerName(g_pad));
                return;
            }
        }
    }
    if (n > 0 && !g_joy) {
        g_joy = SDL_JoystickOpen(0);
        if (g_joy) {
            SDL_JoystickEventState(SDL_ENABLE);
            fprintf(stderr, "[plat] raw joystick opened: %s buttons=%d axes=%d hats=%d\n",
                    SDL_JoystickName(g_joy), SDL_JoystickNumButtons(g_joy),
                    SDL_JoystickNumAxes(g_joy), SDL_JoystickNumHats(g_joy));
        }
    }
}

bool initGfx()
{
    if (MI_SYS_Init() != MI_SUCCESS)
        fprintf(stderr, "[plat] MI_SYS_Init already initialised (continuing)\n");
    if (MI_GFX_Open() != MI_SUCCESS) {
        fprintf(stderr, "[plat] MI_GFX_Open failed\n");
        return false;
    }

    g_fbFd = open("/dev/fb0", O_RDWR);
    if (g_fbFd < 0) { perror("[plat] open /dev/fb0"); return false; }
    if (ioctl(g_fbFd, FBIOGET_VSCREENINFO, &g_gfxVinfo) != 0 ||
        ioctl(g_fbFd, FBIOGET_FSCREENINFO, &g_gfxFinfo) != 0) {
        perror("[plat] fb ioctl"); close(g_fbFd); g_fbFd = -1; return false;
    }

    g_gfxResX = g_gfxVinfo.xres;
    g_gfxResY = g_gfxVinfo.yres;
    g_gfxVinfo.yoffset = 0;
    ioctl(g_fbFd, FBIOPAN_DISPLAY, &g_gfxVinfo);
    g_fbVisibleY = 0;

    g_gfxSrcSize = (size_t)g_gfxResX * g_gfxResY * 4;
    const uint32_t allocSize = ALIGN4K((uint32_t)g_gfxSrcSize);
    if (MI_SYS_MMA_Alloc(nullptr, allocSize, &g_gfxSrcPhy) != MI_SUCCESS) {
        fprintf(stderr, "[plat] MI_SYS_MMA_Alloc failed\n");
        close(g_fbFd); g_fbFd = -1; return false;
    }
    if (MI_SYS_Mmap(g_gfxSrcPhy, allocSize, &g_gfxSrcVir, TRUE) != MI_SUCCESS) {
        fprintf(stderr, "[plat] MI_SYS_Mmap failed\n");
        MI_SYS_MMA_Free(g_gfxSrcPhy); g_gfxSrcPhy = 0;
        close(g_fbFd); g_fbFd = -1; return false;
    }
    memset(g_gfxSrcVir, 0, g_gfxSrcSize);

    g_sw = SDL_CreateRGBSurfaceFrom(g_gfxSrcVir, (int)g_gfxResX, (int)g_gfxResY, 32,
                                    (int)g_gfxResX * 4,
                                    0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0xFF000000u);
    if (!g_sw) {
        fprintf(stderr, "[plat] SDL_CreateRGBSurfaceFrom failed\n");
        MI_SYS_Munmap(g_gfxSrcVir, allocSize); g_gfxSrcVir = nullptr;
        MI_SYS_MMA_Free(g_gfxSrcPhy); g_gfxSrcPhy = 0;
        close(g_fbFd); g_fbFd = -1; return false;
    }

    memset(&g_gfxSrc, 0, sizeof(g_gfxSrc));
    g_gfxSrc.eColorFmt  = E_MI_GFX_FMT_ARGB8888;
    g_gfxSrc.u32Width   = g_gfxResX;
    g_gfxSrc.u32Height  = g_gfxResY;
    g_gfxSrc.u32Stride  = g_gfxResX * 4;
    g_gfxSrc.phyAddr    = g_gfxSrcPhy;
    g_gfxSrcRect.s32Xpos = 0; g_gfxSrcRect.s32Ypos = 0;
    g_gfxSrcRect.u32Width = g_gfxResX; g_gfxSrcRect.u32Height = g_gfxResY;

    memset(&g_gfxDst, 0, sizeof(g_gfxDst));
    g_gfxDst.eColorFmt  = E_MI_GFX_FMT_ARGB8888;
    g_gfxDst.u32Width   = g_gfxResX;
    g_gfxDst.u32Height  = g_gfxResY;
    g_gfxDst.u32Stride  = g_gfxResX * 4;
    g_gfxDst.phyAddr    = g_gfxFinfo.smem_start;
    g_gfxDstRect.s32Xpos = 0; g_gfxDstRect.s32Ypos = 0;
    g_gfxDstRect.u32Width = g_gfxResX; g_gfxDstRect.u32Height = g_gfxResY;

    memset(&g_gfxOpt, 0, sizeof(g_gfxOpt));
    g_gfxOpt.eSrcDfbBldOp = E_MI_GFX_DFB_BLD_ONE;
    g_gfxOpt.eDstDfbBldOp = E_MI_GFX_DFB_BLD_ZERO;
    g_gfxOpt.eMirror      = E_MI_GFX_MIRROR_BOTH;
    g_gfxOpt.eRotate      = E_MI_GFX_ROTATE_0;

    g_useGfx = true;
    fprintf(stderr, "[plat] MI_GFX %ux%u srcPhy=0x%llx dstPhy=0x%llx\n",
            g_gfxResX, g_gfxResY,
            (unsigned long long)g_gfxSrcPhy, (unsigned long long)g_gfxFinfo.smem_start);
    return true;
}

void presentGfx()
{
    if (!g_useGfx) return;
    MI_SYS_FlushInvCache(g_gfxSrcVir, ALIGN4K((uint32_t)g_gfxSrcSize));
    const int target = (g_fbVisibleY + (int)g_gfxResY) % ((int)g_gfxResY * 2);
    g_gfxDst.phyAddr = g_gfxFinfo.smem_start + (MI_PHY)g_gfxResX * target * 4;
    MI_U16 fence = 0;
    MI_GFX_BitBlit(&g_gfxSrc, &g_gfxSrcRect, &g_gfxDst, &g_gfxDstRect, &g_gfxOpt, &fence);
    MI_GFX_WaitAllDone(TRUE, fence);
    g_gfxVinfo.yoffset = (MI_U32)target;
    ioctl(g_fbFd, FBIOPAN_DISPLAY, &g_gfxVinfo);
    g_fbVisibleY = target;
}

bool openFb()
{
    g_fbFd = open("/dev/fb0", O_RDWR);
    if (g_fbFd < 0) { perror("[plat] open /dev/fb0"); return false; }

    struct fb_var_screeninfo v;
    struct fb_fix_screeninfo f;
    if (ioctl(g_fbFd, FBIOGET_VSCREENINFO, &v) != 0 ||
        ioctl(g_fbFd, FBIOGET_FSCREENINFO, &f) != 0) {
        perror("[plat] fb ioctl"); close(g_fbFd); g_fbFd = -1; return false;
    }

    g_fbW = (int)v.xres;
    g_fbH = (int)v.yres;
    g_fbLineBytes = (size_t)f.line_length;
    g_fbFrameBytes = (size_t)g_fbH * g_fbLineBytes;
    g_fbVar = v;

    size_t mapBytes = g_fbFrameBytes * 2;
    if (f.smem_len && f.smem_len < mapBytes) mapBytes = f.smem_len;
    void* m = mmap(nullptr, mapBytes, PROT_READ | PROT_WRITE, MAP_SHARED, g_fbFd, 0);
    if (m == MAP_FAILED) {
        perror("[plat] mmap /dev/fb0"); close(g_fbFd); g_fbFd = -1; return false;
    }
    g_fbMem = (unsigned char*)m;
    memset(g_fbMem, 0, mapBytes);
    g_fbVisibleY = 0;
    g_fbVar.xoffset = 0;
    g_fbVar.yoffset = 0;
    ioctl(g_fbFd, FBIOPAN_DISPLAY, &g_fbVar);

    fprintf(stderr, "[plat] fb(mmap)=%dx%d line=%zu frame=%zu bpp=%u\n",
            g_fbW, g_fbH, g_fbLineBytes, g_fbFrameBytes, v.bits_per_pixel);
    return true;
}

void closeDisplay()
{
    if (g_fbMem) { munmap(g_fbMem, g_fbFrameBytes * 2); g_fbMem = nullptr; }
    if (g_useGfx) {
        const uint32_t allocSize = ALIGN4K((uint32_t)g_gfxSrcSize);
        if (g_gfxSrcVir) { MI_SYS_Munmap(g_gfxSrcVir, allocSize); g_gfxSrcVir = nullptr; }
        if (g_gfxSrcPhy) { MI_SYS_MMA_Free(g_gfxSrcPhy); g_gfxSrcPhy = 0; }
        MI_GFX_Close();
        g_useGfx = false;
    }
    if (g_fbFd >= 0) { close(g_fbFd); g_fbFd = -1; }
}

} // namespace

bool backendInit(int width, int height, const char* title, bool /*vsync*/, bool /*resizable*/)
{
    // Headless verification mode: software renderer into an in-memory surface.
    {
        const char* off = getenv("HGE_OFFSCREEN");
        if (off && off[0] == '1') {
            SDL_Init(SDL_INIT_TIMER);
            g_off = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32,
                                                   SDL_PIXELFORMAT_ARGB8888);
            if (!g_off) {
                fprintf(stderr, "[plat] offscreen surface failed: %s\n", SDL_GetError());
                return false;
            }
            g_ren = SDL_CreateSoftwareRenderer(g_off);
            if (!g_ren) {
                fprintf(stderr, "[plat] software renderer failed: %s\n", SDL_GetError());
                return false;
            }
            g_lw = width; g_lh = height;
            g_w = width; g_h = height;
            fprintf(stderr, "[plat] OFFSCREEN %dx%d renderer=software\n", width, height);
            return true;
        }
    }

    SDL_setenv("SDL_VIDEODRIVER", "mmiyoo", 0);
    SDL_setenv("SDL_AUDIODRIVER", "mmiyoo", 0);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "[plat] SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    int winW = width, winH = height;
    SDL_DisplayMode dm;
    if (SDL_GetDesktopDisplayMode(0, &dm) != 0 || dm.w <= 0 || dm.h <= 0) {
        if (SDL_GetCurrentDisplayMode(0, &dm) == 0 && dm.w > 0 && dm.h > 0) {
            winW = dm.w; winH = dm.h;
        }
    } else {
        winW = dm.w; winH = dm.h;
    }
    const char* envW = getenv("HGE_WIN_W");
    const char* envH = getenv("HGE_WIN_H");
    if (envW && envH) {
        int w = atoi(envW), h = atoi(envH);
        if (w > 0 && h > 0) { winW = w; winH = h; }
    } else {
        FILE* rf = fopen("/tmp/screen_resolution", "r");
        if (rf) {
            int w = 0, h = 0;
            if (fscanf(rf, "%dx%d", &w, &h) == 2 && w > 0 && h > 0) { winW = w; winH = h; }
            fclose(rf);
        }
    }

    g_win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             winW, winH, SDL_WINDOW_SHOWN);
    if (!g_win) {
        fprintf(stderr, "[plat] CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    if (!initGfx()) {
        fprintf(stderr, "[plat] falling back to CPU framebuffer blit\n");
        g_sw = SDL_CreateRGBSurfaceWithFormat(0, winW, winH, 32, SDL_PIXELFORMAT_ARGB8888);
        if (g_sw) openFb();
    }
    if (!g_sw) {
        fprintf(stderr, "[plat] software surface failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(g_win); g_win = nullptr;
        SDL_Quit();
        return false;
    }
    g_ren = SDL_CreateSoftwareRenderer(g_sw);
    if (!g_ren) {
        fprintf(stderr, "[plat] software renderer failed: %s\n", SDL_GetError());
        SDL_FreeSurface(g_sw); g_sw = nullptr;
        SDL_DestroyWindow(g_win); g_win = nullptr;
        SDL_Quit();
        return false;
    }
    SDL_RenderSetClipRect(g_ren, nullptr);

    g_lw = width;
    g_lh = height;

    {
        SDL_RendererInfo ri;
        if (SDL_GetRendererInfo(g_ren, &ri) == 0)
            fprintf(stderr, "[plat] renderer=%s flags=0x%x\n", ri.name, ri.flags);
        fprintf(stderr, "[plat] video=%s window=%dx%d logical=%dx%d sw=%dx%d present=%s\n",
                SDL_GetCurrentVideoDriver(), winW, winH, width, height,
                g_sw->w, g_sw->h, g_useGfx ? "MI_GFX" : "CPU");
    }

    openFirstController();
    return true;
}

void backendShutdown()
{
    if (g_pad) { SDL_GameControllerClose(g_pad); g_pad = nullptr; }
    if (g_joy) { SDL_JoystickClose(g_joy); g_joy = nullptr; }
    if (g_ren) { SDL_DestroyRenderer(g_ren); g_ren = nullptr; }
    if (g_off) { SDL_FreeSurface(g_off); g_off = nullptr; }
    if (g_sw) { SDL_FreeSurface(g_sw); g_sw = nullptr; }
    closeDisplay();
}

void backendUpdateSize()
{
    if (!g_win) return;
    SDL_GetWindowSize(g_win, &g_ww, &g_wh);
    g_w = g_lw;
    g_h = g_lh;
}

void* backendRenderer()
{
    return g_ren;
}

void* backendGLGetProc(const char* /*name*/)
{
    return nullptr;
}

void backendSwapBuffers()
{
#if defined(HGE_GFX_BLIT)
    // gfx_blit queues quads and only rasterizes on an explicit frame
    // boundary; without this every blit frame would present an empty buffer.
    BlitFlush();
#endif
    if (g_ren) SDL_RenderPresent(g_ren);

    if (g_useGfx) { presentGfx(); return; }

    if (g_fbMem && g_sw && g_sw->pixels) {
        const int h = (g_sw->h < g_fbH) ? g_sw->h : g_fbH;
        const int w = (g_sw->w < g_fbW) ? g_sw->w : g_fbW;
        const int back = (g_fbVisibleY == 0) ? g_fbH : 0;
        unsigned char* dst = g_fbMem + (size_t)back * g_fbLineBytes;
        const uint32_t* s32 = (const uint32_t*)g_sw->pixels;
        uint32_t* d32 = (uint32_t*)dst;
        const int sp = g_sw->pitch / 4;
        const int dp = (int)(g_fbLineBytes / 4);
        for (int y = 0; y < h; ++y) {
            const uint32_t* sr = s32 + (size_t)(h - 1 - y) * sp;
            uint32_t* dr = d32 + (size_t)y * dp;
            for (int x = 0; x < w; ++x) dr[x] = sr[w - 1 - x];
        }
        g_fbVar.xoffset = 0;
        g_fbVar.yoffset = (Uint32)back;
        if (ioctl(g_fbFd, FBIOPAN_DISPLAY, &g_fbVar) == 0)
            g_fbVisibleY = back;
    }
}

void backendMessageBox(const char* title, const char* text)
{
    fprintf(stderr, "[plat] %s: %s\n", title ? title : "Error", text ? text : "");
}

bool backendPoll(const SDL_Event& e, Event& ev)
{
    if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
        const SDL_Keysym& k = e.key.keysym;
        const bool down = (e.type == SDL_KEYDOWN);

        if (down && (k.sym == SDLK_RETURN /*START*/ ||
                     k.sym == SDLK_RCTRL  /*MENU*/  ||
                     k.sym == SDLK_ESCAPE /*SELECT*/)) {
            memset(g_heldKeys, 0, sizeof(g_heldKeys));
            ev.type = EV_QUIT;
            fprintf(stderr, "[input] QUIT sym=0x%x\n", (unsigned)k.sym);
            return true;
        }

        int hk = 0;
        switch (k.sym) {
            case SDLK_LEFT:      hk = HGEK_LEFT;  break;
            case SDLK_RIGHT:     hk = HGEK_RIGHT; break;
            case SDLK_UP:        hk = HGEK_UP;    break;
            case SDLK_DOWN:      hk = HGEK_DOWN;  break;
            case SDLK_LALT:      hk = HGEK_SPACE; break; // A -> fire
            case SDLK_LCTRL:     hk = HGEK_R;     break; // B -> restart
            case SDLK_LSHIFT:    hk = HGEK_X;     break; // X
            case SDLK_SPACE:     hk = HGEK_Y;     break; // Y
            case SDLK_TAB:       hk = HGEK_L;     break;
            case SDLK_BACKSPACE: hk = HGEK_R;     break; // R
            default: break;
        }
        if (hk) {
            g_heldKeys[hk] = down ? 1 : 0;
            ev.type = down ? EV_KEYDOWN : EV_KEYUP;
            ev.key  = hk;
            fprintf(stderr, "[input] miyoo sym=0x%x key=%d %s\n",
                    (unsigned)k.sym, hk, down ? "down" : "up");
        }
        return true;
    }

    if (e.type == SDL_CONTROLLERDEVICEADDED) {
        openFirstController();
        ev.type = EV_NONE;
        return true;
    }

    if (e.type == SDL_CONTROLLERBUTTONDOWN || e.type == SDL_CONTROLLERBUTTONUP) {
        const bool down = (e.type == SDL_CONTROLLERBUTTONDOWN);
        if (e.cbutton.button == SDL_CONTROLLER_BUTTON_START) {
            ev.type = down ? EV_QUIT : EV_NONE;
            fprintf(stderr, "[input] pad START %s -> %s\n", down ? "down" : "up",
                    down ? "QUIT" : "-");
            return true;
        }
        const int key = padButtonToHge(e.cbutton.button);
        ev.type = key ? (down ? EV_KEYDOWN : EV_KEYUP) : EV_NONE;
        ev.key  = key;
        fprintf(stderr, "[input] pad btn=%d key=%d %s\n",
                (int)e.cbutton.button, key, down ? "down" : "up");
        return true;
    }

    if (e.type == SDL_JOYBUTTONDOWN || e.type == SDL_JOYBUTTONUP) {
        const bool down = (e.type == SDL_JOYBUTTONDOWN);
        fprintf(stderr, "[input] joy btn=%d %s\n", (int)e.jbutton.button, down ? "down" : "up");
        ev.type = EV_NONE;
        return true;
    }

    return false;
}

int backendKeyDown(int hgeKey)
{
    if (g_heldKeys[hgeKey]) return 1;
    if (g_pad) {
        const int ax = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTX);
        switch (hgeKey) {
        case HGEK_LEFT:
            return (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT) || ax < -8000) ? 1 : 0;
        case HGEK_RIGHT:
            return (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || ax > 8000) ? 1 : 0;
        case HGEK_UP:
            return SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_UP) ? 1 : 0;
        case HGEK_DOWN:
            return SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN) ? 1 : 0;
        case HGEK_SPACE:
            return SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_A) ? 1 : 0;
        case HGEK_R:
            return (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_B) ||
                    SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_Y)) ? 1 : 0;
        default:
            break;
        }
    }
    return -1;   // fall through to the shared keyboard-state path
}

bool backendManualFrameCap()
{
    return true;   // no vsync on the SDL-renderer path
}

} // namespace detail
} // namespace plat
