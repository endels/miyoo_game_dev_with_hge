#include "app/bubble/render.h"

#include "app/bubble/game.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace bbl;

namespace bbl {
namespace {

const int WIN_W = kWinW;
const int WIN_H = kWinH;

const float kCenterX = 406.0f;      // 406 .. 554
const float kCenterW = 148.0f;

const int FONT_CW = 20;
const int FONT_CH = 26;
const int FONT_COLS = 16;
const int FONT_W = FONT_CW * FONT_COLS;
const int FONT_H = FONT_CH * 6;

// Cannon barrel is pre-rotated at load into one spritesheet. Each cell stores
// only the rotated bounding box (not a box around the pivot), so no part is
// clipped when the barrel points left/right.
const int kBarrelFrames = 96;
const int kBarrelCols = 12;
const int kBarrelRows = 8;
const int kBarrelCell = 53;
const int kBarrelSheetW = kBarrelCols * kBarrelCell;
const int kBarrelSheetH = kBarrelRows * kBarrelCell;

// Rotated bounds relative to the bottom-centre pivot.
void barrelBounds(float a, float w, float h,
                  float* bx0, float* by0, float* bx1, float* by1)
{
    const float hw = w * 0.5f;
    const float xs[4] = { -hw, hw, -hw, hw };
    const float ys[4] = { -h, -h, 0.0f, 0.0f };
    const float ca = cosf(a), sa = sinf(a);
    *bx0 = *by0 = 1.0e30f;
    *bx1 = *by1 = -1.0e30f;
    for (int i = 0; i < 4; ++i) {
        const float x = ca * xs[i] - sa * ys[i];
        const float y = sa * xs[i] + ca * ys[i];
        if (x < *bx0) *bx0 = x;
        if (x > *bx1) *bx1 = x;
        if (y < *by0) *by0 = y;
        if (y > *by1) *by1 = y;
    }
}

IGfxTexture* buildBarrelSheet(IGfx* g, const Image* img)
{
    if (!g || !img || img->w <= 0 || img->h <= 0) return nullptr;

    std::vector<u32> sheet((size_t)kBarrelSheetW * kBarrelSheetH, 0);
    const float srcPivotX = img->w * 0.5f;
    const float srcPivotY = (float)img->h;

    for (int f = 0; f < kBarrelFrames; ++f) {
        const float a = -1.5707964f + 3.14159265f * (float)f /
                                          (float)(kBarrelFrames - 1);
        const float ca = cosf(a), sa = sinf(a);
        float bx0, by0, bx1, by1;
        barrelBounds(a, (float)img->w, (float)img->h, &bx0, &by0, &bx1, &by1);
        const int ox = (f % kBarrelCols) * kBarrelCell;
        const int oy = (f / kBarrelCols) * kBarrelCell;

        for (int dy = 0; dy < kBarrelCell; ++dy) {
            const float py = by0 + (float)dy + 0.5f;
            u32* row = &sheet[(size_t)(oy + dy) * kBarrelSheetW + ox];
            for (int dx = 0; dx < kBarrelCell; ++dx) {
                const float px = bx0 + (float)dx + 0.5f;
                const float sxf = ca * px + sa * py + srcPivotX;
                const float syf = -sa * px + ca * py + srcPivotY;
                if (sxf < 0.0f || syf < 0.0f ||
                    sxf >= (float)img->w || syf >= (float)img->h) {
                    continue;
                }
                const int ix = (int)sxf;
                const int iy = (int)syf;
                row[dx] = img->px[(size_t)iy * img->w + ix];
            }
        }
    }

    return g->CreateTexture(kBarrelSheetW, kBarrelSheetH, &sheet[0], kBarrelSheetW * 4);
}

IGfxTexture* upload(IGfx* g, const Image* img)
{
    if (!img) return nullptr;
    return g->CreateTexture(img->w, img->h, &img->px[0], img->w * 4);
}

void quad(IGfx* g, IGfxTexture* t, float x, float y, float w, float h, u32 c)
{
    GfxVertex v[4];
    v[0].x = x;     v[0].y = y;     v[0].z = 0; v[0].color = c; v[0].u = 0; v[0].v = 0;
    v[1].x = x + w; v[1].y = y;     v[1].z = 0; v[1].color = c; v[1].u = 1; v[1].v = 0;
    v[2].x = x;     v[2].y = y + h; v[2].z = 0; v[2].color = c; v[2].u = 0; v[2].v = 1;
    v[3].x = x + w; v[3].y = y + h; v[3].z = 0; v[3].color = c; v[3].u = 1; v[3].v = 1;
    g->DrawQuads(t, v, 1);
}

void quadUV(IGfx* g, IGfxTexture* t, float x, float y, float w, float h,
            float u0, float v0, float u1, float v1, u32 c)
{
    GfxVertex v[4];
    v[0].x = x;     v[0].y = y;     v[0].z = 0; v[0].color = c; v[0].u = u0; v[0].v = v0;
    v[1].x = x + w; v[1].y = y;     v[1].z = 0; v[1].color = c; v[1].u = u1; v[1].v = v0;
    v[2].x = x;     v[2].y = y + h; v[2].z = 0; v[2].color = c; v[2].u = u0; v[2].v = v1;
    v[3].x = x + w; v[3].y = y + h; v[3].z = 0; v[3].color = c; v[3].u = u1; v[3].v = v1;
    g->DrawQuads(t, v, 1);
}

void quadFlip(IGfx* g, IGfxTexture* t, float x, float y, float w, float h,
              u32 c, bool flip)
{
    const float u0 = flip ? 1.0f : 0.0f;
    const float u1 = flip ? 0.0f : 1.0f;
    GfxVertex v[4];
    v[0].x = x;     v[0].y = y;     v[0].z = 0; v[0].color = c; v[0].u = u0; v[0].v = 0;
    v[1].x = x + w; v[1].y = y;     v[1].z = 0; v[1].color = c; v[1].u = u1; v[1].v = 0;
    v[2].x = x;     v[2].y = y + h; v[2].z = 0; v[2].color = c; v[2].u = u0; v[2].v = 1;
    v[3].x = x + w; v[3].y = y + h; v[3].z = 0; v[3].color = c; v[3].u = u1; v[3].v = 1;
    g->DrawQuads(t, v, 1);
}

void text(RenderCtx& c, float x, float y, const char* s, float scale, u32 col)
{
    int si = (int)(scale + 0.5f);
    if (si < 1) si = 1;
    const float adv = 11.0f * si;
    const float cw = (float)FONT_CW * si;
    const float chh = (float)FONT_CH * si;
    for (; *s; ++s) {
        const int ch = (unsigned char)*s;
        if (ch < 32 || ch > 127) { x += adv; continue; }
        const int idx = ch - 32;
        const int col_i = idx % FONT_COLS;
        const int row_i = idx / FONT_COLS;
        const float u0 = (col_i * FONT_CW) / (float)FONT_W;
        const float v0 = (row_i * FONT_CH) / (float)FONT_H;
        const float u1 = u0 + FONT_CW / (float)FONT_W;
        const float v1 = v0 + FONT_CH / (float)FONT_H;
        quadUV(c.g, c.font == nullptr ? nullptr : c.font, x, y, cw, chh, u0, v0, u1, v1, col);
        x += adv;
    }
}

void centreText(RenderCtx& c, float cx, float y, const char* s, float scale, u32 col)
{
    int si = (int)(scale + 0.5f);
    if (si < 1) si = 1;
    const float w = (float)strlen(s) * 11.0f * si;
    text(c, cx - w * 0.5f, y, s, (float)si, col);
}

// The strip has sweepFrames sweep frames followed by idle frames.
int waveFrameFor(float front, float x, float halfW, int frames, int sweep)
{
    if (front < -1.0e8f) return frames - 1;
    const float u = (front - (x - halfW)) / (2.0f * halfW);
    if (u < 0.0f || u > 1.0f) return frames - 1;
    int f = (int)(u * (float)sweep);
    return f >= sweep ? sweep - 1 : f;
}

void drawDino(RenderCtx& c, IGfx* g, int variant, float cx, float feetY, bool flip,
              const Game& game, float time);

void drawBoard(RenderCtx& c, Game& game, float time, bool showAim, int variant, bool dinoOnLeft)
{
    IGfx* g = c.g;
    const float x0 = game.X0();
    const float fw = game.FieldW();

    // playfield veil + frame
    quad(g, nullptr, x0, 0, fw, WIN_H, 0x50060910);
    quad(g, nullptr, x0, 0, fw, 8, 0xFF2A3648);
    quad(g, nullptr, x0, 8, fw, 2, 0xFF6E86A6);
    quad(g, nullptr, x0, 0, 2, WIN_H, 0xFF33415A);
    quad(g, nullptr, x0 + fw - 2, 0, 2, WIN_H, 0xFF33415A);

    // death line
    for (float x = x0 + 8; x < x0 + fw - 20; x += 26)
        quad(g, nullptr, x, kDeathY, 12, 2, 0x99E05A5A);

    // light band sweeping this board
    const int   kFrames = 16;
    const int   kSweep = 6;
    const float kWaveEvery = 4.0f;
    const float kWaveLen = 0.75f;
    const float kWaveWidth = 150.0f;
    float wt = time / kWaveEvery;
    wt -= (float)(int)wt;
    const float duty = kWaveLen / kWaveEvery;
    float front = -1.0e9f;
    if (wt < duty)
        front = x0 - kWaveWidth * 0.5f + (wt / duty) * (fw + kWaveWidth);

    for (int r = 0; r < game.RowsUsed(); ++r) {
        const int n = game.RowCount(r);
        for (int cc = 0; cc < n; ++cc) {
            const int v = game.Cell(r, cc);
            if (!v) continue;
            const float x = game.XOf(r, cc);
            const int fr = waveFrameFor(front, x, kWaveWidth * 0.5f, kFrames, kSweep);
            const float u0 = fr / (float)kFrames;
            const float u1 = (fr + 1) / (float)kFrames;
            quadUV(g, c.bubblesAnim[(v - 1) % kColors], x - kR, game.YOfLive(r) - kR,
                   kD, kD, u0, 0.0f, u1, 1.0f, 0xFFFFFFFF);
        }
    }

    // garbage rising from the bottom
    const std::vector<Riser>& risers = game.Risers();
    for (size_t i = 0; i < risers.size(); ++i)
        quad(g, c.bubbles[(risers[i].color - 1) % kColors],
             risers[i].x - kR, risers[i].y - kR, kD, kD, 0xFFFFFFFF);

    // dropped bubbles
    const std::vector<Falling>& fall = game.FallingBubbles();
    for (size_t i = 0; i < fall.size(); ++i)
        quad(g, c.bubbles[(fall[i].color - 1) % kColors],
             fall[i].x - kR, fall[i].y - kR, kD, kD, 0xFFFFFFFF);

    // aim guide with one wall bounce (player only)
    if (showAim && game.GetState() == Game::AIM) {
        const float ang = game.AimAngle();
        float dx = cosf(ang), dy = sinf(ang);
        float x = game.ShooterX(), y = kShooterY;
        for (int i = 0; i < 40; ++i) {
            x += dx * 16.0f;
            y += dy * 16.0f;
            if (y < 14.0f) break;
            if (x < x0 + kR) { x = x0 + kR; dx = -dx; }
            if (x > x0 + fw - kR) { x = x0 + fw - kR; dx = -dx; }
            if (game.SolidAt(x, y)) break;
            if ((i & 1) == 0)
                quad(g, c.dot, x - 3, y - 3, 6, 6, 0x88FFFFFF);
        }
    }

    // cannon (pre-rotated frame sheet)
    const float sx = game.ShooterX();
    {
        const float rot = game.AimAngle() + 1.5707964f;
        int fi = (int)lroundf((rot + 1.5707964f) / 3.14159265f *
                              (float)(kBarrelFrames - 1));
        if (fi < 0) fi = 0;
        else if (fi >= kBarrelFrames) fi = kBarrelFrames - 1;
        const int fx = (fi % kBarrelCols) * kBarrelCell;
        const int fy = (fi / kBarrelCols) * kBarrelCell;
        const float u0 = (float)fx / (float)kBarrelSheetW;
        const float v0 = (float)fy / (float)kBarrelSheetH;
        const float u1 = (float)(fx + kBarrelCell) / (float)kBarrelSheetW;
        const float v1 = (float)(fy + kBarrelCell) / (float)kBarrelSheetH;
        float bx0, by0, bx1, by1;
        barrelBounds(rot, c.barrelSrcW, c.barrelSrcH, &bx0, &by0, &bx1, &by1);
        quadUV(g, c.cannonBarrelFrames,
               sx + bx0, kShooterY + by0,
               (float)kBarrelCell, (float)kBarrelCell,
               u0, v0, u1, v1, 0xFFFFFFFF);
    }

    quad(g, c.dot, sx - 46, kShooterY - 46, 92, 92, 0x18A0C0FF);
    quad(g, c.cannonBase, sx - 32, kShooterY - 32, 64, 64, 0xFFFFFFFF);

    // loaded bubble
    {
        const int fr = waveFrameFor(front, sx, kWaveWidth * 0.5f, kFrames, kSweep);
        const float u0 = fr / (float)kFrames;
        const float u1 = (fr + 1) / (float)kFrames;
        quadUV(g, c.bubblesAnim[(game.CurrentColor() - 1) % kColors],
               sx - kR, kShooterY - kR, kD, kD, u0, 0.0f, u1, 1.0f, 0xFFFFFFFF);
    }

    if (game.IsFlying())
        quad(g, c.bubbles[(game.ShotColor() - 1) % kColors],
             game.ShotX() - kR, game.ShotY() - kR, kD, kD, 0xFFFFFFFF);

    // next bubble, opposite side of the dino
    {
        const float nx = dinoOnLeft ? sx + 62.0f : sx - 94.0f;
        quad(g, nullptr, nx, kShooterY - 22, 44, 44, 0x50000000);
        quad(g, c.bubbles[(game.NextColor() - 1) % kColors],
             nx + 2, kShooterY - 20, 40, 40, 0xB0FFFFFF);
    }

    drawDino(c, g, variant, dinoOnLeft ? sx - 84.0f : sx + 84.0f, 530.0f,
             dinoOnLeft, game, time);

    // particles
    const std::vector<Particle>& ps = game.Particles();
    for (size_t i = 0; i < ps.size(); ++i) {
        const Particle& p = ps[i];
        const float k = p.life > 0 ? p.t / p.life : 1.0f;
        if (k < 0.0f || k >= 1.0f) continue;
        if (p.kind == 0) {
            const float s = 16.0f * p.scale * (1.0f - 0.35f * k);
            const u32 a = (u32)((1.0f - k) * 235.0f);
            quad(g, c.dot, p.x - s * 0.5f, p.y - s * 0.5f, s, s, (a << 24) | 0x00FFFFFF);
        } else {
            const float s = 64.0f * p.scale * (0.35f + k * 0.9f);
            const u32 a = (u32)((1.0f - k) * 190.0f);
            quad(g, c.ring, p.x - s * 0.5f, p.y - s * 0.5f, s, s,
                 (a << 24) | (p.color & 0x00FFFFFF));
        }
    }
}

void drawDino(RenderCtx& c, IGfx* g, int v, float cx, float feetY, bool flip,
              const Game& game, float time)
{
    if (!c.dinoIdle[v][0]) return;

    const float DS = 2.0f;
    const float DW = 40.0f * DS, DH = 40.0f * DS;
    IGfxTexture* t = c.dinoIdle[v][0];

    const float blinkT = (v == 0) ? 2.4f : 3.1f;
    const float phase = (v == 0) ? 0.0f : 1.35f;
    const float ph = fmodf(time + phase, blinkT);
    const float blinkAt = blinkT - 0.42f;
    const float glance = fmodf(time * 0.7f + phase * 3.1f, 9.0f);
    const bool  bored = game.IdleTime() > 6.0f;
    const bool  combo = game.LastPops() >= 5 && game.SinceResolve() < 0.9f;
    const float peek = fmodf(time * 0.18f + phase * 1.7f, 1.0f);

    if (game.GetState() == Game::WIN) {
        if (c.dinoCheer[v]) t = c.dinoCheer[v];
    } else if (game.GetState() == Game::LOSE) {
        const bool sadNow = fmodf(time + phase, 2.2f) < 1.3f;
        if (sadNow && c.dinoSad[v]) t = c.dinoSad[v];
        else if (!sadNow && c.dinoLook[v]) t = c.dinoLook[v];
    } else if (combo) {
        if (c.dinoShoot[v]) t = c.dinoShoot[v];
    } else if (game.IsFlying()) {
        if (c.dinoShoot[v]) t = c.dinoShoot[v];
    } else if (bored && peek > 0.88f) {
        if (c.dinoCheer[v]) t = c.dinoCheer[v];
    } else if (glance > (bored ? 6.8f : 8.45f)) {
        if (c.dinoLook[v]) t = c.dinoLook[v];
    } else if (ph > blinkAt) {
        const float u = (ph - blinkAt) / 0.42f;
        if (u < 0.35f || u > 0.72f) {
            if (c.dinoIdle[v][1]) t = c.dinoIdle[v][1];
        } else if (c.dinoIdle[v][2]) {
            t = c.dinoIdle[v][2];
        }
    }
    quadFlip(g, t, cx - DW * 0.5f, feetY - DH, DW, DH, 0xFFFFFFFF, flip);
}

} // namespace

bool LoadTextures(RenderCtx& c, IGfx* gfx, const Pack& pack, std::string* err)
{
    c.g = gfx;

    const Image* img;
    c.bg = upload(c.g, pack.Find("bg"));
    for (int i = 0; i < kColors; ++i) {
        char n[32];
        snprintf(n, sizeof(n), "bubble%d", i);
        c.bubbles[i] = upload(c.g, pack.Find(n));
        snprintf(n, sizeof(n), "bubble%d_big", i);
        c.bubblesBig[i] = upload(c.g, pack.Find(n));
        snprintf(n, sizeof(n), "bubble%d_anim", i);
        c.bubblesAnim[i] = upload(c.g, pack.Find(n));
        if (!c.bubbles[i] || !c.bubblesBig[i] || !c.bubblesAnim[i]) {
            if (err) *err = "game.pak is corrupt (bubble sprites missing).";
            return false;
        }
    }
    if ((img = pack.Find("cannon_base")) != nullptr) c.cannonBase = upload(c.g, img);
    if ((img = pack.Find("cannon_barrel")) != nullptr) {
        c.barrelSrcW = (float)img->w;
        c.barrelSrcH = (float)img->h;
        c.cannonBarrelFrames = buildBarrelSheet(c.g, img);
    }
    if ((img = pack.Find("dot")) != nullptr) c.dot = upload(c.g, img);
    if ((img = pack.Find("ring")) != nullptr) c.ring = upload(c.g, img);
    if ((img = pack.Find("wave")) != nullptr) c.wave = upload(c.g, img);
    if ((img = pack.Find("font")) != nullptr) c.font = upload(c.g, img);
    for (int v = 0; v < 2; ++v) {
        char n[32];
        for (int i = 0; i < 3; ++i) {
            snprintf(n, sizeof(n), "bub%d_idle%d", v, i);
            if ((img = pack.Find(n)) != nullptr)
                c.dinoIdle[v][i] = upload(c.g, img);
        }
        snprintf(n, sizeof(n), "bub%d_shoot", v);
        if ((img = pack.Find(n)) != nullptr) c.dinoShoot[v] = upload(c.g, img);
        snprintf(n, sizeof(n), "bub%d_cheer", v);
        if ((img = pack.Find(n)) != nullptr) c.dinoCheer[v] = upload(c.g, img);
        snprintf(n, sizeof(n), "bub%d_sad", v);
        if ((img = pack.Find(n)) != nullptr) c.dinoSad[v] = upload(c.g, img);
        snprintf(n, sizeof(n), "bub%d_look", v);
        if ((img = pack.Find(n)) != nullptr) c.dinoLook[v] = upload(c.g, img);
    }
    if (!c.bg || !c.cannonBase || !c.cannonBarrelFrames || !c.dot || !c.ring || !c.font) {
        if (err) *err = "game.pak is corrupt or from an older build.";
        return false;
    }
    return true;
}

void FreeTextures(RenderCtx& c)
{
    IGfx* g = c.g;
    if (g) {
        if (c.bg) g->ReleaseTexture(c.bg);
        for (int i = 0; i < kColors; ++i) {
            if (c.bubbles[i])      g->ReleaseTexture(c.bubbles[i]);
            if (c.bubblesBig[i])   g->ReleaseTexture(c.bubblesBig[i]);
            if (c.bubblesAnim[i])  g->ReleaseTexture(c.bubblesAnim[i]);
        }
        if (c.cannonBase)         g->ReleaseTexture(c.cannonBase);
        if (c.cannonBarrelFrames) g->ReleaseTexture(c.cannonBarrelFrames);
        if (c.dot)          g->ReleaseTexture(c.dot);
        if (c.ring)         g->ReleaseTexture(c.ring);
        if (c.wave)         g->ReleaseTexture(c.wave);
        if (c.font)         g->ReleaseTexture(c.font);
        for (int v = 0; v < 2; ++v) {
            for (int i = 0; i < 3; ++i)
                if (c.dinoIdle[v][i]) g->ReleaseTexture(c.dinoIdle[v][i]);
            if (c.dinoShoot[v]) g->ReleaseTexture(c.dinoShoot[v]);
            if (c.dinoCheer[v]) g->ReleaseTexture(c.dinoCheer[v]);
            if (c.dinoSad[v])   g->ReleaseTexture(c.dinoSad[v]);
            if (c.dinoLook[v])  g->ReleaseTexture(c.dinoLook[v]);
        }
    }
    c = RenderCtx();
}

void RenderFrame(RenderCtx& c, Side s[2], double time, float resultTimer)
{
    IGfx* g = c.g;
    g->BeginFrame();
    g->Clear(0xFF080B12);
    g->SetTransform(0, 0, 0, 0, 0, 1, 1);
    g->SetScissor(0, 0, 0, 0);
    g->SetBlend(GFX_BLEND_ALPHA);

    quad(g, c.bg, 0, 0, WIN_W, WIN_H, 0xFFFFFFFF);

    drawBoard(c, s[0].game, (float)time, true, 0, true);
    drawBoard(c, s[1].game, (float)time, false, 1, false);

    // ---- centre column ------------------------------------------------
    quad(g, nullptr, kCenterX, 0, 1, WIN_H, 0xFF2A3648);
    quad(g, nullptr, kCenterX + kCenterW - 1, 0, 1, WIN_H, 0xFF2A3648);
    quad(g, nullptr, kCenterX, 0, kCenterW, WIN_H, 0x40060910);

    centreText(c, kCenterX + kCenterW * 0.5f, 16, "VS", 2.0f, 0xFFEAF0FA);

    struct BarInfo { float x; const char* label; u32 col; };
    const BarInfo bars[2] = {
        { kCenterX + 18.0f, "YOU", 0xFFFFD24A },
        { kCenterX + 92.0f, "CPU", 0xFF6FA8FF },
    };
    const float barW = 28.0f, barTop = 120.0f, barH = 250.0f;

    for (int i = 0; i < 2; ++i) {
        const float x = bars[i].x;
        quad(g, nullptr, x, barTop, barW, barH, 0x70000000);
        quad(g, nullptr, x, barTop, barW, 1, 0xFF3A4A62);
        quad(g, nullptr, x, barTop + barH - 1, barW, 1, 0xFF3A4A62);
        quad(g, nullptr, x, barTop, 1, barH, 0xFF3A4A62);
        quad(g, nullptr, x + barW - 1, barTop, 1, barH, 0xFF3A4A62);

        float fill = s[i].energy / (float)kEnergyPerWave;
        if (fill > 1.0f) fill = 1.0f;
        if (fill > 0.0f)
            quad(g, nullptr, x + 3, barTop + barH - 3 - (barH - 6) * fill,
                 barW - 6, (barH - 6) * fill, bars[i].col);

        // incoming garbage warning
        const int inc = (int)s[i].game.Risers().size();
        if (inc > 0) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", inc);
            centreText(c, x + barW * 0.5f, barTop + barH + 8, buf, 1.0f, 0xFFFF9A6A);
        }
    }
    centreText(c, kCenterX + 32.0f, 392, "YOU", 1.0f, 0xFFEAF0FA);
    centreText(c, kCenterX + 106.0f, 392, "CPU", 1.0f, 0xFFEAF0FA);

    centreText(c, kCenterX + kCenterW * 0.5f, 448, "CLEAR", 1.0f, 0xFF8FA6C4);
    centreText(c, kCenterX + kCenterW * 0.5f, 464, "TO ATTACK", 1.0f, 0xFF8FA6C4);

    // ---- result -------------------------------------------------------
    const bool playerDead = s[0].game.GetState() == Game::LOSE;
    const bool aiDead = s[1].game.GetState() == Game::LOSE;
    if (playerDead || aiDead) {
        quad(g, nullptr, 0, 0, WIN_W, WIN_H, 0xB4000000);
        const bool win = aiDead && !playerDead;
        centreText(c, WIN_W * 0.5f, 200, win ? "YOU WIN" : "YOU LOSE", 3.0f,
                   win ? 0xFFFFE08A : 0xFFFF8A8A);
        centreText(c, WIN_W * 0.5f, 320,
                   resultTimer > 0.0f ? "RESTARTING..." : "PRESS R TO RESTART",
                   1.0f, 0xFF9FB3CE);
    }
}

} // namespace bbl
