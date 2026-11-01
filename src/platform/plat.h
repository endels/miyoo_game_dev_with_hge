#ifndef HGE_PLAT_H
#define HGE_PLAT_H

// Platform layer: the only part of the engine that talks to the OS.
// Backed by SDL2 for Windows / Linux / embedded Linux (kmsdrm).
//
// Everything above this file is platform independent. When the real HGE core
// is ported (M2) hge_impl.cpp/system.cpp/input.cpp call into this and nothing
// else.

#include "hge_types.h"
#include <vector>

namespace plat {

// ---- events -------------------------------------------------------------
enum EventType {
    EV_NONE = 0,
    EV_QUIT,
    EV_RESIZE,
    EV_KEYDOWN,
    EV_KEYUP,
    EV_MOUSEDOWN,
    EV_MOUSEUP,
    EV_MOUSEMOVE,
    EV_MOUSEWHEEL,
    EV_FOCUS_LOST,
    EV_FOCUS_GAINED
};

struct Event {
    int  type;       // EventType
    int  key;        // HGEK_* for key events, 0 otherwise
    int  button;     // 0=left 1=middle 2=right (mouse)
    int  x, y;       // mouse position (logical pixels)
    int  wheel;      // +1 / -1
    u32  codepoint;  // unicode for text input, 0 otherwise
};

// ---- lifecycle ----------------------------------------------------------
// Creates window + OpenGL context. On desktop a GL 2.1 context is requested;
// on an ES-only platform (embedded) an OpenGL ES 2.0 context is requested.
bool  Init(int width, int height, const char* title, bool vsync, bool resizable);
void  Shutdown();

void* Window();      // SDL_Window*   (opaque to the engine)
void* GLContext();   // SDL_GLContext (opaque)
void* Renderer();    // SDL_Renderer* (opaque) on the SDL-render-backed build

// GL entry point resolver; same signature as GfxProcGetter in igfx.h.
void* GLGetProc(const char* name);

// Directory of the executable, with trailing separator ("" if unknown).
const char* BasePath();

// Reads a whole file. Uses SDL_RWops so UTF-8 / non-ASCII paths work on
// Windows (plain fopen() would fail on the ANSI code page).
bool ReadFileAll(const char* path, std::vector<u8>& out);

// For the self-contained exe: the paks are appended after the PE image and
// located through a footer, so only range reads are needed.
long long FileSize(const char* path);
bool ReadFileRange(const char* path, long long offset, size_t size,
                   std::vector<u8>& out);

void  SwapBuffers();

// Native error dialog (SDL_ShowSimpleMessageBox); no-op if SDL is not up.
void  MessageBox(const char* title, const char* text);

int   Width();
int   Height();
bool  HasFocus();
void  SetTitle(const char* utf8);

// ---- timing -------------------------------------------------------------
double Time();               // seconds since Init()
void   SleepMs(int ms);

// ---- input --------------------------------------------------------------
bool  Poll(Event& ev);       // false when the queue is empty for this frame
bool  KeyDown(int hgeKey);   // HGEK_* -> held state
void  MousePos(int* x, int* y);
void  SetMouseRelative(bool on);   // for first person / lock-on styles

} // namespace plat

// ---- HGE-compatible key codes -------------------------------------------
// Kept numerically identical to HGE 1.81 (which used Win32 virtual keys) so
// existing HGE game code keeps compiling untouched. plat_sdl2.cpp maps these
// to SDL scancodes at startup.
enum {
    HGEK_LBUTTON   = 0x01, HGEK_RBUTTON = 0x02, HGEK_MBUTTON = 0x04,
    HGEK_BACKSPACE = 0x08, HGEK_TAB     = 0x09, HGEK_ENTER   = 0x0D,
    HGEK_SHIFT     = 0x10, HGEK_CONTROL = 0x11, HGEK_ALT     = 0x12,
    HGEK_PAUSE     = 0x13, HGEK_CAPSLOCK= 0x14,
    HGEK_ESCAPE    = 0x1B, HGEK_SPACE   = 0x20,
    HGEK_PGUP      = 0x21, HGEK_PGDN    = 0x22, HGEK_END     = 0x23,
    HGEK_HOME      = 0x24,
    HGEK_LEFT      = 0x25, HGEK_UP      = 0x26, HGEK_RIGHT   = 0x27,
    HGEK_DOWN      = 0x28,
    HGEK_INSERT    = 0x2D, HGEK_DELETE  = 0x2E,
    HGEK_0 = 0x30, HGEK_1 = 0x31, HGEK_2 = 0x32, HGEK_3 = 0x33,
    HGEK_4 = 0x34, HGEK_5 = 0x35, HGEK_6 = 0x36, HGEK_7 = 0x37,
    HGEK_8 = 0x38, HGEK_9 = 0x39,
    HGEK_A = 0x41, HGEK_B = 0x42, HGEK_C = 0x43, HGEK_D = 0x44,
    HGEK_E = 0x45, HGEK_F = 0x46, HGEK_G = 0x47, HGEK_H = 0x48,
    HGEK_I = 0x49, HGEK_J = 0x4A, HGEK_K = 0x4B, HGEK_L = 0x4C,
    HGEK_M = 0x4D, HGEK_N = 0x4E, HGEK_O = 0x4F, HGEK_P = 0x50,
    HGEK_Q = 0x51, HGEK_R = 0x52, HGEK_S = 0x53, HGEK_T = 0x54,
    HGEK_U = 0x55, HGEK_V = 0x56, HGEK_W = 0x57, HGEK_X = 0x58,
    HGEK_Y = 0x59, HGEK_Z = 0x5A,
    HGEK_NUMPAD0 = 0x60, HGEK_NUMPAD1 = 0x61, HGEK_NUMPAD2 = 0x62,
    HGEK_NUMPAD3 = 0x63, HGEK_NUMPAD4 = 0x64, HGEK_NUMPAD5 = 0x65,
    HGEK_NUMPAD6 = 0x66, HGEK_NUMPAD7 = 0x67, HGEK_NUMPAD8 = 0x68,
    HGEK_NUMPAD9 = 0x69,
    HGEK_F1 = 0x70, HGEK_F2 = 0x71, HGEK_F3 = 0x72, HGEK_F4 = 0x73,
    HGEK_F5 = 0x74, HGEK_F6 = 0x75, HGEK_F7 = 0x76, HGEK_F8 = 0x77,
    HGEK_F9 = 0x78, HGEK_F10 = 0x79, HGEK_F11 = 0x7A, HGEK_F12 = 0x7B,
    HGEK_LSHIFT = 0xA0, HGEK_RSHIFT = 0xA1,
    HGEK_LCTRL  = 0xA2, HGEK_RCTRL  = 0xA3,
    HGEK_LALT   = 0xA4, HGEK_RALT   = 0xA5
};

#endif // HGE_PLAT_H
