// libretro platform layer. Runs the generic engine entry on a ucontext
// coroutine: each retro_run() advances exactly one frame, because
// plat::SwapBuffers() yields back to the frontend.

#include "platform/plat.h"
#include "platform/plat_libretro.h"
#include "gfx/gfx_blit.h"

#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <ucontext.h>

namespace {

const int FB_W = 640;   // render resolution handed to the frontend (4:3 panel)
const int FB_H = 480;   // game draws 16:9 inside this with letterbox bars

SDL_Renderer* g_ren  = nullptr;
SDL_Surface*  g_surf = nullptr;
int           g_w = 0, g_h = 0;

ucontext_t g_ucMain, g_ucGame;
char       g_stack[1024 * 1024];
bool       g_started = false;
bool       g_done    = false;
PlatLibretroEntry g_entry = nullptr;

unsigned g_held = 0;
plat::Event g_queue[8];
int g_qHead = 0, g_qTail = 0;

// Test hook: HGE_TEST_FRAMES=N makes the game quit after N frames so the
// core can be exercised from a shell without a controller.
int  g_testFrames = 0;
int  g_frameCount = 0;
bool g_quitPushed = false;

void pushEvent(int type, int key)
{
    const int next = (g_qTail + 1) % 8;
    if (next == g_qHead) return;
    plat::Event& e = g_queue[g_qTail];
    memset(&e, 0, sizeof(e));
    e.type = type;
    e.key  = key;
    g_qTail = next;
}

void gameEntry()
{
    if (g_entry)
        g_entry(0, nullptr);
    g_done = true;
    setcontext(&g_ucMain);   // return to the last retro_load_game/retro_run
}

} // namespace

// ---------------------------------------------------------------- stepping
void plat_libretro_set_entry(PlatLibretroEntry fn)
{
    g_entry = fn;
}

int plat_libretro_begin()
{
    getcontext(&g_ucGame);
    g_ucGame.uc_stack.ss_sp   = g_stack;
    g_ucGame.uc_stack.ss_size = sizeof(g_stack);
    g_ucGame.uc_link          = &g_ucMain;
    makecontext(&g_ucGame, gameEntry, 0);
    g_started = true;
    const char* tf = getenv("HGE_TEST_FRAMES");
    g_testFrames = tf ? atoi(tf) : 0;
    swapcontext(&g_ucMain, &g_ucGame);   // run until first SwapBuffers / exit
    return g_done ? 0 : 1;
}

int plat_libretro_step()
{
    if (!g_started || g_done) return 0;
    if (g_testFrames > 0 && ++g_frameCount >= g_testFrames && !g_quitPushed) {
        g_quitPushed = true;
        pushEvent(plat::EV_QUIT, 0);
    }
    swapcontext(&g_ucMain, &g_ucGame);
    return g_done ? 0 : 1;
}

const void* plat_libretro_fb(int* w, int* h, int* pitch)
{
    if (!g_surf) return nullptr;
    if (w) *w = g_surf->w;
    if (h) *h = g_surf->h;
    if (pitch) *pitch = g_surf->pitch;
    return g_surf->pixels;
}

void plat_libretro_set_buttons(unsigned held, unsigned pressed)
{
    g_held = held;
    if (pressed & (PB_START | PB_SELECT)) pushEvent(plat::EV_QUIT, 0);
    if (pressed & PB_A) pushEvent(plat::EV_KEYDOWN, HGEK_SPACE);
    if (pressed & (PB_B | PB_Y)) pushEvent(plat::EV_KEYDOWN, HGEK_R);
}

namespace plat {

bool Init(int width, int height, const char* /*title*/, bool /*vsync*/, bool /*resizable*/)
{
#ifdef SDL_MAIN_HANDLED
    SDL_SetMainReady();
#endif
    if (SDL_Init(SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "[plat] SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    g_surf = SDL_CreateRGBSurfaceWithFormat(0, FB_W, FB_H, 32,
                                            SDL_PIXELFORMAT_ARGB8888);
    if (!g_surf) return false;
    g_ren = SDL_CreateSoftwareRenderer(g_surf);
    if (!g_ren) return false;

    g_w = width;   // logical size the game draws in (960x540)
    g_h = height;
    fprintf(stderr, "[plat] libretro surface %dx%d logical %dx%d\n",
            FB_W, FB_H, g_w, g_h);
    return true;
}

void Shutdown()
{
    // Do not tear SDL down here: the frontend may still be reading the last
    // frame we handed it. Real cleanup happens in plat_libretro_cleanup(),
    // called from retro_deinit after the frontend has stopped using it.
}

void* Window()     { return nullptr; }
void* GLContext()  { return nullptr; }
void* Renderer()   { return g_ren; }
void* GLGetProc(const char*) { return nullptr; }

const char* BasePath()
{
    static char buf[1024] = {0};
    static bool done = false;
    if (!done) {
        const char* env = getenv("HGE_DIR");
        if (env && env[0]) {
            snprintf(buf, sizeof(buf), "%s", env);
        } else {
            // A libretro core cannot know where the frontend put the app data,
            // so the launcher is expected to export HGE_DIR. Falling back to the
            // working directory keeps the engine free of any game's install path.
            fprintf(stderr, "[hge] HGE_DIR not set; BasePath() falls back to ./\n");
            snprintf(buf, sizeof(buf), "./");
        }
        const size_t n = strlen(buf);
        if (n && buf[n - 1] != '/' && n + 1 < sizeof(buf)) {
            buf[n] = '/'; buf[n + 1] = 0;
        }
        done = true;
    }
    return buf;
}

long long FileSize(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    const long long n = ftell(f);
    fclose(f);
    return n;
}

bool ReadFileRange(const char* path, long long offset, size_t size,
                   std::vector<u8>& out)
{
    out.clear();
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    if (offset < 0 || fseek(f, (long)offset, SEEK_SET) != 0) { fclose(f); return false; }
    out.resize(size);
    const size_t got = size ? fread(&out[0], 1, size, f) : 0;
    fclose(f);
    if (got != size) { out.clear(); return false; }
    return true;
}

bool ReadFileAll(const char* path, std::vector<u8>& out)
{
    out.clear();
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    const long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return false; }
    out.resize((size_t)n);
    const size_t got = n ? fread(&out[0], 1, (size_t)n, f) : 0;
    fclose(f);
    if (got != (size_t)n) { out.clear(); return false; }
    return true;
}

void MessageBox(const char* title, const char* text)
{
    fprintf(stderr, "[hge] %s: %s\n", title ? title : "Error", text ? text : "");
}

void SwapBuffers()
{
    // Frame boundary: rasterize everything the game queued this frame.
    BlitFlush();
    if (g_started && !g_done)
        swapcontext(&g_ucGame, &g_ucMain);
}

int  Width()  { return g_w; }
int  Height() { return g_h; }
bool HasFocus() { return true; }
void SetTitle(const char*) {}

double Time()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

void SleepMs(int /*ms*/) {}   // pacing is owned by the frontend

bool ManualFrameCap() { return false; }

bool Poll(Event& ev)
{
    if (g_qHead == g_qTail) return false;
    ev = g_queue[g_qHead];
    g_qHead = (g_qHead + 1) % 8;
    return true;
}

bool KeyDown(int hgeKey)
{
    switch (hgeKey) {
    case HGEK_LEFT:  return (g_held & PB_LEFT) != 0;
    case HGEK_RIGHT: return (g_held & PB_RIGHT) != 0;
    case HGEK_UP:    return (g_held & PB_UP) != 0;
    case HGEK_DOWN:  return (g_held & PB_DOWN) != 0;
    case HGEK_SPACE: return (g_held & PB_A) != 0;
    case HGEK_R:     return (g_held & (PB_B | PB_Y)) != 0;
    default:         return false;
    }
}

void MousePos(int* x, int* y) { if (x) *x = 0; if (y) *y = 0; }
void SetMouseRelative(bool) {}

} // namespace plat

void plat_libretro_cleanup()
{
    if (g_ren) { SDL_DestroyRenderer(g_ren); g_ren = nullptr; }
    if (g_surf) { SDL_FreeSurface(g_surf); g_surf = nullptr; }
    SDL_Quit();
}
