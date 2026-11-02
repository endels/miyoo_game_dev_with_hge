// SDL2 platform backend: window, GL/SDL renderer, timing, input.
// One implementation serves Windows, Linux desktop, embedded Linux (kmsdrm)
// and the Miyoo (SigmaStar + custom SDL2). Only the backend differs:
//   default      -> OpenGL / OpenGL ES context (desktop / kmsdrm)
//   HGE_PLATFORM_MIYOO -> SDL_Renderer ("mmiyoo" or "software") + gamepad input

#include "platform/plat.h"

#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(HGE_PLATFORM_MIYOO)
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
#endif

#ifdef HGE_PLATFORM_MIYOO
#define MIYOO_ENABLE 1
#else
#define MIYOO_ENABLE 0
#endif

namespace {

SDL_Window*   g_win  = nullptr;
SDL_GLContext g_ctx  = nullptr;
#if MIYOO_ENABLE
SDL_Renderer*      g_ren = nullptr;   // mmiyoo renderer on the window (gfx draws with it)
SDL_GameController* g_pad = nullptr;
unsigned char      g_heldKeys[256]; // held state by HGEK code
SDL_Joystick*      g_joy = nullptr;
SDL_Surface*       g_off = nullptr;   // offscreen test surface only (BUBBLE_OFFSCREEN)
SDL_Surface*       g_sw  = nullptr;   // software render target (MMA or malloc)

// Output path. The mmiyoo SDL2 video renderers are unusable for a full-screen
// 640x480 app (stock = no-op stubs, PICO-8 = narrow bezel view), so we render
// with SDL's software renderer and present ourselves. Preferred path mirrors
// RetroArch's miyoomini driver: the frame lives in an MI_SYS MMA buffer that
// MI_GFX_BitBlit copies/scales to the panel in hardware (no CPU memcpy).
int                g_fbFd = -1;
bool               g_useGfx = false;   // MI_GFX hardware present path
// CPU fallback (mmap + memcpy)
unsigned char*     g_fbMem = nullptr;
size_t             g_fbFrameBytes = 0;
size_t             g_fbLineBytes = 0;
int                g_fbW = 0, g_fbH = 0;
int                g_fbVisibleY = 0;
struct fb_var_screeninfo g_fbVar;
// MI_GFX hardware present
MI_GFX_Surface_t   g_gfxSrc, g_gfxDst;
MI_GFX_Rect_t      g_gfxSrcRect, g_gfxDstRect;
MI_GFX_Opt_t       g_gfxOpt;
MI_PHY             g_gfxSrcPhy = 0;
void*              g_gfxSrcVir = nullptr;
size_t             g_gfxSrcSize = 0;
struct fb_fix_screeninfo g_gfxFinfo;
struct fb_var_screeninfo g_gfxVinfo;
uint32_t           g_gfxResX = 0, g_gfxResY = 0;
#endif
bool          g_run  = false;
int           g_w    = 0;   // drawable (pixel) size / logical size on miyoo
int           g_h    = 0;
int           g_ww   = 0;   // window (logical) size
int           g_wh   = 0;
#if MIYOO_ENABLE
int           g_lw   = 0;   // logical (render coordinate space) size
int           g_lh   = 0;
#endif
Uint64        g_freq = 0;
Uint64        g_t0   = 0;

// HGE virtual key -> SDL scancode (keyboard path). 0xFFFF = unmapped.
u16 g_vk2sc[256];

struct KeyMap { int vk; SDL_Scancode sc; };

const KeyMap kKeyMap[] = {
    { HGEK_BACKSPACE, SDL_SCANCODE_BACKSPACE },
    { HGEK_TAB,       SDL_SCANCODE_TAB },
    { HGEK_ENTER,     SDL_SCANCODE_RETURN },
    { HGEK_ESCAPE,    SDL_SCANCODE_ESCAPE },
    { HGEK_SPACE,     SDL_SCANCODE_SPACE },
    { HGEK_PAUSE,     SDL_SCANCODE_PAUSE },
    { HGEK_CAPSLOCK,  SDL_SCANCODE_CAPSLOCK },
    { HGEK_PGUP,      SDL_SCANCODE_PAGEUP },
    { HGEK_PGDN,      SDL_SCANCODE_PAGEDOWN },
    { HGEK_HOME,      SDL_SCANCODE_HOME },
    { HGEK_END,       SDL_SCANCODE_END },
    { HGEK_INSERT,    SDL_SCANCODE_INSERT },
    { HGEK_DELETE,    SDL_SCANCODE_DELETE },
    { HGEK_LEFT,      SDL_SCANCODE_LEFT },
    { HGEK_RIGHT,     SDL_SCANCODE_RIGHT },
    { HGEK_UP,        SDL_SCANCODE_UP },
    { HGEK_DOWN,      SDL_SCANCODE_DOWN },
    { HGEK_LSHIFT,    SDL_SCANCODE_LSHIFT },
    { HGEK_RSHIFT,    SDL_SCANCODE_RSHIFT },
    { HGEK_LCTRL,     SDL_SCANCODE_LCTRL },
    { HGEK_RCTRL,     SDL_SCANCODE_RCTRL },
    { HGEK_LALT,      SDL_SCANCODE_LALT },
    { HGEK_RALT,      SDL_SCANCODE_RALT },
    { HGEK_0, SDL_SCANCODE_0 }, { HGEK_1, SDL_SCANCODE_1 },
    { HGEK_2, SDL_SCANCODE_2 }, { HGEK_3, SDL_SCANCODE_3 },
    { HGEK_4, SDL_SCANCODE_4 }, { HGEK_5, SDL_SCANCODE_5 },
    { HGEK_6, SDL_SCANCODE_6 }, { HGEK_7, SDL_SCANCODE_7 },
    { HGEK_8, SDL_SCANCODE_8 }, { HGEK_9, SDL_SCANCODE_9 },
    { HGEK_A, SDL_SCANCODE_A }, { HGEK_B, SDL_SCANCODE_B },
    { HGEK_C, SDL_SCANCODE_C }, { HGEK_D, SDL_SCANCODE_D },
    { HGEK_E, SDL_SCANCODE_E }, { HGEK_F, SDL_SCANCODE_F },
    { HGEK_G, SDL_SCANCODE_G }, { HGEK_H, SDL_SCANCODE_H },
    { HGEK_I, SDL_SCANCODE_I }, { HGEK_J, SDL_SCANCODE_J },
    { HGEK_K, SDL_SCANCODE_K }, { HGEK_L, SDL_SCANCODE_L },
    { HGEK_M, SDL_SCANCODE_M }, { HGEK_N, SDL_SCANCODE_N },
    { HGEK_O, SDL_SCANCODE_O }, { HGEK_P, SDL_SCANCODE_P },
    { HGEK_Q, SDL_SCANCODE_Q }, { HGEK_R, SDL_SCANCODE_R },
    { HGEK_S, SDL_SCANCODE_S }, { HGEK_T, SDL_SCANCODE_T },
    { HGEK_U, SDL_SCANCODE_U }, { HGEK_V, SDL_SCANCODE_V },
    { HGEK_W, SDL_SCANCODE_W }, { HGEK_X, SDL_SCANCODE_X },
    { HGEK_Y, SDL_SCANCODE_Y }, { HGEK_Z, SDL_SCANCODE_Z },
    { HGEK_NUMPAD0, SDL_SCANCODE_KP_0 }, { HGEK_NUMPAD1, SDL_SCANCODE_KP_1 },
    { HGEK_NUMPAD2, SDL_SCANCODE_KP_2 }, { HGEK_NUMPAD3, SDL_SCANCODE_KP_3 },
    { HGEK_NUMPAD4, SDL_SCANCODE_KP_4 }, { HGEK_NUMPAD5, SDL_SCANCODE_KP_5 },
    { HGEK_NUMPAD6, SDL_SCANCODE_KP_6 }, { HGEK_NUMPAD7, SDL_SCANCODE_KP_7 },
    { HGEK_NUMPAD8, SDL_SCANCODE_KP_8 }, { HGEK_NUMPAD9, SDL_SCANCODE_KP_9 },
    { HGEK_F1, SDL_SCANCODE_F1 }, { HGEK_F2, SDL_SCANCODE_F2 },
    { HGEK_F3, SDL_SCANCODE_F3 }, { HGEK_F4, SDL_SCANCODE_F4 },
    { HGEK_F5, SDL_SCANCODE_F5 }, { HGEK_F6, SDL_SCANCODE_F6 },
    { HGEK_F7, SDL_SCANCODE_F7 }, { HGEK_F8, SDL_SCANCODE_F8 },
    { HGEK_F9, SDL_SCANCODE_F9 }, { HGEK_F10, SDL_SCANCODE_F10 },
    { HGEK_F11, SDL_SCANCODE_F11 }, { HGEK_F12, SDL_SCANCODE_F12 }
};

void buildKeyMap()
{
    for (int i = 0; i < 256; ++i) g_vk2sc[i] = 0xFFFF;
    const int n = (int)(sizeof(kKeyMap) / sizeof(kKeyMap[0]));
    for (int i = 0; i < n; ++i)
        g_vk2sc[kKeyMap[i].vk & 0xFF] = (u16)kKeyMap[i].sc;
}

void updateSize()
{
    if (!g_win) return;
#if MIYOO_ENABLE
    // The engine draws in the fixed logical space set by RenderSetLogicalSize.
    SDL_GetWindowSize(g_win, &g_ww, &g_wh);
    g_w = g_lw;
    g_h = g_lh;
#else
    SDL_GL_GetDrawableSize(g_win, &g_w, &g_h);
    SDL_GetWindowSize(g_win, &g_ww, &g_wh);
#endif
}

#if MIYOO_ENABLE
// Map a game controller button to an HGE virtual key (0 = none).
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
    // Fallback: open a raw joystick so button events still reach us (logged).
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

// ---------------------------------------------------------------- MI_GFX path
// Allocate the render target as an MI_SYS MMA buffer and let MI_GFX_BitBlit
// copy/mirror it to the panel (RetroArch miyoomini approach). The SDL software
// renderer draws directly into the mapped MMA buffer, so no extra copy is
// needed on present.
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

    // ARGB8888 matches /dev/fb0 (B=0,G=8,R=16,A=24).
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
    g_gfxOpt.eSrcDfbBldOp = E_MI_GFX_DFB_BLD_ONE;   // dst = src * 1
    g_gfxOpt.eDstDfbBldOp = E_MI_GFX_DFB_BLD_ZERO;  //      + dst * 0
    // The panel is mounted 180 degrees relative to the SDL surface.
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

// CPU fallback: mmap the framebuffer and memcpy the surface (vertical flip).
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
#endif

} // namespace

namespace plat {

bool Init(int width, int height, const char* title, bool vsync, bool resizable)
{
#ifdef SDL_MAIN_HANDLED
    SDL_SetMainReady();
#endif

#if MIYOO_ENABLE
    // Headless verification mode: software renderer into an in-memory surface.
    // Only the timer subsystem is initialised, so /dev/fb0 is never touched.
    {
        const char* off = getenv("BUBBLE_OFFSCREEN");
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
            buildKeyMap();
            g_freq = SDL_GetPerformanceFrequency();
            g_t0   = SDL_GetPerformanceCounter();
            g_run  = true;
            fprintf(stderr, "[plat] OFFSCREEN %dx%d renderer=software\n", width, height);
            return true;
        }
    }

    // Pick the Miyoo SDL video/audio drivers unless the launch script already did.
    SDL_setenv("SDL_VIDEODRIVER", "mmiyoo", 0);
    SDL_setenv("SDL_AUDIODRIVER", "mmiyoo", 0);
#endif

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER
#if MIYOO_ENABLE
                 | SDL_INIT_GAMECONTROLLER
#endif
                 ) != 0) {
        fprintf(stderr, "[plat] SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

#if MIYOO_ENABLE
    // Native panel size for the window; the game draws in a 960x540 logical
    // space that SDL scales (with letterboxing) via RenderSetLogicalSize.
    // Size precedence: BUBBLE_WIN_W/H env (set by launch.sh from Onion's
    // /tmp/screen_resolution) > SDL desktop/current mode > Init args.
    int winW = width, winH = height;
    SDL_DisplayMode dm;
    if (SDL_GetDesktopDisplayMode(0, &dm) != 0 || dm.w <= 0 || dm.h <= 0) {
        if (SDL_GetCurrentDisplayMode(0, &dm) == 0 && dm.w > 0 && dm.h > 0) {
            winW = dm.w; winH = dm.h;
        }
    } else {
        winW = dm.w; winH = dm.h;
    }
    const char* envW = getenv("BUBBLE_WIN_W");
    const char* envH = getenv("BUBBLE_WIN_H");
    if (envW && envH) {
        int w = atoi(envW), h = atoi(envH);
        if (w > 0 && h > 0) { winW = w; winH = h; }
    } else {
        // Onion stores the panel size here (e.g. "640x480"); prefer it over the
        // (unreliable on mmiyoo) SDL display-mode query so the window matches.
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

    // SDL's mmiyoo video renderers cannot present a full-screen 640x480 frame
    // (stock = no-op stubs, PICO-8 = narrow integer bezel view). Render with the
    // software renderer into our own ARGB8888 surface, then blit that surface
    // straight to /dev/fb0 (also ARGB8888) with double buffering.
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    // Preferred: MI_GFX hardware present (RetroArch-style). Fall back to a plain
    // malloc surface + CPU mmap blit if MI_GFX is unavailable.
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
#else
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);

#ifdef HGE_GLES
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#endif

    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN;
    if (resizable) flags |= SDL_WINDOW_RESIZABLE;

    g_win = SDL_CreateWindow(title,
                             SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             width, height, flags);
    if (!g_win) {
        fprintf(stderr, "[plat] CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    g_ctx = SDL_GL_CreateContext(g_win);
    if (!g_ctx) {
        fprintf(stderr, "[plat] CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(g_win);
        g_win = nullptr;
        SDL_Quit();
        return false;
    }

    SDL_GL_SetSwapInterval(vsync ? 1 : 0);
#endif

    updateSize();
    buildKeyMap();

    g_freq = SDL_GetPerformanceFrequency();
    g_t0   = SDL_GetPerformanceCounter();
    g_run  = true;
    return true;
}

void Shutdown()
{
#if MIYOO_ENABLE
    if (g_pad) { SDL_GameControllerClose(g_pad); g_pad = nullptr; }
    if (g_joy) { SDL_JoystickClose(g_joy); g_joy = nullptr; }
    if (g_ren) { SDL_DestroyRenderer(g_ren); g_ren = nullptr; }
    if (g_off) { SDL_FreeSurface(g_off); g_off = nullptr; }
    if (g_sw) { SDL_FreeSurface(g_sw); g_sw = nullptr; }
#endif
    if (g_ctx) { SDL_GL_DeleteContext(g_ctx); g_ctx = nullptr; }
    if (g_win) { SDL_DestroyWindow(g_win); g_win = nullptr; }
    if (g_run) { SDL_Quit(); g_run = false; }
#if MIYOO_ENABLE
    closeDisplay();
#endif
}

void* Window()    { return g_win; }
void* GLContext() { return g_ctx; }
void* Renderer() {
#if MIYOO_ENABLE
    return g_ren;
#else
    return nullptr;
#endif
}

void* GLGetProc(const char* name)
{
#if MIYOO_ENABLE
    (void)name;
    return nullptr;
#else
    return name ? SDL_GL_GetProcAddress(name) : nullptr;
#endif
}

const char* BasePath()
{
    static char buf[1024] = {0};
    static bool done = false;
    if (!done) {
        char* p = SDL_GetBasePath();
        if (p) {
            snprintf(buf, sizeof(buf), "%s", p);
            SDL_free(p);
        }
        done = true;
    }
    return buf;
}

long long FileSize(const char* path)
{
    SDL_RWops* rw = SDL_RWFromFile(path, "rb");
    if (!rw) return -1;
    const Sint64 n = SDL_RWsize(rw);
    SDL_RWclose(rw);
    return (long long)n;
}

bool ReadFileRange(const char* path, long long offset, size_t size,
                   std::vector<u8>& out)
{
    out.clear();
    SDL_RWops* rw = SDL_RWFromFile(path, "rb");
    if (!rw) return false;
    if (offset < 0 || SDL_RWseek(rw, (Sint64)offset, RW_SEEK_SET) < 0) {
        SDL_RWclose(rw);
        return false;
    }
    out.resize(size);
    const size_t got = size ? SDL_RWread(rw, &out[0], 1, size) : 0;
    SDL_RWclose(rw);
    if (got != size) { out.clear(); return false; }
    return true;
}

bool ReadFileAll(const char* path, std::vector<u8>& out)
{
    out.clear();
    SDL_RWops* rw = SDL_RWFromFile(path, "rb");
    if (!rw) return false;
    const Sint64 sz = SDL_RWsize(rw);
    if (sz < 0) { SDL_RWclose(rw); return false; }
    out.resize((size_t)sz);
    if (sz > 0) {
        const size_t got = SDL_RWread(rw, &out[0], 1, (size_t)sz);
        if (got != (size_t)sz) { SDL_RWclose(rw); out.clear(); return false; }
    }
    SDL_RWclose(rw);
    return true;
}

void MessageBox(const char* title, const char* text)
{
#if MIYOO_ENABLE
    fprintf(stderr, "[plat] %s: %s\n", title ? title : "Error", text ? text : "");
#else
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                             title ? title : "Error",
                             text ? text : "", nullptr);
#endif
}

void SwapBuffers()
{
#if MIYOO_ENABLE
    // Flush the software renderer's command queue into g_sw->pixels.
    if (g_ren) SDL_RenderPresent(g_ren);

    // Hardware path: MI_GFX copies/mirrors the MMA-backed frame to the panel.
    if (g_useGfx) { presentGfx(); return; }

    if (g_fbMem && g_sw && g_sw->pixels) {
        const int h = (g_sw->h < g_fbH) ? g_sw->h : g_fbH;
        const int w = (g_sw->w < g_fbW) ? g_sw->w : g_fbW;
        // Draw into the buffer that is not currently displayed, then pan to it.
        const int back = (g_fbVisibleY == 0) ? g_fbH : 0;
        unsigned char* dst = g_fbMem + (size_t)back * g_fbLineBytes;
        // The panel is mounted 180 degrees relative to SDL's surface: rotate 180.
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
#else
    if (g_win) SDL_GL_SwapWindow(g_win);
#endif
}

int  Width()  { return g_w; }
int  Height() { return g_h; }

bool HasFocus()
{
    return g_win && (SDL_GetWindowFlags(g_win) & SDL_WINDOW_INPUT_FOCUS) != 0;
}

void SetTitle(const char* utf8)
{
    if (g_win && utf8) SDL_SetWindowTitle(g_win, utf8);
}

double Time()
{
    if (!g_freq) return 0.0;
    return (double)(SDL_GetPerformanceCounter() - g_t0) / (double)g_freq;
}

void SleepMs(int ms)
{
    if (ms > 0) SDL_Delay((Uint32)ms);
}

bool Poll(Event& ev)
{
    SDL_Event e;
    if (!SDL_PollEvent(&e)) return false;
    memset(&ev, 0, sizeof(ev));

    switch (e.type) {
    case SDL_QUIT:
        ev.type = EV_QUIT;
        break;

    case SDL_WINDOWEVENT:
        if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
            e.window.event == SDL_WINDOWEVENT_RESIZED) {
            updateSize();
            ev.type = EV_RESIZE;
        } else if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
            ev.type = EV_FOCUS_LOST;
        } else if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
            ev.type = EV_FOCUS_GAINED;
        } else {
            ev.type = EV_NONE;
        }
        break;

    case SDL_KEYDOWN:
    case SDL_KEYUP: {
        const SDL_Keysym& k = e.key.keysym;
        const bool down = (e.type == SDL_KEYDOWN);
#if MIYOO_ENABLE
        // MMiyoo delivers buttons as specific SDLK_* keysyms (see RetroArch
        // input/drivers_joypad/sdl_dingux_joypad.c). Map them directly.
        if (down && (k.sym == SDLK_RETURN /*START*/ ||
                     k.sym == SDLK_RCTRL  /*MENU*/  ||
                     k.sym == SDLK_ESCAPE /*SELECT*/)) {
            memset(g_heldKeys, 0, sizeof(g_heldKeys));
            ev.type = EV_QUIT;
            fprintf(stderr, "[input] QUIT sym=0x%x\n", (unsigned)k.sym);
            break;
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
        } else {
            ev.type = EV_NONE;
        }
#else
        ev.type = down ? EV_KEYDOWN : EV_KEYUP;
        ev.key  = 0;
        for (int vk = 0; vk < 256; ++vk) {
            if (g_vk2sc[vk] != 0xFFFF && g_vk2sc[vk] == (u16)k.scancode) {
                ev.key = vk;
                break;
            }
        }
        if (down && (k.sym & SDLK_SCANCODE_MASK) == 0) {
            ev.codepoint = (u32)k.sym;
        }
        if (!e.key.repeat)
            fprintf(stderr, "[input] kbd sc=%d sym=0x%x vk=%d %s\n",
                    (int)k.scancode, (unsigned)k.sym, ev.key, down ? "down" : "up");
#endif
        break;
    }

#if MIYOO_ENABLE
    case SDL_CONTROLLERDEVICEADDED:
        openFirstController();
        ev.type = EV_NONE;
        break;

    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP: {
        const bool down = (e.type == SDL_CONTROLLERBUTTONDOWN);
        if (e.cbutton.button == SDL_CONTROLLER_BUTTON_START) {
            if (down) ev.type = EV_QUIT;   // START exits
            else ev.type = EV_NONE;
            fprintf(stderr, "[input] pad START %s -> %s\n", down ? "down" : "up",
                    down ? "QUIT" : "-");
            break;
        }
        const int key = padButtonToHge(e.cbutton.button);
        if (key) {
            ev.type = down ? EV_KEYDOWN : EV_KEYUP;
            ev.key  = key;
        } else {
            ev.type = EV_NONE;
        }
        fprintf(stderr, "[input] pad btn=%d key=%d %s\n",
                (int)e.cbutton.button, key, down ? "down" : "up");
        break;
    }

    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP: {
        const bool down = (e.type == SDL_JOYBUTTONDOWN);
        fprintf(stderr, "[input] joy btn=%d %s\n", (int)e.jbutton.button, down ? "down" : "up");
        ev.type = EV_NONE;
        break;
    }
#endif

    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        ev.type   = (e.type == SDL_MOUSEBUTTONDOWN) ? EV_MOUSEDOWN : EV_MOUSEUP;
        ev.button = (e.button.button == SDL_BUTTON_LEFT)   ? 0 :
                    (e.button.button == SDL_BUTTON_MIDDLE) ? 1 : 2;
        ev.key    = (ev.button == 0) ? HGEK_LBUTTON :
                    (ev.button == 1) ? HGEK_MBUTTON : HGEK_RBUTTON;
        ev.x      = e.button.x;
        ev.y      = e.button.y;
        break;

    case SDL_MOUSEMOTION:
        ev.type = EV_MOUSEMOVE;
        ev.x    = e.motion.x;
        ev.y    = e.motion.y;
        break;

    case SDL_MOUSEWHEEL:
        ev.type  = EV_MOUSEWHEEL;
        ev.wheel = e.wheel.y;
        break;

    default:
        ev.type = EV_NONE;
        break;
    }
    return true;
}

bool KeyDown(int hgeKey)
{
    if (hgeKey < 0 || hgeKey > 255) return false;
#if MIYOO_ENABLE
    if (g_heldKeys[hgeKey]) return true;  // MMiyoo button keysyms
    if (g_pad) {
        // Continuous aiming/fire from the gamepad (the game polls held state
        // for aiming so we do not rely on keyboard repeat).
        const int ax = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTX);
        switch (hgeKey) {
        case HGEK_LEFT:
            return SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT) || ax < -8000;
        case HGEK_RIGHT:
            return SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || ax > 8000;
        case HGEK_UP:
            return SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_UP);
        case HGEK_DOWN:
            return SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
        case HGEK_SPACE:
            return SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_A);
        case HGEK_R:
            return SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_B) ||
                   SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_Y);
        default:
            break;
        }
    }
#endif
    u16 sc = g_vk2sc[hgeKey];
    if (sc == 0xFFFF) return false;
    const Uint8* ks = SDL_GetKeyboardState(nullptr);
    return ks && ks[sc] != 0;
}

void MousePos(int* x, int* y)
{
    int mx = 0, my = 0;
    SDL_GetMouseState(&mx, &my);
    if (x) *x = mx;
    if (y) *y = my;
}

void SetMouseRelative(bool on)
{
    SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE);
}

} // namespace plat
