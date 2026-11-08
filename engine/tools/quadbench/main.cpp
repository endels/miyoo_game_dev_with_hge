// quadbench — renderer throughput probe.
//
// Draws N textured quads per frame in several scenarios and reports the cost of
// each stage separately:
//
//   draw_avg     BeginFrame + Clear + submit + EndFrame (submission + rasterize)
//   present_avg  SwapBuffers (framebuffer/MMA present)
//   frame_avg    draw + present  ->  the fps the machine could sustain
//
// It drives plat:: directly instead of hge::Run so that frame pacing and vsync
// cannot mask the real cost, and so each stage can be timed in isolation.
//
// This is an engine-internal tool: it includes platform/plat.h, which games are
// not allowed to see.
//
// Args:
//   --quads 64,128,256,...   quad counts to sweep   (default 64..2048)
//   --frames N               frames per phase       (default 90)
//   --scenarios a,b,...      subset of plain,onebyone,rotated,scissor,add,mixed
//   --size WxH               logical size           (default 960x540)
//
// HGE_OFFSCREEN=1 runs headless (in-memory surface, no /dev/fb0 writes) which is
// the safe way to bench on a device whose menu is still running.

#include "hge_gfx.h"
#include "hge_system.h"
#include "platform/plat.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

const int kTexSize = 32;
const int kWarmupFrames = 8;

enum Kind { kPlain, kOneByOne, kRotated, kScissor, kAdd, kMixed };

struct Scenario {
    const char* name;
    Kind        kind;
};

const Scenario kScenarios[] = {
    {"plain",    kPlain},     // one DrawQuads call, alpha blend
    {"onebyone", kOneByOne},  // one DrawQuads call per quad
    {"rotated",  kRotated},   // per-quad SetTransform + one call
    {"scissor",  kScissor},   // per-quad SetScissor + one call
    {"add",      kAdd},       // additive blend, one call
    {"mixed",    kMixed},     // 80% plain tiles + 20% rotated sprites
};

u32 g_pixels[kTexSize * kTexSize];

void buildTexture()
{
    for (int y = 0; y < kTexSize; ++y) {
        for (int x = 0; x < kTexSize; ++x) {
            const u32 r = (u32)(x * 8) & 0xFFu;
            const u32 g = (u32)(y * 8) & 0xFFu;
            const u32 b = (u32)((x * 3 + y * 5) & 0xFF);
            // Semi-transparent edges and centre keep the alpha-blend path hot.
            const u32 a = ((x + y) % 7 == 0) ? 0x60u : 0xE0u;
            g_pixels[y * kTexSize + x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
}

void quadVerts(GfxVertex* v, float x, float y, float w, float h)
{
    v[0] = {x,     y,     0.0f, 0xFFFFFFFFu, 0.0f, 0.0f};
    v[1] = {x + w, y,     0.0f, 0xFFFFFFFFu, 1.0f, 0.0f};
    v[2] = {x,     y + h, 0.0f, 0xFFFFFFFFu, 0.0f, 1.0f};
    v[3] = {x + w, y + h, 0.0f, 0xFFFFFFFFu, 1.0f, 1.0f};
}

// Lay n quads out on a grid that covers the whole screen, so large counts mean
// real overdraw rather than off-screen clipping.
void layout(std::vector<GfxVertex>& verts, int n, int w, int h)
{
    verts.resize((size_t)n * 4);
    const float qw = 32.0f, qh = 32.0f;
    const int cols = std::max(1, w / (int)qw);
    for (int i = 0; i < n; ++i) {
        const int cx = i % cols;
        const int cy = (i / cols) % std::max(1, h / (int)qh);
        quadVerts(&verts[(size_t)i * 4], (float)(cx * (int)qw), (float)(cy * (int)qh), qw, qh);
    }
}

void drawScenario(IGfx* gfx, IGfxTexture* tex, const std::vector<GfxVertex>& verts, int n, Kind kind)
{
    switch (kind) {
    case kPlain:
        gfx->SetBlend(GFX_BLEND_ALPHA);
        gfx->DrawQuads(tex, &verts[0], n);
        break;
    case kOneByOne:
        gfx->SetBlend(GFX_BLEND_ALPHA);
        for (int i = 0; i < n; ++i) gfx->DrawQuads(tex, &verts[(size_t)i * 4], 1);
        break;
    case kRotated:
        gfx->SetBlend(GFX_BLEND_ALPHA);
        for (int i = 0; i < n; ++i) {
            const GfxVertex* q = &verts[(size_t)i * 4];
            const float cx = q[0].x + 16.0f, cy = q[0].y + 16.0f;
            gfx->SetTransform(cx, cy, 0.0f, 0.0f, (float)((i % 16) * 22), 1.0f, 1.0f);
            gfx->DrawQuads(tex, q, 1);
        }
        gfx->SetTransform(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f);
        break;
    case kScissor:
        gfx->SetBlend(GFX_BLEND_ALPHA);
        for (int i = 0; i < n; ++i) {
            const GfxVertex* q = &verts[(size_t)i * 4];
            gfx->SetScissor((int)q[0].x, (int)q[0].y, 32, 32);
            gfx->DrawQuads(tex, q, 1);
        }
        gfx->SetScissor(0, 0, 0, 0);
        break;
    case kAdd:
        gfx->SetBlend(GFX_BLEND_ADD);
        gfx->DrawQuads(tex, &verts[0], n);
        gfx->SetBlend(GFX_BLEND_ALPHA);
        break;
    case kMixed:
    default: {
        const int sprites = n / 5;
        const int tiles = n - sprites;
        if (tiles > 0) {
            gfx->SetBlend(GFX_BLEND_ALPHA);
            gfx->DrawQuads(tex, &verts[0], tiles);
        }
        for (int i = 0; i < sprites; ++i) {
            const GfxVertex* q = &verts[(size_t)(tiles + i) * 4];
            const float cx = q[0].x + 16.0f, cy = q[0].y + 16.0f;
            gfx->SetTransform(cx, cy, 0.0f, 0.0f, (float)((i % 12) * 30), 1.0f, 1.0f);
            gfx->DrawQuads(tex, q, 1);
        }
        gfx->SetTransform(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f);
        break;
    }
    }
}

struct Row {
    const char* scenario;
    int         quads;
    double      drawAvg, drawP50, drawWorst;
    double      presentAvg;
    double      frameAvg;
};

double median(std::vector<double>& v)
{
    if (v.empty()) return 0.0;
    const size_t mid = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + (long)mid, v.end());
    return v[mid];
}

}  // namespace

int main(int argc, char** argv)
{
    std::vector<int> counts = {64, 128, 256, 384, 512, 768, 1024, 1536, 2048};
    int frames = 90;
    int width = 960, height = 540;
    std::vector<Scenario> scenarios;

    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (std::strcmp(a, "--frames") == 0 && i + 1 < argc) {
            frames = std::atoi(argv[++i]);
        } else if (std::strcmp(a, "--size") == 0 && i + 1 < argc) {
            if (std::sscanf(argv[++i], "%dx%d", &width, &height) != 2) {
                std::fprintf(stderr, "quadbench: bad --size\n");
                return 2;
            }
        } else if (std::strcmp(a, "--quads") == 0 && i + 1 < argc) {
            counts.clear();
            char* p = argv[++i];
            char* end = nullptr;
            while (*p) {
                const long v = std::strtol(p, &end, 10);
                if (end == p || v <= 0 || v > 100000) {
                    std::fprintf(stderr, "quadbench: bad --quads\n");
                    return 2;
                }
                counts.push_back((int)v);
                p = (*end == ',') ? end + 1 : end;
            }
        } else if (std::strcmp(a, "--scenarios") == 0 && i + 1 < argc) {
            char* p = argv[++i];
            while (*p) {
                char* comma = std::strchr(p, ',');
                const size_t len = comma ? (size_t)(comma - p) : std::strlen(p);
                for (const Scenario& s : kScenarios) {
                    if (std::strlen(s.name) == len && std::strncmp(s.name, p, len) == 0)
                        scenarios.push_back(s);
                }
                if (!comma) break;
                p = comma + 1;
            }
            if (scenarios.empty()) {
                std::fprintf(stderr, "quadbench: no known scenario in --scenarios\n");
                return 2;
            }
        } else {
            std::fprintf(stderr, "quadbench: unknown argument %s\n", a);
            return 2;
        }
    }
    if (scenarios.empty())
        for (const Scenario& s : kScenarios) scenarios.push_back(s);
    if (counts.empty()) counts.push_back(256);

    if (!plat::Init(width, height, "quadbench", false, false)) {
        std::fprintf(stderr, "quadbench: plat::Init failed\n");
        return 2;
    }
    IGfx* gfx = hge::CreateGfx();
    if (!gfx || !gfx->Init(plat::Window(), plat::GLGetProc)) {
        std::fprintf(stderr, "quadbench: gfx init failed\n");
        plat::Shutdown();
        return 2;
    }

    buildTexture();
    IGfxTexture* tex = gfx->CreateTexture(kTexSize, kTexSize, g_pixels, kTexSize * 4);
    if (!tex) {
        std::fprintf(stderr, "quadbench: CreateTexture failed\n");
        gfx->Shutdown();
        plat::Shutdown();
        return 2;
    }

    const int maxN = *std::max_element(counts.begin(), counts.end());
    std::vector<GfxVertex> verts;
    layout(verts, maxN, width, height);

    std::printf("# quadbench backend=%s screen=%dx%d frames=%d offscreen=%s immediate=%s\n",
                gfx->Info(), width, height, frames,
                std::getenv("HGE_OFFSCREEN") ? std::getenv("HGE_OFFSCREEN") : "0",
                std::getenv("HGE_IMMEDIATE") ? std::getenv("HGE_IMMEDIATE") : "0");
    std::printf("scenario,quads,draw_avg_ms,draw_p50_ms,draw_worst_ms,"
                "present_avg_ms,frame_avg_ms,fps\n");
    std::fflush(stdout);

    std::vector<Row> rows;
    for (const Scenario& sc : scenarios) {
        for (int n : counts) {
            std::vector<double> draws, presents;
            draws.reserve((size_t)frames);
            presents.reserve((size_t)frames);
            double drawSum = 0.0, presentSum = 0.0, worst = 0.0;

            for (int f = 0; f < frames + kWarmupFrames; ++f) {
                const double t0 = hge::Time();
                gfx->BeginFrame();
                gfx->Clear(0xFF101820u);
                drawScenario(gfx, tex, verts, n, sc.kind);
                gfx->EndFrame();
                const double t1 = hge::Time();
                plat::SwapBuffers();
                const double t2 = hge::Time();

                if (f < kWarmupFrames) continue;
                const double d = (t1 - t0) * 1000.0;
                const double p = (t2 - t1) * 1000.0;
                draws.push_back(d);
                presents.push_back(p);
                drawSum += d;
                presentSum += p;
                if (d > worst) worst = d;
            }

            Row r;
            r.scenario = sc.name;
            r.quads = n;
            r.drawAvg = drawSum / frames;
            r.drawP50 = median(draws);
            r.drawWorst = worst;
            r.presentAvg = presentSum / frames;
            r.frameAvg = r.drawAvg + r.presentAvg;
            rows.push_back(r);

            std::printf("%s,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.1f\n",
                        r.scenario, r.quads, r.drawAvg, r.drawP50, r.drawWorst,
                        r.presentAvg, r.frameAvg,
                        r.frameAvg > 0.0 ? 1000.0 / r.frameAvg : 0.0);
            std::fflush(stdout);
        }
    }

    // Largest count per scenario that still fits the 60 fps frame budget.
    std::printf("# budget60: ");
    for (const Scenario& sc : scenarios) {
        int best = 0;
        for (const Row& r : rows) {
            if (std::strcmp(r.scenario, sc.name) == 0 && r.frameAvg <= 1000.0 / 60.0) best = r.quads;
        }
        std::printf("%s=%d ", sc.name, best);
    }
    std::printf("\n");

    gfx->ReleaseTexture(tex);
    gfx->Shutdown();
    plat::Shutdown();
    return 0;
}
