// Backend conformance test for the CPU blitter. Asserts real pixels for:
// clear, opaque quad, scissor, texture upload, and alpha blending.

#include "gfx/gfx_blit.h"
#include "hge_gfx.h"

#include <cstddef>
#include <cstdio>

namespace {

int g_failures = 0;

void fail(const char* what, u32 got, u32 want)
{
    fprintf(stderr, "[blit-test] FAIL %s: got %08X want %08X\n", what, got, want);
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

u32 pixel(const void* fb, int pitch, int x, int y)
{
    return ((const u32*)((const u8*)fb + (size_t)y * pitch))[x];
}

} // namespace

int main()
{
    IGfx* g = CreateGfxBlit();
    if (!g || !g->Init(nullptr, nullptr)) {
        fprintf(stderr, "[blit-test] init failed\n");
        return 1;
    }
    g->OnResize(960, 540);

    int w = 0, h = 0, pitch = 0;
    const void* fb = BlitFramebuffer(&w, &h, &pitch);
    if (!fb || w != 640 || h != 480) {
        fprintf(stderr, "[blit-test] unexpected framebuffer %dx%d\n", w, h);
        return 1;
    }

    g->SetTransform(0, 0, 0, 0, 0, 1, 1);
    g->SetScissor(0, 0, 0, 0);

    // 1. clear + opaque full-screen quad
    g->BeginFrame();
    g->Clear(0xFF102030);
    g->SetBlend(GFX_BLEND_NONE);
    drawFull(g, nullptr, 0xFF204060);
    g->EndFrame();
    fb = BlitFramebuffer(&w, &h, &pitch);
    if (pixel(fb, pitch, 320, 240) != 0xFF204060)
        fail("opaque quad", pixel(fb, pitch, 320, 240), 0xFF204060);

    // 2. scissor: 480x270 logical -> 320x180 output, offset y=60
    g->BeginFrame();
    g->Clear(0xFF000000);
    g->SetScissor(0, 0, 480, 270);
    g->SetBlend(GFX_BLEND_NONE);
    drawFull(g, nullptr, 0xFFFF0000);
    g->SetScissor(0, 0, 0, 0);
    g->EndFrame();
    fb = BlitFramebuffer(&w, &h, &pitch);
    if (pixel(fb, pitch, 160, 150) != 0xFFFF0000)
        fail("scissor inside", pixel(fb, pitch, 160, 150), 0xFFFF0000);
    if (pixel(fb, pitch, 500, 300) != 0xFF000000)
        fail("scissor outside", pixel(fb, pitch, 500, 300), 0xFF000000);

    // 2b. the scissor is given in logical coordinates and must go through the
    // same logical -> framebuffer transform as the vertices. 320x180 logical
    // maps to 213x120 fb pixels starting at row 60 (letterboxed 960x540 ->
    // 640x480). Regression guard: an unscaled scissor would paint above row 60
    // and out to x=320.
    g->BeginFrame();
    g->Clear(0xFF000000);
    g->SetScissor(0, 0, 320, 180);
    g->SetBlend(GFX_BLEND_NONE);
    drawFull(g, nullptr, 0xFF00FFFF);
    g->SetScissor(0, 0, 0, 0);
    g->EndFrame();
    fb = BlitFramebuffer(&w, &h, &pitch);
    if (pixel(fb, pitch, 100, 100) != 0xFF00FFFF)
        fail("scissor scaled inside", pixel(fb, pitch, 100, 100), 0xFF00FFFF);
    if (pixel(fb, pitch, 100, 30) != 0xFF000000)
        fail("scissor scaled above band", pixel(fb, pitch, 100, 30), 0xFF000000);
    if (pixel(fb, pitch, 300, 100) != 0xFF000000)
        fail("scissor scaled right of band", pixel(fb, pitch, 300, 100), 0xFF000000);

    // 3. texture upload
    u32 texpx[4] = { 0xFF00FF00u, 0xFF00FF00u, 0xFF00FF00u, 0xFF00FF00u };
    IGfxTexture* tex = g->CreateTexture(2, 2, texpx, 8);
    if (!tex) {
        fprintf(stderr, "[blit-test] CreateTexture failed\n");
        ++g_failures;
    } else {
        g->BeginFrame();
        g->Clear(0xFF000000);
        g->SetBlend(GFX_BLEND_NONE);
        drawFull(g, tex, 0xFFFFFFFF);
        g->EndFrame();
        fb = BlitFramebuffer(&w, &h, &pitch);
        if (pixel(fb, pitch, 320, 240) != 0xFF00FF00)
            fail("texture", pixel(fb, pitch, 320, 240), 0xFF00FF00);
        g->ReleaseTexture(tex);
    }

    // 3b. rotation via SetTransform
    {
        u32 rotpx[16];
        for (int i = 0; i < 16; ++i) rotpx[i] = 0xFFFF0000u;
        rotpx[0] = rotpx[1] = rotpx[4] = rotpx[5] = 0xFF00FF00u;
        IGfxTexture* rot = g->CreateTexture(4, 4, rotpx, 16);
        if (!rot) {
            fprintf(stderr, "[blit-test] rotation texture failed\n");
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
            fb = BlitFramebuffer(&w, &h, &pitch);
            if (pixel(fb, pitch, 373, 187) != 0xFF00FF00u)
                fail("rotation top-right", pixel(fb, pitch, 373, 187), 0xFF00FF00u);
            if (pixel(fb, pitch, 267, 187) != 0xFFFF0000u)
                fail("rotation top-left", pixel(fb, pitch, 267, 187), 0xFFFF0000u);
            g->ReleaseTexture(rot);
        }
    }

    // 4. alpha blend: 0x80 white over black ~= 0x808080
    g->BeginFrame();
    g->Clear(0xFF000000);
    g->SetBlend(GFX_BLEND_ALPHA);
    drawFull(g, nullptr, 0x80FFFFFF);
    g->SetBlend(GFX_BLEND_ALPHA);
    g->EndFrame();
    fb = BlitFramebuffer(&w, &h, &pitch);
    {
        const u32 c = pixel(fb, pitch, 320, 240);
        const int r = (int)((c >> 16) & 0xFF);
        if (r < 110 || r > 145)
            fail("alpha blend", c, 0xFF808080);
    }

    g->Shutdown();
    delete g;

    if (g_failures) {
        fprintf(stderr, "[blit-test] %d failure(s)\n", g_failures);
        return 1;
    }
    printf("[blit-test] GFX CONFORMANCE PASS\n");
    return 0;
}
