// SDL2 desktop backend: OpenGL / OpenGL ES window + swap.

#include "plat_sdl2_internal.h"

#include <cstdio>
#include <cstring>

namespace plat {
namespace detail {

bool backendInit(int width, int height, const char* title, bool vsync, bool resizable)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "[plat] SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

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
    backendUpdateSize();
    return true;
}

void backendShutdown()
{
    if (g_ctx) { SDL_GL_DeleteContext(g_ctx); g_ctx = nullptr; }
    if (g_win) { SDL_DestroyWindow(g_win); g_win = nullptr; }
}

void backendUpdateSize()
{
    if (!g_win) return;
    SDL_GL_GetDrawableSize(g_win, &g_w, &g_h);
    SDL_GetWindowSize(g_win, &g_ww, &g_wh);
}

void* backendRenderer()
{
    return nullptr;
}

void* backendGLGetProc(const char* name)
{
    return name ? SDL_GL_GetProcAddress(name) : nullptr;
}

void backendSwapBuffers()
{
    if (g_win) SDL_GL_SwapWindow(g_win);
}

void backendMessageBox(const char* title, const char* text)
{
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,
                             title ? title : "Error",
                             text ? text : "", nullptr);
}

bool backendPoll(const SDL_Event& e, Event& ev)
{
    if (e.type != SDL_KEYDOWN && e.type != SDL_KEYUP)
        return false;

    const SDL_Keysym& k = e.key.keysym;
    const bool down = (e.type == SDL_KEYDOWN);

    ev.type = down ? EV_KEYDOWN : EV_KEYUP;
    ev.key  = 0;
    for (int vk = 0; vk < 256; ++vk) {
        if (g_vk2sc[vk] != 0xFFFF && g_vk2sc[vk] == (u16)k.scancode) {
            ev.key = vk;
            break;
        }
    }
    if (down && (k.sym & SDLK_SCANCODE_MASK) == 0)
        ev.codepoint = (u32)k.sym;

    if (!e.key.repeat)
        fprintf(stderr, "[input] kbd sc=%d sym=0x%x vk=%d %s\n",
                (int)k.scancode, (unsigned)k.sym, ev.key, down ? "down" : "up");
    return true;
}

int backendKeyDown(int /*hgeKey*/)
{
    return -1;   // fall through to the shared keyboard-state path
}

bool backendManualFrameCap()
{
    return false;   // vsync owns pacing on desktop
}

} // namespace detail
} // namespace plat
