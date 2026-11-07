#include "app/bubble/battle.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace bbl {

const float kBoardX[2] = { 6.0f, 554.0f };

namespace {

float rnd01()
{
    static u32 s = 0x9E3779B9u;
    s = s * 1664525u + 1013904223u;
    return (float)((s >> 8) & 0xFFFF) / 65535.0f;
}

// Separate RNG for the optional wall-bounce decision, so enabling or tuning
// it does not shift the main AI stream (that made A/B comparisons meaningless).
float bounceRand()
{
    static u32 s = 0x51ED270Bu;
    s = s * 1664525u + 1013904223u;
    return (float)((s >> 8) & 0xFFFF) / 65535.0f;
}

// Straight line of sight between two points, ignoring the very ends so the
// target cell and the shooter are not treated as blockers.
bool lineClear(const Game& g, float x0, float y0, float x1, float y1)
{
    const float dx = x1 - x0, dy = y1 - y0;
    const float dist = sqrtf(dx * dx + dy * dy);
    const int steps = (int)(dist / 8.0f);
    for (int i = 3; i < steps - 2; ++i) {
        const float u = (float)i / (float)steps;
        if (g.SolidAt(x0 + dx * u, y0 + dy * u)) return false;
    }
    return true;
}

// Replay a shot on a throw-away copy and score the result, so the AI can tell
// whether a fancy wall-bounce actually beats the plain shot.
int shotValue(const Game& g, float aimX, float aimY)
{
    Game sim = g;
    sim.AimAt(aimX, aimY);
    sim.Fire();
    int guard = 0;
    while (sim.GetState() == Game::FLY && guard++ < 800)
        sim.Update(1.0f / 120.0f);
    if (sim.GetState() == Game::LOSE) return -100000;

    const int cleared = (sim.Pops() - g.Pops()) + (sim.Drops() - g.Drops());
    const int corrupt = sim.CorruptPopped() - g.CorruptPopped();
    int v = cleared * 100 + corrupt * 500;
    if (sim.Score() > g.Score()) v += (sim.Score() - g.Score()) / 20;
    return v;
}

} // namespace

int aiClampLevel(int level)
{
    if (level < 1) return 1;
    if (level > kAiLevels) return kAiLevels;
    return level;
}

const AiLevelInfo& aiLevelInfo(int level)
{
    static const AiLevelInfo kTable[kAiLevels] = {
        // name      interval jitter focus  bounce atk  pace
        { "EASY",     0.95f,  34.0f, 0.00f, 0.00f, 0, 1.60f },
        { "CASUAL",   0.75f,  24.0f, 0.10f, 0.25f, 0, 1.30f },
        { "NORMAL",   0.55f,  14.0f, 0.30f, 0.60f, 0, 1.00f },
        { "SHARP",    0.42f,   8.0f, 0.55f, 0.85f, 1, 0.75f },
        { "MASTER",   0.32f,   3.0f, 0.85f, 1.00f, 2, 0.55f },
    };
    return kTable[aiClampLevel(level) - 1];
}

void aiApplyLevel(Side& s, int level)
{
    level = aiClampLevel(level);
    const AiLevelInfo& lv = aiLevelInfo(level);
    s.aiLevel = level;
    s.aiInterval = lv.interval;
    s.aiJitter = lv.jitter;
    s.aiFocusW = lv.focusW;
    s.aiBounceP = lv.bounce;
    s.aiPace = lv.pace;
    // HGE_AI_BOUNCE is the old dev A/B knob for the wall shot; it still wins.
    const char* e = getenv("HGE_AI_BOUNCE");
    if (e) s.aiBounceP = (float)atof(e);
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
        s[i].sparring = false;
        s[i].keepWin = false;
        s[i].stun = 0.0f;
        s[i].clearStun = 3.0f;
        s[i].fireScale = 1.0f;
        s[i].garbageAfterMoves = 0;
        s[i].energyGain = 1.0f;
        s[i].stageRules = false;
        s[i].dropEnergy = 1.0f;
        s[i].waveSize = kWaveSize;
        s[i].attackBonus = 0;
        s[i].chainPay = false;
        s[i].duelMode = false;
        s[i].aiFocusCol = -1;
        aiApplyLevel(s[i], kAiLevelDefault);
        s[i].aiFocusW = 0.0f;   // stage AI has no column bias
    }
    s[0].ai = false;
    s[1].ai = true;
}

void aiUpdate(Side& s, float dt)
{
    if (s.stun > 0.0f) {
        s.stun -= dt;
        return;
    }
    if (s.game.GetState() != Game::AIM) return;
    s.aiTimer -= dt;
    if (s.aiTimer > 0.0f) return;

    // Score every attachable empty cell: matching neighbours matter most,
    // then prefer higher cells so the stack does not creep downwards.
    // Only consider cells with a clear line of fire: aiming at a buried cell
    // just makes the shot bounce off the outer shell.
    auto reachable = [&](float tx, float ty) {
        return lineClear(s.game, s.game.ShooterX(), kShooterY, tx, ty);
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
            int score = same * 100 - r * 3 + (int)(rnd01() * 6.0f);
            // AI4: prefer a landing column that matches the column our attack
            // should hit (the opponent's deepest one).
            if (s.aiFocusCol >= 0 && s.aiFocusW > 0.0f && c == s.aiFocusCol)
                score += (int)(s.aiFocusW * 120.0f);

            if (score > bestScore) { bestScore = score; bestR = r; bestC = c; }
        }
    }

    float aimX = 0.0f, aimY = 0.0f;
    if (bestR >= 0) {
        aimX = s.game.XOf(bestR, bestC);
        aimY = Game::YOf(bestR);
    }

    // Occasional one-bounce shot. Wall shots make the CPU read as smarter, but
    // they are only considered when the straight options are weak (and how
    // often is per level), so the match does not become unfair.
    bool bounceShot = false;
    if (bestScore < 200 && bounceRand() < s.aiBounceP) {
        const float wallL = s.game.X0() + kR;
        const float wallR = s.game.X0() + s.game.FieldW() - kR;
        const float sx = s.game.ShooterX(), sy = kShooterY;
        int   bScore = -100000;
        float bX = 0.0f, bY = 0.0f;
        for (int r = 0; r <= s.game.RowsUsed() && r < kMaxRows; ++r) {
            const int n = s.game.RowCount(r);
            for (int c = 0; c < n; ++c) {
                if (s.game.Cell(r, c)) continue;
                if (!s.game.Attachable(r, c)) continue;
                const float tx = s.game.XOf(r, c), ty = Game::YOf(r);
                if (lineClear(s.game, sx, sy, tx, ty)) continue;  // direct works
                for (int w = 0; w < 2; ++w) {
                    // Mirror the target across the side wall: aiming at the
                    // mirrored point makes the ball bounce exactly once.
                    const float wall = w ? wallR : wallL;
                    const float mx = 2.0f * wall - tx;
                    const float t = (wall - sx) / (mx - sx);
                    if (!(t > 0.02f && t < 0.98f)) continue;
                    const float by = sy + t * (ty - sy);
                    if (by < kCeilY + kR) continue;        // ceiling arrives first
                    if (!lineClear(s.game, sx, sy, wall, by)) continue;
                    if (!lineClear(s.game, wall, by, tx, ty)) continue;
                    const int same = s.game.SameNeighbors(r, c, want);
                    const int score = same * 100 - r * 3 + 40 + (int)(bounceRand() * 6.0f);
                    if (score > bScore) { bScore = score; bX = mx; bY = ty; }
                }
            }
        }
        if (bScore > -100000) {
            // Verify against the real physics: only bounce when the simulated
            // result is clearly better than the straight shot, so the trick
            // never costs the CPU a turn.
            const int direct = (bestR >= 0) ? shotValue(s.game, aimX, aimY) : -100000;
            const int bounce = shotValue(s.game, bX, bY);
            if (bounce >= direct && bounce >= 100) {
                bounceShot = true;
                aimX = bX;
                aimY = bY;
                if (getenv("HGE_DEBUG_AI"))
                    fprintf(stderr, "[ai] bounce move=%d aim=%.0f,%.0f direct=%d bounce=%d\n",
                            s.game.Moves(), bX, bY, direct, bounce);
            }
        }
    }

    // Stage mode scales its fixed delays by the level; a duel level already
    // carries its own interval, so it is used as-is (pace 1.0 is exact).
    const float pace = s.duelMode ? 1.0f : s.aiPace;

    if (bestR < 0 && !bounceShot) {
        const float miss = s.sparring ? (s.fireScale < 0.8f ? 44.0f : 64.0f) : 40.0f;
        s.game.AimAt(s.game.ShooterX() + (rnd01() - 0.5f) * miss, 120.0f);
        s.game.Fire();
        s.aiTimer = (s.duelMode
                         ? s.aiInterval
                         : (s.sparring
                                ? (2.3f + 0.25f * (float)(s.game.Moves() % 3)) * s.fireScale
                                : 0.9f + rnd01() * 0.8f)) * pace;
        return;
    }

    const float jitter = bounceShot
                             ? 5.0f
                             : (s.sparring ? (s.fireScale < 0.8f ? 18.0f : 26.0f)
                                           : s.aiJitter);
    const float tx = aimX + (rnd01() - 0.5f) * jitter;
    s.game.AimAt(tx, aimY);
    s.game.Fire();
    s.aiTimer = (s.duelMode
                     ? s.aiInterval + (bounceShot ? 0.15f : 0.0f)
                     : ((s.sparring
                             ? (2.2f + 0.25f * (float)(s.game.Moves() % 3)) * s.fireScale
                             : 1.00f + rnd01() * 0.75f) +
                        (bounceShot ? 0.15f : 0.0f))) * pace;
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
        if ((dp > 0 || dd > 0) && s[i].game.Moves() >= s[i].garbageAfterMoves) {
            // direct pops count half, chain drops count full: dropping is the
            // skilled play, so it should be what charges the attack
            s[i].energy += ((float)dp * 0.5f + (float)dd * s[i].dropEnergy) *
                           s[i].energyGain;

            // Big drops and 5+ pop chains are skill plays: bank extra garbage
            // for the next attack wave. The queued-riser cap still bounds it.
            int bonus = 0;
            if (s[i].stageRules && dd >= 3)
                bonus = (dd >= 6) ? 2 : 1;
            if (s[i].stageRules && s[i].chainPay && dp >= 5)
                ++bonus;
            if (bonus > s[i].attackBonus) s[i].attackBonus = bonus;
            if (s[i].attackBonus > 3) s[i].attackBonus = 3;

            while (s[i].energy >= (float)kEnergyPerWave) {
                s[i].energy -= (float)kEnergyPerWave;
                int wave = s[i].waveSize + (s[i].stageRules ? s[i].attackBonus : 0);
                if (wave > 8) wave = 8;
                s[i].attackBonus = 0;
                s[1 - i].game.AddGarbage(wave);
            }
        }
        if (s[i].game.GetState() == Game::WIN) {
            if (s[i].sparring) s[i].stun = s[i].clearStun;
            if (!s[i].keepWin) s[i].game.ResumeIfWon();
        }
    }
}

u64 battleChecksum(const Side s[2])
{
    u64 h = 1469598103934665603ull;
    auto mix = [&h](u64 v) {
        for (int i = 0; i < 8; ++i) {
            h ^= (v >> (i * 8)) & 0xFFull;
            h *= 1099511628211ull;
        }
    };

    for (int i = 0; i < 2; ++i) {
        const Game& g = s[i].game;
        mix((u64)g.RowsUsed());
        mix((u64)g.Cols());
        mix((u64)g.Score());
        mix((u64)g.Moves());
        mix((u64)g.Pops());
        mix((u64)g.Drops());
        mix((u64)g.Descends());
        mix((u64)g.BubblesLeft());
        mix((u64)g.GetState());
        mix((u64)g.CurrentColor());
        mix((u64)g.NextColor());
        for (int r = 0; r < kMaxRows; ++r) {
            for (int c = 0; c < kColsMax; ++c)
                mix((u64)g.Cell(r, c));
        }
        u32 energyBits = 0;
        memcpy(&energyBits, &s[i].energy, sizeof(energyBits));
        mix((u64)energyBits);
        mix((u64)s[i].game.Risers().size());
        mix((u64)s[i].game.FallingBubbles().size());
        mix((u64)s[i].game.Particles().size());
    }
    return h;
}

} // namespace bbl
