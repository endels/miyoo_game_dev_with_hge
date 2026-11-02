// Backend conformance test for gfx_sdl using an SDL software renderer as the
// offscreen target. This covers the Miyoo renderer path on the host.

#include "gfx/gfx_sdl.h"
#include "hge_gfx.h"

#include <SDL.h>
#include <cstdio>
#include <vector>

namespace {
SDL_Renderer* g_ren = nullptr;
}

// gfx_sdl only needs these four platform entry points.
namespace plat {
void*  Renderer() { return g_ren; }
int    Width()    { return 960; }
int    Height()   { return 540; }
double Time()     { return 0.0; }
}

namespace {

int g_failures = 0;

void fail(const char* what, u32 got, u32 want)
{
    fprintf(stderr, "[sdl-test] FAIL %s: got %08X want %08X\n", what, got, want);
    ++g_failures;
}

void drawFull(IGfx* g, IGfxTexture* tex, u32 color)
{
    GfxVertex v[4];
    for (int i = 0; i < 4; ++i) {
        v[i].z = 0.0f;
        v[i].color = color;
    }
    v[0].x = 0;     v[0].y = 0;     v[0].u = 0; v[0].v = 0;
    v[1].x = 960;   v[1].y = 0;     v[1].u = 1; v[1].v = 0;
    v[2].x = 0;     v[2].y = 540;   v[2].u = 0; v[2].v = 1;
    v[3].x = 960;   v[3].y = 540;   v[3].u = 1; v[3].v = 1;
    g->DrawQuads(tex, v, 1);
}

bool readFrame(IGfx* g, std::vector<u32>& px)
{
    SDL_RenderPresent(g_ren);
    px.assign((size_t)960 * 540, 0);
    return g->ReadPixels(0, 0, 960, 540, &px[0]);
}

} // namespace

int main()
{
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "[sdl-test] SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(
        0, 960, 540, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surf) {
        fprintf(stderr, "[sdl-test] surface failed: %s\n", SDL_GetError());
        return 1;
    }
    g_ren = SDL_CreateSoftwareRenderer(surf);
    if (!g_ren) {
        fprintf(stderr, "[sdl-test] renderer failed: %s\n", SDL_GetError());
        return 1;
    }

    IGfx* g = CreateGfxSDL();
    if (!g || !g->Init(nullptr, nullptr)) {
        fprintf(stderr, "[sdl-test] gfx init failed\n");
        return 1;
    }
    g->OnResize(960, 540);

    std::vector<u32> px;
    g->SetTransform(0, 0, 0, 0, 0, 1, 1);
    g->SetScissor(0, 0, 0, 0);

    // 1. clear + opaque quad
    g->BeginFrame();
    g->Clear(0xFF102030);
    g->SetBlend(GFX_BLEND_NONE);
    drawFull(g, nullptr, 0xFF204060);
    g->EndFrame();
    if (!readFrame(g, px)) {
        fprintf(stderr, "[sdl-test] ReadPixels failed\n");
        return 1;
    }
    if (px[(size_t)270 * 960 + 480] != 0xFF204060)
        fail("opaque quad", px[(size_t)270 * 960 + 480], 0xFF204060);

    // 2. scissor
    g->BeginFrame();
    g->Clear(0xFF000000);
    g->SetScissor(0, 0, 480, 270);
    g->SetBlend(GFX_BLEND_NONE);
    drawFull(g, nullptr, 0xFFFF0000);
    g->SetScissor(0, 0, 0, 0);
    g->EndFrame();
    if (!readFrame(g, px)) return 1;
    if (px[(size_t)150 * 960 + 160] != 0xFFFF0000)
        fail("scissor inside", px[(size_t)150 * 960 + 160], 0xFFFF0000);
    if (px[(size_t)300 * 960 + 500] != 0xFF000000)
        fail("scissor outside", px[(size_t)300 * 960 + 500], 0xFF000000);

    // 3. texture upload
    u32 texpx[4] = { 0xFF00FF00u, 0xFF00FF00u, 0xFF00FF00u, 0xFF00FF00u };
    IGfxTexture* tex = g->CreateTexture(2, 2, texpx, 8);
    if (!tex) {
        fprintf(stderr, "[sdl-test] CreateTexture failed\n");
        ++g_failures;
    } else {
        g->BeginFrame();
        g->Clear(0xFF000000);
        g->SetBlend(GFX_BLEND_NONE);
        drawFull(g, tex, 0xFFFFFFFF);
        g->EndFrame();
        if (!readFrame(g, px)) return 1;
        if (px[(size_t)270 * 960 + 480] != 0xFF00FF00)
            fail("texture", px[(size_t)270 * 960 + 480], 0xFF00FF00);
        g->ReleaseTexture(tex);
    }

    // 3b. rotation via SetTransform (MMIYOO has no SDL_RenderCopyEx)
    {
        u32 rotpx[16];
        for (int i = 0; i < 16; ++i) rotpx[i] = 0xFFFF0000u;
        rotpx[0] = rotpx[1] = rotpx[4] = rotpx[5] = 0xFF00FF00u;
        IGfxTexture* rot = g->CreateTexture(4, 4, rotpx, 16);
        if (!rot) {
            fprintf(stderr, "[sdl-test] rotation texture failed\n");
            ++g_failures;
        } else {
            GfxVertex v[4];
            for (int i = 0; i < 4; ++i) { v[i].z = 0.0f; v[i].color = 0xFFFFFFFFu; }
            v[0].x = 380; v[0].y = 170; v[0].u = 0; v[0].v = 0;
            v[1].x = 580; v[1].y = 170; v[1].u = 1; v[1].v = 0;
            v[2].x = 380; v[2].y = 370; v[2].u = 0; v[2].v = 1;
            v[3].x = 580; v[3].y = 370; v[3].u = 1; v[3].v = 1;

            g->BeginFrame();
            g->Clear(0xFF000000);
            g->SetBlend(GFX_BLEND_NONE);
            g->SetTransform(480, 270, 0, 0, 1.5707964f, 1, 1);
            g->DrawQuads(rot, v, 1);
            g->SetTransform(0, 0, 0, 0, 0, 1, 1);
            g->EndFrame();
            if (!readFrame(g, px)) return 1;
            if (px[(size_t)190 * 960 + 560] != 0xFF00FF00u)
                fail("rotation top-right", px[(size_t)190 * 960 + 560], 0xFF00FF00u);
            if (px[(size_t)190 * 960 + 400] != 0xFFFF0000u)
                fail("rotation top-left", px[(size_t)190 * 960 + 400], 0xFFFF0000u);
            g->ReleaseTexture(rot);
        }
    }

    // 4. alpha blend
    g->BeginFrame();
    g->Clear(0xFF000000);
    g->SetBlend(GFX_BLEND_ALPHA);
    drawFull(g, nullptr, 0x80FFFFFF);
    g->EndFrame();
    if (!readFrame(g, px)) return 1;
    {
        const u32 c = px[(size_t)270 * 960 + 480];
        const int r = (int)((c >> 16) & 0xFF);
        if (r < 110 || r > 145)
            fail("alpha blend", c, 0xFF808080);
    }

    g->Shutdown();
    delete g;
    SDL_DestroyRenderer(g_ren);
    SDL_FreeSurface(surf);
    SDL_Quit();

    if (g_failures) {
        fprintf(stderr, "[sdl-test] %d failure(s)\n", g_failures);
        return 1;
    }
    printf("[sdl-test] GFX CONFORMANCE PASS\n");
    return 0;
}
