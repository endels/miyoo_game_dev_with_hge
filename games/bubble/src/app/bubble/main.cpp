// Bubble versus — player vs AI on two boards.
//
//   bubble_level1              play (mouse aim, click/space fire, R restart)
//   bubble_level1 --selftest   headless grid/physics invariants, exit 0/1
//   bubble_level1 --shot a.ppm render one frame to a PPM and exit

#include "platform/plat.h"
#include "hge_gfx.h"
#include "hge_resource.h"
#include "app/bubble/game.h"
#include "hge_audio.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace bbl;

namespace {

const int   WIN_W = 960;
const int   WIN_H = 540;

const int   kBoardCols = 10;
const float kBoardX[2] = { 6.0f, 554.0f };
const float kCenterX = 406.0f;      // 406 .. 554
const float kCenterW = 148.0f;

const int FONT_CW = 20;
const int FONT_CH = 26;
const int FONT_COLS = 16;
const int FONT_W = FONT_CW * FONT_COLS;
const int FONT_H = FONT_CH * 6;

struct Ctx {
    IGfx* g = nullptr;
    IGfxTexture* bg = nullptr;
    IGfxTexture* bubbles[kColors] = {0};
    IGfxTexture* bubblesBig[kColors] = {0};
    IGfxTexture* bubblesAnim[kColors] = {0};
    IGfxTexture* cannonBase = nullptr;
    IGfxTexture* cannonBarrel = nullptr;
    IGfxTexture* dot = nullptr;
    IGfxTexture* ring = nullptr;
    IGfxTexture* wave = nullptr;
    IGfxTexture* font = nullptr;
    IGfxTexture* dinoIdle[2][3] = {{0}};
    IGfxTexture* dinoShoot[2] = {0};
    IGfxTexture* dinoCheer[2] = {0};
    IGfxTexture* dinoSad[2] = {0};
    IGfxTexture* dinoLook[2] = {0};
};

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

void text(Ctx& c, float x, float y, const char* s, float scale, u32 col)
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

void centreText(Ctx& c, float cx, float y, const char* s, float scale, u32 col)
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

void drawDino(Ctx& c, IGfx* g, int variant, float cx, float feetY, bool flip,
              const Game& game, float time);

void drawBoard(Ctx& c, Game& game, float time, bool showAim, int variant, bool dinoOnLeft)
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

    // cannon
    const float sx = game.ShooterX();
    g->SetTransform(sx, kShooterY, 0, 0, game.AimAngle() + 1.5707964f, 1, 1);
    quad(g, c.cannonBarrel, sx - 10, kShooterY - 46, 20, 46, 0xFFFFFFFF);
    g->SetTransform(0, 0, 0, 0, 0, 1, 1);

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

void drawDino(Ctx& c, IGfx* g, int v, float cx, float feetY, bool flip,
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

// ------------------------------------------------------------------ battle
struct Side {
    Game  game;
    float energy;
    float aiTimer;
    int   lastPops;
    int   lastDrops;
    bool  ai;
};

float rnd01()
{
    static u32 s = 0x9E3779B9u;
    s = s * 1664525u + 1013904223u;
    return (float)((s >> 8) & 0xFFFF) / 65535.0f;
}

void resetBattle(Side s[2])
{
    for (int i = 0; i < 2; ++i) {
        s[i].game.Configure(kBoardCols, kBoardX[i]);
        s[i].game.SetColors(kMaxColors);
        s[i].game.Reset(0x1234u + (u32)i * 777u);
        s[i].energy = 0.0f;
        s[i].aiTimer = 1.2f;
        s[i].lastPops = s[i].game.Pops();
        s[i].lastDrops = s[i].game.Drops();
    }
    s[0].ai = false;
    s[1].ai = true;
}

void aiUpdate(Side& s, float dt)
{
    if (s.game.GetState() != Game::AIM) return;
    s.aiTimer -= dt;
    if (s.aiTimer > 0.0f) return;

    // Score every attachable empty cell: matching neighbours matter most,
    // then prefer higher cells so the stack does not creep downwards.
    // Only consider cells with a clear line of fire: aiming at a buried cell
    // just makes the shot bounce off the outer shell.
    auto reachable = [&](float tx, float ty) {
        const float sx = s.game.ShooterX(), sy = kShooterY;
        const float dx = tx - sx, dy = ty - sy;
        const float dist = sqrtf(dx * dx + dy * dy);
        const int steps = (int)(dist / 8.0f);
        for (int i = 3; i < steps - 2; ++i) {
            const float u = (float)i / (float)steps;
            if (s.game.SolidAt(sx + dx * u, sy + dy * u)) return false;
        }
        return true;
    };

    const int want = s.game.CurrentColor();
    int bestR = -1, bestC = -1, bestScore = -100000;
    for (int r = 0; r <= s.game.RowsUsed() && r < kMaxRows; ++r) {
        const int n = s.game.RowCount(r);
        for (int c = 0; c < n; ++c) {
            if (s.game.Cell(r, c)) continue;
            if (!s.game.Attachable(r, c)) continue;
            if (!reachable(s.game.XOf(r, c), Game::YOf(r))) continue;
            const int same = s.game.SameNeighbors(r, c, want);
            const int score = same * 100 - r * 3 + (int)(rnd01() * 6.0f);
            if (score > bestScore) { bestScore = score; bestR = r; bestC = c; }
        }
    }

    if (bestR < 0) {
        s.game.AimAt(s.game.ShooterX() + (rnd01() - 0.5f) * 40.0f, 120.0f);
        s.game.Fire();
        s.aiTimer = 0.9f + rnd01() * 0.8f;
        return;
    }

    const float tx = s.game.XOf(bestR, bestC) + (rnd01() - 0.5f) * 14.0f;
    s.game.AimAt(tx, Game::YOf(bestR));
    s.game.Fire();
    s.aiTimer = 1.00f + rnd01() * 0.75f;
}

void updateBattle(Side s[2], float dt)
{
    s[0].game.Update(dt);
    s[1].game.Update(dt);

    for (int i = 0; i < 2; ++i) {
        const int dp = s[i].game.Pops() - s[i].lastPops;
        const int dd = s[i].game.Drops() - s[i].lastDrops;
        s[i].lastPops = s[i].game.Pops();
        s[i].lastDrops = s[i].game.Drops();
        if (dp > 0 || dd > 0) {
            // direct pops count half, chain drops count full: dropping is the
            // skilled play, so it should be what charges the attack
            s[i].energy += (float)dp * 0.5f + (float)dd;
            while (s[i].energy >= (float)kEnergyPerWave) {
                s[i].energy -= (float)kEnergyPerWave;
                s[1 - i].game.AddGarbage(kWaveSize);
            }
        }
        if (s[i].game.GetState() == Game::WIN) s[i].game.ResumeIfWon();
    }
}

void render(Ctx& c, Side s[2], float time, float resultTimer)
{
    IGfx* g = c.g;
    g->BeginFrame();
    g->Clear(0xFF080B12);
    g->SetTransform(0, 0, 0, 0, 0, 1, 1);
    g->SetScissor(0, 0, 0, 0);
    g->SetBlend(GFX_BLEND_ALPHA);

    quad(g, c.bg, 0, 0, WIN_W, WIN_H, 0xFFFFFFFF);

    drawBoard(c, s[0].game, time, true, 0, true);
    drawBoard(c, s[1].game, time, false, 1, false);

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

bool writePPM(const char* path, const u32* px, int w, int h)
{
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    std::vector<u8> row((size_t)w * 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const u32 c = px[(size_t)y * w + x];
            row[x * 3 + 0] = (u8)((c >> 16) & 0xFF);
            row[x * 3 + 1] = (u8)((c >> 8) & 0xFF);
            row[x * 3 + 2] = (u8)(c & 0xFF);
        }
        fwrite(&row[0], 1, (size_t)w * 3, f);
    }
    fclose(f);
    return true;
}

struct AudioPack {
    std::vector< std::vector<i16> > data;
    std::vector<std::string>        names;
    std::vector<audio::Clip>        clips;

    bool Load(const std::vector<u8>& bytes)
    {
        if (bytes.size() < 12 || memcmp(&bytes[0], "HGEA", 4) != 0) return false;
        u32 ver = 0, count = 0;
        memcpy(&ver, &bytes[4], 4);
        memcpy(&count, &bytes[8], 4);
        if (ver != 1 || count == 0 || count > 64) return false;

        struct Hdr { char name[24]; u32 frames, ch, rate; };
        if (12 + sizeof(Hdr) * count > bytes.size()) return false;

        std::vector<Hdr> hs(count);
        size_t off = 12;
        for (u32 i = 0; i < count; ++i) {
            memcpy(&hs[i], &bytes[off], sizeof(Hdr));
            hs[i].name[23] = 0;
            off += sizeof(Hdr);
        }

        data.resize(count);
        names.resize(count);
        clips.resize(count);
        for (u32 i = 0; i < count; ++i) {
            if (hs[i].frames == 0 || hs[i].ch == 0 || hs[i].ch > 2) return false;
            const size_t n = (size_t)hs[i].frames * hs[i].ch;
            if (off + n * 2 > bytes.size()) return false;
            data[i].resize(n);
            memcpy(&data[i][0], &bytes[off], n * 2);
            off += n * 2;
            names[i].assign(hs[i].name);
            clips[i].pcm      = &data[i][0];
            clips[i].frames   = (int)hs[i].frames;
            clips[i].channels = (int)hs[i].ch;
            clips[i].rate     = (int)hs[i].rate;
        }
        return true;
    }

    const audio::Clip* Find(const char* n) const
    {
        for (size_t i = 0; i < names.size(); ++i)
            if (names[i] == n) return &clips[i];
        return nullptr;
    }
};

} // namespace

#ifdef HGE_LIBRETRO
int bubble_game_main(int argc, char** argv)
#else
int main(int argc, char** argv)
#endif
{
    bool selftest = false;
    const char* shotPath = nullptr;
    int frames = 0;
    int warmup = 0;
    float animOffset = 0.0f;
    float simSeconds = 0.0f;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--selftest")) selftest = true;
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) shotPath = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--warmup") && i + 1 < argc) warmup = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--anim") && i + 1 < argc) animOffset = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--sim") && i + 1 < argc) simSeconds = (float)atof(argv[++i]);
    }

    if (selftest) {
        Game game;
        game.Configure(kBoardCols, 0.0f);
        char msg[256];
        const bool ok = game.SelfTest(msg, sizeof(msg));
        printf("[bubble] SELFTEST %s: %s\n", ok ? "PASS" : "FAIL", msg);
        return ok ? 0 : 1;
    }

    if (!plat::Init(WIN_W, WIN_H, "Bubble Versus - HGE-SDL", !shotPath, true)) {
        plat::MessageBox("Bubble Versus", "Could not create the window (SDL init failed).");
        return 2;
    }

    Ctx ctx;
    ctx.g = hge::CreateGfx();
    if (!ctx.g->Init(plat::Window(), plat::GLGetProc)) {
        plat::MessageBox("Bubble Versus", "Could not create the renderer.\n"
                                          "A GPU driver with OpenGL 2.1 / OpenGL ES 2.0 is required.");
        plat::Shutdown();
        return 2;
    }
    fprintf(stderr, "[t] gfx_init=%.3fs\n", plat::Time());
    ctx.g->OnResize(plat::Width(), plat::Height());
    printf("[bubble] renderer: %s\n", ctx.g->Info());

    std::vector<u8> embGame, embAudio;
    bool haveEmbedded = false;
    {
        std::string selfPaths[2];
        selfPaths[0] = (argc > 0 && argv[0]) ? argv[0] : "";
        selfPaths[1] = std::string(plat::BasePath()) + "bubble_level1";
        for (int sp = 0; sp < 2 && !haveEmbedded; ++sp) {
            const char* self = selfPaths[sp].empty() ? nullptr : selfPaths[sp].c_str();
            if (!self) continue;
            const long long sz = plat::FileSize(self);
            if (sz > 64) {
                std::vector<u8> tail;
                if (plat::ReadFileRange(self, sz - 16, 16, tail) &&
                    memcmp(&tail[0], "HGEEMB01", 8) == 0) {
                    u32 gs = 0, as = 0;
                    memcpy(&gs, &tail[8], 4);
                    memcpy(&as, &tail[12], 4);
                    const long long base = sz - 16 - (long long)gs - (long long)as;
                    if (base > 0 &&
                        plat::ReadFileRange(self, base, gs, embGame) &&
                        plat::ReadFileRange(self, base + (long long)gs, as, embAudio))
                        haveEmbedded = true;
                }
            }
        }
    }

    Pack pack;
    std::string cands[3];
    cands[0] = std::string(plat::BasePath()) + "game.pak";
    cands[1] = "game.pak";
    cands[2] = "assets/game.pak";
    bool loaded = false;
    if (haveEmbedded)
        loaded = pack.LoadFromMemory(&embGame[0], embGame.size(), "<exe>");
    for (int i = 0; i < 3 && !loaded; ++i) {
        std::vector<u8> bytes;
        if (!plat::ReadFileAll(cands[i].c_str(), bytes)) continue;
        loaded = pack.LoadFromMemory(&bytes[0], bytes.size(), cands[i].c_str());
        if (!loaded)
            fprintf(stderr, "[bubble] %s: %s\n", cands[i].c_str(), pack.LastError());
    }
    fprintf(stderr, "[t] game.pak=%s %.3fs\n", loaded?"ok":"FAIL", plat::Time());
    if (!loaded) {
        plat::MessageBox("Bubble Versus",
                         "game.pak not found.\n\n"
                         "Keep bubble_level1.exe, game.pak and audio.pak in the same folder.");
        return 2;
    }

    const Image* img;
    ctx.bg = upload(ctx.g, pack.Find("bg"));
    for (int i = 0; i < kColors; ++i) {
        char n[32];
        snprintf(n, sizeof(n), "bubble%d", i);
        ctx.bubbles[i] = upload(ctx.g, pack.Find(n));
        snprintf(n, sizeof(n), "bubble%d_big", i);
        ctx.bubblesBig[i] = upload(ctx.g, pack.Find(n));
        snprintf(n, sizeof(n), "bubble%d_anim", i);
        ctx.bubblesAnim[i] = upload(ctx.g, pack.Find(n));
        if (!ctx.bubbles[i] || !ctx.bubblesBig[i] || !ctx.bubblesAnim[i]) {
            plat::MessageBox("Bubble Versus", "game.pak is corrupt (bubble sprites missing).");
            return 2;
        }
    }
    if ((img = pack.Find("cannon_base")) != nullptr) ctx.cannonBase = upload(ctx.g, img);
    if ((img = pack.Find("cannon_barrel")) != nullptr) ctx.cannonBarrel = upload(ctx.g, img);
    if ((img = pack.Find("dot")) != nullptr) ctx.dot = upload(ctx.g, img);
    if ((img = pack.Find("ring")) != nullptr) ctx.ring = upload(ctx.g, img);
    if ((img = pack.Find("wave")) != nullptr) ctx.wave = upload(ctx.g, img);
    if ((img = pack.Find("font")) != nullptr) ctx.font = upload(ctx.g, img);
    for (int v = 0; v < 2; ++v) {
        char n[32];
        for (int i = 0; i < 3; ++i) {
            snprintf(n, sizeof(n), "bub%d_idle%d", v, i);
            if ((img = pack.Find(n)) != nullptr)
                ctx.dinoIdle[v][i] = upload(ctx.g, img);
        }
        snprintf(n, sizeof(n), "bub%d_shoot", v);
        if ((img = pack.Find(n)) != nullptr) ctx.dinoShoot[v] = upload(ctx.g, img);
        snprintf(n, sizeof(n), "bub%d_cheer", v);
        if ((img = pack.Find(n)) != nullptr) ctx.dinoCheer[v] = upload(ctx.g, img);
        snprintf(n, sizeof(n), "bub%d_sad", v);
        if ((img = pack.Find(n)) != nullptr) ctx.dinoSad[v] = upload(ctx.g, img);
        snprintf(n, sizeof(n), "bub%d_look", v);
        if ((img = pack.Find(n)) != nullptr) ctx.dinoLook[v] = upload(ctx.g, img);
    }
    if (!ctx.bg || !ctx.cannonBase || !ctx.cannonBarrel || !ctx.dot || !ctx.ring || !ctx.font) {
        plat::MessageBox("Bubble Versus", "game.pak is corrupt or from an older build.");
        return 2;
    }
    fprintf(stderr, "[t] textures=%.3fs\n", plat::Time());

    // ---------------------------------------------------------- audio
    AudioPack audioPack;
    bool audioOn = false;
    if (haveEmbedded && audioPack.Load(embAudio))
        audioOn = audio::Init();
    if (!audioOn) {
        std::string apath[3];
        apath[0] = std::string(plat::BasePath()) + "audio.pak";
        apath[1] = "audio.pak";
        apath[2] = "assets/audio.pak";
        std::vector<u8> abytes;
        for (int i = 0; i < 3 && !audioOn; ++i) {
            if (!plat::ReadFileAll(apath[i].c_str(), abytes)) continue;
            if (!audioPack.Load(abytes)) continue;
            if (!audio::Init()) break;
            audioOn = true;
        }
    }
    fprintf(stderr, "[t] audio_init=%.3fs (on=%d)\n", plat::Time(), (int)audioOn);
    const audio::Clip* cShoot = audioOn ? audioPack.Find("shoot") : nullptr;
    const audio::Clip* cStick = audioOn ? audioPack.Find("stick") : nullptr;
    const audio::Clip* cPop   = audioOn ? audioPack.Find("pop") : nullptr;
    const audio::Clip* cDrop  = audioOn ? audioPack.Find("drop") : nullptr;
    const audio::Clip* cDesc  = audioOn ? audioPack.Find("descend") : nullptr;
    const audio::Clip* cWin   = audioOn ? audioPack.Find("win") : nullptr;
    const audio::Clip* cLose  = audioOn ? audioPack.Find("lose") : nullptr;
    if (audioOn) {
        const audio::Clip* bgm = audioPack.Find("bgm");
        if (bgm) audio::PlayMusic(*bgm, 0.40f);
    }
    printf("[bubble] %s\n", audio::Info());

    Side sides[2];
    resetBattle(sides);

    // --warmup N: player fires N shots before the first frame (screenshots)
    for (int i = 0; i < warmup; ++i) {
        if (sides[0].game.GetState() != Game::AIM) break;
        int r = -1, c = -1;
        for (int rr = 0; rr < sides[0].game.RowsUsed() && r < 0; ++rr) {
            const int n = sides[0].game.RowCount(rr);
            for (int cc = 0; cc < n; ++cc) {
                if (sides[0].game.Cell(rr, cc) == sides[0].game.CurrentColor()) {
                    r = rr; c = cc; break;
                }
            }
        }
        if (r < 0) break;
        sides[0].game.AimAt(sides[0].game.XOf(r, c) + ((i * 37) % 17) - 8.0f,
                            Game::YOf(r));
        sides[0].game.Fire();
        int guard = 0;
        while (sides[0].game.IsFlying() && guard++ < 4000) sides[0].game.Update(1.0f / 240.0f);
    }

    if (simSeconds > 0.0f) {
        const float step = 1.0f / 60.0f;
        for (float t = 0.0f; t < simSeconds; t += step) {
            updateBattle(sides, step);
            aiUpdate(sides[1], step);
        }
        printf("[bubble] sim %.1fs: p1 left=%d p2 left=%d p1 risers=%d p2 risers=%d\\n",
               simSeconds, sides[0].game.BubblesLeft(), sides[1].game.BubblesLeft(),
               (int)sides[0].game.Risers().size(), (int)sides[1].game.Risers().size());
    }

    int  iw = plat::Width(), ih = plat::Height();
    fprintf(stderr, "[t] enter_loop=%.3fs size=%dx%d\n", plat::Time(), iw, ih);
    int  frame = 0;
    int  shotFrame = shotPath ? 40 : -1;
    double watchdog = 0.0;
    { const char* wd = getenv("BUBBLE_WATCHDOG"); if (wd) watchdog = atof(wd); }
    if (watchdog > 0.0) fprintf(stderr, "[t] watchdog=%.0fs\n", watchdog);
    bool quit = false;
    float resultTimer = -1.0f;
    int  lastMoves[2] = { 0, 0 };
    int  lastPopsAll = 0, lastDropsAll = 0, popsAtFire = 0;
    double last = plat::Time();
    Game::State prevState[2] = { sides[0].game.GetState(), sides[1].game.GetState() };

    while (!quit) {
        plat::Event ev;
        while (plat::Poll(ev)) {
            if (ev.type == plat::EV_QUIT) quit = true;
            else if (ev.type == plat::EV_KEYDOWN) {
                if (ev.key == HGEK_ESCAPE) quit = true;
                else if (ev.key == HGEK_R) { resetBattle(sides); resultTimer = -1.0f; }
                else if (resultTimer > 0.0f) { /* frozen while the result shows */ }
                else if (ev.key == HGEK_SPACE) sides[0].game.Fire();
            } else if (ev.type == plat::EV_MOUSEMOVE) {
                if (resultTimer <= 0.0f) sides[0].game.AimAt((float)ev.x, (float)ev.y);
            } else if (ev.type == plat::EV_MOUSEDOWN) {
                if (resultTimer > 0.0f) { /* frozen */ }
                else if (ev.button == 0) sides[0].game.Fire();
                else if (ev.button == 2) { resetBattle(sides); resultTimer = -1.0f; }
            } else if (ev.type == plat::EV_RESIZE) {
                iw = plat::Width(); ih = plat::Height();
                ctx.g->OnResize(iw, ih);
            }
        }
        if (iw != plat::Width() || ih != plat::Height()) {
            iw = plat::Width(); ih = plat::Height();
            ctx.g->OnResize(iw, ih);
        }

        const double now = plat::Time();
        float dt = (float)(now - last);
        last = now;
        if (dt < 0.0f) dt = 0.0f;
        if (dt > 0.05f) dt = 0.05f;

        // Aim: continuous while a direction is held. Works for both keyboard
        // (SDL keyboard state) and the Miyoo gamepad D-pad / left stick.
        if (resultTimer <= 0.0f) {
            const float aimRate = 1.6f; // rad/s (slower for finer aim control)
            if (plat::KeyDown(HGEK_LEFT))  sides[0].game.AimBy(-aimRate * dt);
            if (plat::KeyDown(HGEK_RIGHT)) sides[0].game.AimBy(aimRate * dt);
        }

        const bool over = sides[0].game.GetState() == Game::LOSE ||
                          sides[1].game.GetState() == Game::LOSE;

        if (over) {
            if (resultTimer < 0.0f) resultTimer = 3.0f;
            else resultTimer -= dt;
            if (resultTimer <= 0.0f) {
                resetBattle(sides);
                resultTimer = -1.0f;
                lastMoves[0] = lastMoves[1] = 0;
                lastPopsAll = lastDropsAll = 0;
                prevState[0] = sides[0].game.GetState();
                prevState[1] = sides[1].game.GetState();
            }
        } else {
            resultTimer = -1.0f;
            updateBattle(sides, dt);
            aiUpdate(sides[1], dt);
        }

        if (audioOn) {
            const int mv0 = sides[0].game.Moves();
            const int pp0 = sides[0].game.Pops();
            const int dd0 = sides[0].game.Drops();
            const int de0 = sides[0].game.Descends();
            if (mv0 > lastMoves[0]) { if (cShoot) audio::Play(*cShoot, 0.38f); popsAtFire = pp0; }
            if (pp0 > lastPopsAll)  { if (cPop)   audio::Play(*cPop, 0.52f); }
            if (dd0 > lastDropsAll) { if (cDrop)  audio::Play(*cDrop, 0.48f); }
            if (de0 > lastMoves[1]) { if (cDesc)  audio::Play(*cDesc, 0.40f); }
            if (prevState[0] == Game::FLY && sides[0].game.GetState() != Game::FLY &&
                pp0 == popsAtFire) {
                if (cStick) audio::Play(*cStick, 0.30f);
            }
            lastMoves[0] = mv0;
            lastMoves[1] = de0;
            lastPopsAll = pp0;
            lastDropsAll = dd0;

            const Game::State st0 = sides[0].game.GetState();
            const Game::State st1 = sides[1].game.GetState();
            if (st0 == Game::LOSE && prevState[0] != Game::LOSE) { if (cLose) audio::Play(*cLose, 0.55f); }
            if (st1 == Game::LOSE && prevState[1] != Game::LOSE) { if (cWin)  audio::Play(*cWin, 0.70f); }
            prevState[0] = st0;
            prevState[1] = st1;
        }

        render(ctx, sides, (float)now + animOffset, resultTimer);

        if (shotPath && frame + 1 >= shotFrame) {
            std::vector<u32> px((size_t)iw * ih);
            if (ctx.g->ReadPixels(0, 0, iw, ih, &px[0]))
                if (writePPM(shotPath, &px[0], iw, ih))
                    printf("[bubble] wrote %s (%dx%d)\n", shotPath, iw, ih);
            quit = true;
        }

        plat::SwapBuffers();

#ifdef HGE_PLATFORM_MIYOO
        // No vsync on the SDL-renderer backend: cap to ~60 fps so we don't
        // spin at 300 fps and drain the battery / tear the panel.
        {
            const double targetDt = 1.0 / 60.0;
            const double spent = plat::Time() - now;
            if (spent < targetDt)
                plat::SleepMs((int)((targetDt - spent) * 1000.0));
        }
#endif

        ++frame;
        if (frames > 0 && frame >= frames) quit = true;
        if (watchdog > 0.0 && now > watchdog) { fprintf(stderr, "[watchdog] auto-exit\n"); quit = true; }
    }

    audio::Shutdown();
    ctx.g->Shutdown();
    delete ctx.g;
    plat::Shutdown();
    return 0;
}
