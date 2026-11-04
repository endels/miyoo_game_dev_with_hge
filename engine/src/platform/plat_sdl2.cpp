// SDL2 platform layer — common part.
//
// Everything here is shared by the desktop and Miyoo backends. The backend
// .cpp owns window/context creation, presentation, and event translation.

#include "plat_sdl2_internal.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#ifdef __linux__
#include <sys/prctl.h>
#endif

namespace plat {
namespace detail {

SDL_Window*   g_win = nullptr;
SDL_GLContext g_ctx = nullptr;
int           g_w   = 0;
int           g_h   = 0;
int           g_ww  = 0;
int           g_wh  = 0;
Uint64        g_freq = 0;
Uint64        g_t0   = 0;

u16 g_vk2sc[256];

namespace {

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

} // namespace

void buildKeyMap()
{
    for (int i = 0; i < 256; ++i) g_vk2sc[i] = 0xFFFF;
    const int n = (int)(sizeof(kKeyMap) / sizeof(kKeyMap[0]));
    for (int i = 0; i < n; ++i)
        g_vk2sc[kKeyMap[i].vk & 0xFF] = (u16)kKeyMap[i].sc;
}

} // namespace detail

bool Init(int width, int height, const char* title, bool vsync, bool resizable)
{
#ifdef __linux__
    // Opt-in debug helper: allow an external debugger (gdb -p) to attach even
    // when /proc/sys/kernel/yama/ptrace_scope is 1. Enabled either at build
    // time (HGE_DEBUG_ATTACH, used by the debug preset) or per run with
    // HGE_ALLOW_GDB=1.
#ifdef HGE_DEBUG_ATTACH
    prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);
#else
    if (const char* allow = getenv("HGE_ALLOW_GDB")) {
        if (allow[0] && allow[0] != '0')
            prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0);
    }
#endif
#endif
#ifdef SDL_MAIN_HANDLED
    SDL_SetMainReady();
#endif
    if (!detail::backendInit(width, height, title, vsync, resizable))
        return false;

    detail::backendUpdateSize();
    detail::buildKeyMap();
    detail::g_freq = SDL_GetPerformanceFrequency();
    detail::g_t0   = SDL_GetPerformanceCounter();
    return true;
}

void Shutdown()
{
    detail::backendShutdown();
    SDL_Quit();
}

void* Window()    { return detail::g_win; }
void* GLContext() { return detail::g_ctx; }
void* Renderer()  { return detail::backendRenderer(); }

void* GLGetProc(const char* name)
{
    return detail::backendGLGetProc(name);
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
    detail::backendMessageBox(title, text);
}

void SwapBuffers()
{
    detail::backendSwapBuffers();
}

int  Width()  { return detail::g_w; }
int  Height() { return detail::g_h; }

bool HasFocus()
{
    return detail::g_win &&
           (SDL_GetWindowFlags(detail::g_win) & SDL_WINDOW_INPUT_FOCUS) != 0;
}

void SetTitle(const char* utf8)
{
    if (detail::g_win && utf8) SDL_SetWindowTitle(detail::g_win, utf8);
}

double Time()
{
    if (!detail::g_freq) return 0.0;
    return (double)(SDL_GetPerformanceCounter() - detail::g_t0) / (double)detail::g_freq;
}

void SleepMs(int ms)
{
    if (ms > 0) SDL_Delay((Uint32)ms);
}

bool ManualFrameCap()
{
    return detail::backendManualFrameCap();
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
            detail::backendUpdateSize();
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
    case SDL_KEYUP:
    case SDL_CONTROLLERDEVICEADDED:
    case SDL_CONTROLLERBUTTONDOWN:
    case SDL_CONTROLLERBUTTONUP:
    case SDL_JOYBUTTONDOWN:
    case SDL_JOYBUTTONUP:
        if (!detail::backendPoll(e, ev))
            ev.type = EV_NONE;
        break;

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

    const int backend = detail::backendKeyDown(hgeKey);
    if (backend >= 0) return backend != 0;

    const u16 sc = detail::g_vk2sc[hgeKey];
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
