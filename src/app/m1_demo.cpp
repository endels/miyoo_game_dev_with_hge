// M1 skeleton demo.
//
// Purpose: prove the whole new stack works end to end -- SDL2 window,
// GL/GLES context, shader, dynamic VBO batching, ARGB->RGBA upload with the
// .bgra swizzle, NPOT textures, alpha blending, scissor clipping and the
// HGE-style 2D transform.
//
// It draws a 20x11 grid of 48px tiles (the FSM tile size) plus a rotating
// sprite, because that is exactly the workload the port has to survive.
//
//   m1_demo                 normal run, ESC to quit
//   m1_demo --frames 300    run 300 frames then exit
//   m1_demo --selftest      run headless-ish, verify pixels, exit 0/1

#include "platform/plat.h"
#include "gfx/igfx.h"
#include "hge_types.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

// ---- procedural test art (M2 replaces this with stb_image + FSM sheets) --
IGfxTexture* makeTileTexture(IGfx* gfx)
{
    const int S = 48;
    u32 px[S * S];
    const u32 base   = 0xFF2E4A2E;
    const u32 alt    = 0xFF3A5A38;
    const u32 border = 0xFF6A8F5A;
    for (int y = 0; y < S; ++y) {
        for (int x = 0; x < S; ++x) {
            u32 c = (((x / 24) + (y / 24)) & 1) ? alt : base;
            if (x == 0 || y == 0 || x == S - 1 || y == S - 1) c = border;
            px[y * S + x] = c;
        }
    }
    return gfx->CreateTexture(S, S, px, S * 4);
}

IGfxTexture* makeSpriteTexture(IGfx* gfx)
{
    const int S = 96;   // NPOT on purpose: exercises the ES2 NPOT rules
    u32 px[S * S];
    const f32 cx = 47.5f, cy = 47.5f;
    for (int y = 0; y < S; ++y) {
        for (int x = 0; x < S; ++x) {
            const f32 d = fabsf(x - cx) + fabsf(y - cy);   // diamond
            u32 c;
            if (d < 26.0f)      c = 0xFFF0E0A0;
            else if (d < 38.0f) c = 0xFFFFFFFF;
            else                c = 0x00000000;
            px[y * S + x] = c;
        }
    }
    return gfx->CreateTexture(S, S, px, S * 4);
}

void drawQuad(IGfx* gfx, IGfxTexture* tex,
              float x0, float y0, float x1, float y1,
              float u0, float v0, float u1, float v1, u32 color)
{
    GfxVertex v[4];
    v[0].x = x0; v[0].y = y0; v[0].z = 0; v[0].u = u0; v[0].v = v0; v[0].color = color;
    v[1].x = x1; v[1].y = y0; v[1].z = 0; v[1].u = u1; v[1].v = v0; v[1].color = color;
    v[2].x = x0; v[2].y = y1; v[2].z = 0; v[2].u = u0; v[2].v = v1; v[2].color = color;
    v[3].x = x1; v[3].y = y1; v[3].z = 0; v[3].u = u1; v[3].v = v1; v[3].color = color;
    gfx->DrawQuads(tex, v, 1);
}

int channelDiff(u32 a, u32 b, int shift)
{
    int ca = (int)((a >> shift) & 0xFF);
    int cb = (int)((b >> shift) & 0xFF);
    int d = ca - cb;
    return d < 0 ? -d : d;
}

bool near(u32 got, u32 want, int tol)
{
    return channelDiff(got, want, 0)  <= tol &&
           channelDiff(got, want, 8)  <= tol &&
           channelDiff(got, want, 16) <= tol;
}

} // namespace

int main(int argc, char** argv)
{
    int  frames    = 0;      // 0 = run forever
    bool selftest  = false;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--selftest")) { selftest = true; frames = 30; }
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
    }

    const int W = 960, H = 540;
    if (!plat::Init(W, H, "HGE-SDL M1", !selftest, true)) {
        fprintf(stderr, "[m1] platform init failed\n");
        return 2;
    }

    IGfx* gfx = CreateGfxGL();
    if (!gfx->Init(plat::Window(), plat::GLGetProc)) {
        fprintf(stderr, "[m1] graphics init failed\n");
        plat::Shutdown();
        return 2;
    }
    gfx->OnResize(plat::Width(), plat::Height());
    printf("[m1] renderer: %s\n", gfx->Info());

    IGfxTexture* tile   = makeTileTexture(gfx);
    IGfxTexture* sprite = makeSpriteTexture(gfx);
    if (!tile || !sprite) {
        fprintf(stderr, "[m1] texture creation failed\n");
        return 2;
    }

    const u32 bg       = HGE_ARGB(255, 16, 16, 24);
    const u32 probeCol = HGE_ARGB(255, 204, 32, 32);
    const float dt     = 1.0f / 60.0f;

    int   frame      = 0;
    float angle      = 0.0f;
    bool  quit       = false;
    double lastTitle = 0.0;
    int   intW = plat::Width(), intH = plat::Height();

    while (!quit) {
        plat::Event ev;
        while (plat::Poll(ev)) {
            if (ev.type == plat::EV_QUIT) quit = true;
            if (ev.type == plat::EV_KEYDOWN && ev.key == HGEK_ESCAPE) quit = true;
            if (ev.type == plat::EV_RESIZE) {
                intW = plat::Width();
                intH = plat::Height();
                gfx->OnResize(intW, intH);
            }
        }
        if (intW != plat::Width() || intH != plat::Height()) {
            intW = plat::Width();
            intH = plat::Height();
            gfx->OnResize(intW, intH);
        }

        const int  lastFrame = frames > 0 && frame >= frames - 1;
        const bool probe     = selftest && lastFrame;

        gfx->BeginFrame();
        gfx->Clear(bg);

        // 1) 20x11 tile grid, 48px each, one batch
        gfx->SetTransform(0, 0, 0, 0, 0, 1, 1);
        gfx->SetScissor(0, 0, 0, 0);
        gfx->SetBlend(GFX_BLEND_ALPHA);

        GfxVertex grid[220 * 4];
        int q = 0;
        for (int ty = 0; ty < 11; ++ty) {
            for (int tx = 0; tx < 20; ++tx) {
                const float x = (float)(tx * 48);
                const float y = 6.0f + (float)(ty * 48);
                GfxVertex* v = &grid[q * 4];
                v[0].x = x;       v[0].y = y;       v[0].z = 0; v[0].u = 0; v[0].v = 0; v[0].color = 0xFFFFFFFF;
                v[1].x = x + 48;  v[1].y = y;       v[1].z = 0; v[1].u = 1; v[1].v = 0; v[1].color = 0xFFFFFFFF;
                v[2].x = x;       v[2].y = y + 48;  v[2].z = 0; v[2].u = 0; v[2].v = 1; v[2].color = 0xFFFFFFFF;
                v[3].x = x + 48;  v[3].y = y + 48;  v[3].z = 0; v[3].u = 1; v[3].v = 1; v[3].color = 0xFFFFFFFF;
                ++q;
            }
        }
        gfx->DrawQuads(tile, grid, q);

        // 2) rotating sprite about its centre (HGE Gfx_SetTransform semantics)
        const float cx = (float)intW * 0.5f;
        const float cy = (float)intH * 0.5f;
        // HGE semantics: vertices are already in screen space, (x,y) is the
        // pivot they are rotated/scaled about. Not a local space origin.
        gfx->SetTransform(cx, cy, 0, 0, angle, 1, 1);
        drawQuad(gfx, sprite, cx - 48, cy - 48, cx + 48, cy + 48, 0, 0, 1, 1, 0xFFFFFFFF);
        gfx->SetTransform(0, 0, 0, 0, 0, 1, 1);

        // 3) scissor probe (selftest only)
        if (probe) {
            gfx->SetScissor(0, 0, 120, 120);
            drawQuad(gfx, nullptr, 0, 0, (float)intW, (float)intH, 0, 0, 0, 0, probeCol);
            gfx->SetScissor(0, 0, 0, 0);
        }

        gfx->EndFrame();

        if (selftest && lastFrame) {
            u32 c[4] = {0, 0, 0, 0};
            bool ok = true;
            if (!gfx->ReadPixels(60, 60, 1, 1, &c[0]))  { printf("readback failed\n"); ok = false; }
            if (!gfx->ReadPixels(300, 538, 1, 1, &c[1])) ok = false;
            if (!gfx->ReadPixels((int)cx, (int)cy, 1, 1, &c[2])) ok = false;
            if (!gfx->ReadPixels(200, 200, 1, 1, &c[3])) ok = false;

            printf("[m1] px(60,60)   = %08X  want ~CC2020 (scissor inside)\n", c[0]);
            printf("[m1] px(300,538) = %08X  want ~101018 (scissor clipped)\n", c[1]);
            printf("[m1] px(centre)  = %08X  want ~F0E0A0 (sprite fill)\n", c[2]);
            printf("[m1] px(200,200) = %08X  want tile, not background\n", c[3]);

            if (!near(c[0], probeCol, 3))  { printf("FAIL: scissor did not clip in\n"); ok = false; }
            if (!near(c[1], bg, 3))        { printf("FAIL: scissor did not clip out\n"); ok = false; }
            if (!near(c[2], 0xFFF0E0A0, 12)) { printf("FAIL: sprite not at centre\n"); ok = false; }
            if (near(c[3], bg, 3) || near(c[3], probeCol, 3)) { printf("FAIL: tile grid missing\n"); ok = false; }

            printf(ok ? "[m1] SELFTEST PASS\n" : "[m1] SELFTEST FAIL\n");
            plat::SwapBuffers();
            gfx->Shutdown();
            delete gfx;
            plat::Shutdown();
            return ok ? 0 : 1;
        }

        plat::SwapBuffers();

        ++frame;
        angle += 1.5f * dt;
        if (frames > 0 && frame >= frames) break;

        const double t = plat::Time();
        if (t - lastTitle > 0.5) {
            char title[128];
            snprintf(title, sizeof(title), "HGE-SDL M1  %.1f fps  %dx%d",
                     frame / (t > 0 ? t : 1.0), intW, intH);
            plat::SetTitle(title);
            lastTitle = t;
        }
    }

    gfx->Shutdown();
    delete gfx;
    plat::Shutdown();
    return 0;
}
