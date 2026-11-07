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
        // name      jitter simN wDrop focus  wSelf self blunder bounce pace
        { "EASY",     40.0f,  0,    0, 0.00f, 0.0f, 0, 0.35f, 0.00f, 1.60f },
        { "CASUAL",   45.0f,  0,    0, 0.00f, 0.0f, 0, 0.20f, 0.25f, 1.30f },
        { "NORMAL",   16.0f,  0,    0, 0.00f, 0.0f, 0, 0.00f, 0.00f, 1.00f },
        { "SHARP",    10.0f,  8,   90, 0.55f, 1.2f, 8, 0.00f, 0.85f, 0.75f },
        { "MASTER",    2.0f, 18,  110, 0.85f, 1.6f, 7, 0.00f, 1.00f, 0.55f },
    };
    return kTable[aiClampLevel(level) - 1];
}

void aiApplyLevel(Side& s, int level)
{
    level = aiClampLevel(level);
    const AiLevelInfo& lv = aiLevelInfo(level);
    s.aiLevel = level;
    s.aiJitter = lv.jitter;
    s.aiFocusW = lv.focusW;
    s.aiBounceP = lv.bounce;
    s.aiPace = lv.pace;
    s.aiSimN = lv.simN;
    s.aiWDrop = lv.wDrop;
    s.aiWSelf = lv.wSelf;
    s.aiSelfDepth = lv.selfDepth;
    s.aiBlunder = lv.blunder;
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
        s[i].aiPlanned = false;
        s[i].aiAimCol = -1;
        s[i].aiFallbackShot = false;
        s[i].aiCandCount = 0;
        s[i].aiTopCount = 0;
        s[i].aiSimCursor = 0;
        s[i].aiHaveChoice = false;
        s[i].aiChosenR = -1;
        s[i].aiChosenC = -1;
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
        // Stage shipped with jitter 14; duel's NORMAL is 12. Keep the stage
        // stream bit-identical (--sim-only checksum).
        s[i].aiJitter = 14.0f;
        s[i].aiBounceP = 0.60f;
        s[i].aiFocusW = 0.0f;   // stage AI has no column bias
    }
    s[0].ai = false;
    s[1].ai = true;
}

static bool aiPlanStage(Side& s)
{
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
    int aimCol = bestC;
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
        int   bC = -1;
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
                    if (score > bScore) { bScore = score; bX = mx; bY = ty; bC = c; }
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
                aimCol = bC;
                if (getenv("HGE_DEBUG_AI"))
                    fprintf(stderr, "[ai] bounce move=%d aim=%.0f,%.0f direct=%d bounce=%d\n",
                            s.game.Moves(), bX, bY, direct, bounce);
            }
        }
    }

    s.aiFallbackShot = false;
    if (bestR < 0 && !bounceShot) {
        const float miss = s.sparring ? (s.fireScale < 0.8f ? 44.0f : 64.0f) : 40.0f;
        const float tx = s.game.ShooterX() + (rnd01() - 0.5f) * miss;
        s.game.AimAt(tx, 120.0f);
        aimCol = (int)((tx - s.game.X0()) / kD);
        if (aimCol < 0) aimCol = 0;
        if (aimCol >= kBoardCols) aimCol = kBoardCols - 1;
        s.aiAimCol = aimCol;
        s.aiPlanned = true;
        s.aiFallbackShot = true;
        return false;
    }

    const float jitter = bounceShot
                             ? 5.0f
                             : (s.sparring ? (s.fireScale < 0.8f ? 18.0f : 26.0f)
                                           : s.aiJitter);
    const float tx = aimX + (rnd01() - 0.5f) * jitter;
    s.game.AimAt(tx, aimY);
    s.aiAimCol = aimCol;
    s.aiPlanned = true;
    return bounceShot;
}

// ---- duel planner -------------------------------------------------------
struct SimShot {
    int pops;
    int drops;
    int depthAfter;
    int landCol;
    bool lost;
};

static Game g_replayGame;

static void replayShot(const Game& src, float aimX, float aimY, SimShot& out)
{
    g_replayGame = src;
    g_replayGame.AimAt(aimX, aimY);
    g_replayGame.Fire();
    int guard = 0;
    while (g_replayGame.GetState() == Game::FLY && guard++ < 800)
        g_replayGame.Update(1.0f / 60.0f);
    out.lost = (g_replayGame.GetState() == Game::LOSE);
    out.pops = g_replayGame.Pops() - src.Pops();
    out.drops = g_replayGame.Drops() - src.Drops();
    out.depthAfter = g_replayGame.DepthRows();
    out.landCol = g_replayGame.LastLandCol();
}

static int deepestColWithColor(const Game& g, int color)
{
    int best = -1, bestD = -1;
    for (int c = 0; c < kBoardCols; ++c) {
        const int d = g.ColumnDepth(c);
        if (d <= 0) continue;
        if (g.Cell(d - 1, c) != color) continue;
        if (d > bestD) { bestD = d; best = c; }
    }
    return best;
}

static int sameClusterSize(const Game& g, int r0, int c0, int color)
{
    static const int kMaxCells = kMaxRows * kColsMax;
    int qr[kMaxCells], qc[kMaxCells];
    bool seen[kMaxRows][kColsMax] = {{ false }};
    int head = 0, tail = 0;
    seen[r0][c0] = true;
    qr[tail] = r0;
    qc[tail++] = c0;
    int cluster = 1;
    const float x0 = g.XOf(r0, c0);
    const float y0 = Game::YOf(r0);
    while (head < tail) {
        const int r = qr[head], c = qc[head++];
        for (int nr = r - 1; nr <= r + 1; ++nr) {
            if (nr < 0 || nr >= kMaxRows) continue;
            const int n = g.RowCount(nr);
            for (int nc = 0; nc < n; ++nc) {
                if (seen[nr][nc] || g.Cell(nr, nc) != color) continue;
                const float dx = fabsf(g.XOf(nr, nc) - g.XOf(r, c));
                const float dy = fabsf(Game::YOf(nr) - Game::YOf(r));
                const bool adjacent =
                    (fabsf(dx - kD) < 1.0f && dy < 1.0f) ||
                    (fabsf(dx - kD * 0.5f) < 1.0f && fabsf(dy - kRowH) < 1.0f);
                if (!adjacent) continue;
                seen[nr][nc] = true;
                qr[tail] = nr;
                qc[tail++] = nc;
                ++cluster;
            }
        }
    }
    (void)x0;
    (void)y0;
    return cluster;
}

static int estimatedDrops(const Game& g, int r, int c, int color)
{
    const int cluster = sameClusterSize(g, r, c, color);
    return cluster >= 5 ? cluster - 3 : 0;
}
static void aiStepDuel(Side& s)
{
    if (s.aiSimN <= 0 || !s.aiHaveChoice) return;
    if (s.aiSimCursor >= s.aiTopCount) return;

    const int idx = s.aiTopIdx[s.aiSimCursor++];
    const int r = s.aiCandR[idx];
    const int c = s.aiCandC[idx];
    const float tx = s.game.XOf(r, c) + s.aiJitterDx;
    const float ty = Game::YOf(r);

    SimShot sim;
    replayShot(s.game, tx, ty, sim);
    if (sim.lost) return;

    float v = 100.0f * (float)sim.pops + (float)(s.aiWDrop * sim.drops);
    if (s.aiFocusCol >= 0 && sim.landCol == s.aiFocusCol)
        v += s.aiFocusW * 120.0f;
    if (s.aiWSelf > 0.0f && s.aiDepthBefore >= s.aiSelfDepth) {
        const int rescued = s.aiDepthBefore - sim.depthAfter;
        if (rescued > 0) v += s.aiWSelf * 80.0f * (float)rescued;
    }

    if (v > s.aiSimBest) {
        s.aiSimBest = v;
        s.aiChosenR = r;
        s.aiChosenC = c;
        s.aiChosenX = tx;
        s.aiChosenY = ty;
        s.aiAimCol = sim.landCol >= 0 ? sim.landCol : c;
        s.game.AimAt(tx, ty);
    }
}

bool aiPlan(Side& s)
{
    if (!s.duelMode) return aiPlanStage(s);

    s.aiCandCount = 0;
    s.aiTopCount = 0;
    s.aiSimCursor = 0;
    s.aiSimBest = -1.0e9f;
    s.aiHaveChoice = false;
    s.aiDepthBefore = s.game.DepthRows();
    s.aiChosenR = s.aiChosenC = -1;

    // MASTER targets the deepest column whose top bubble matches the hand
    // colour; lower levels use the deepest column set by DuelMatch.
    int focusCol = s.aiFocusCol;
    if (s.aiLevel >= kAiLevels) {
        const int c = deepestColWithColor(s.game, s.game.CurrentColor());
        if (c >= 0) focusCol = c;
    }
    s.aiFocusCol = focusCol;

    const int want = s.game.CurrentColor();
    const int topNeed = s.aiSimN > 0 ? s.aiSimN : 4;
    for (int r = 0; r <= s.game.RowsUsed() && r < kMaxRows; ++r) {
        const int n = s.game.RowCount(r);
        for (int c = 0; c < n; ++c) {
            if (s.game.Cell(r, c)) continue;
            if (!s.game.Attachable(r, c)) continue;
            if (!lineClear(s.game, s.game.ShooterX(), kShooterY,
                           s.game.XOf(r, c), Game::YOf(r))) continue;

            const int same = s.game.SameNeighbors(r, c, want);
            const int estD = estimatedDrops(s.game, r, c, want);
            int score = same * 100 - r * 3 + (int)(rnd01() * 6.0f);
            if (s.aiWDrop > 0)
                score += s.aiWDrop * estD;

            if (focusCol >= 0 && s.aiFocusW > 0.0f && c == focusCol)
                score += (int)(s.aiFocusW * 120.0f);

            if (s.aiCandCount < 128) {
                const int i = s.aiCandCount++;
                s.aiCandR[i] = r;
                s.aiCandC[i] = c;
                s.aiCandV0[i] = score;
                int pos = 0;
                while (pos < s.aiTopCount &&
                       s.aiCandV0[s.aiTopIdx[pos]] >= score) ++pos;
                if (pos < topNeed) {
                    int last = s.aiTopCount < topNeed ? s.aiTopCount : topNeed - 1;
                    for (int j = last; j > pos; --j)
                        s.aiTopIdx[j] = s.aiTopIdx[j - 1];
                    s.aiTopIdx[pos] = i;
                    if (s.aiTopCount < topNeed) ++s.aiTopCount;
                }
            }
        }
    }

    if (s.aiTopCount == 0) {
        const float miss = 40.0f;
        const float tx = s.game.ShooterX() + (rnd01() - 0.5f) * miss;
        s.game.AimAt(tx, 120.0f);
        int col = (int)((tx - s.game.X0()) / kD);
        if (col < 0) col = 0;
        if (col >= kBoardCols) col = kBoardCols - 1;
        s.aiAimCol = col;
        s.aiPlanned = true;
        s.aiFallbackShot = true;
        return false;
    }

    int pick = s.aiTopIdx[0];
    if (s.aiBlunder > 0.0f && rnd01() < s.aiBlunder) {
        const int bestC = s.aiCandC[pick];
        int pool[128];
        int poolN = 0;
        if (s.aiLevel == 1) {
            for (int i = 0; i < s.aiCandCount; ++i)
                if (abs(s.aiCandC[i] - bestC) >= 2) pool[poolN++] = i;
            if (poolN == 0)
                for (int i = 0; i < s.aiCandCount; ++i) pool[poolN++] = i;
            pick = pool[(int)(rnd01() * (float)poolN) % poolN];
        } else {
            // CASUAL: pick one of the next three V0 candidates; prefer an
            // actually-different column so the miss is readable.
            for (int k = 1; k < s.aiTopCount && k <= 3; ++k) {
                const int idx = s.aiTopIdx[k];
                if (abs(s.aiCandC[idx] - bestC) >= 2) pool[poolN++] = idx;
            }
            if (poolN == 0)
                for (int k = 1; k < s.aiTopCount && k <= 3; ++k)
                    pool[poolN++] = s.aiTopIdx[k];
            if (poolN > 0) pick = pool[(int)(rnd01() * (float)poolN) % poolN];
        }
    }

    s.aiChosenR = s.aiCandR[pick];
    s.aiChosenC = s.aiCandC[pick];
    s.aiJitterDx = (rnd01() - 0.5f) * s.aiJitter;
    s.aiChosenX = s.game.XOf(s.aiChosenR, s.aiChosenC) + s.aiJitterDx;
    s.aiChosenY = Game::YOf(s.aiChosenR);
    s.game.AimAt(s.aiChosenX, s.aiChosenY);
    s.aiAimCol = s.aiChosenC;
    s.aiPlanned = true;
    s.aiHaveChoice = true;
    if (s.aiSimN > 0) s.aiTopCount = s.aiTopCount < s.aiSimN
                                           ? s.aiTopCount : s.aiSimN;
    return false;
}

void aiUpdate(Side& s, float dt)
{
    if (s.stun > 0.0f) {
        s.stun -= dt;
        return;
    }
    if (s.game.GetState() != Game::AIM) return;
    if (s.duelMode) aiStepDuel(s);
    s.aiTimer -= dt;
    if (s.aiTimer > 0.0f) return;

    if (s.duelMode) {
        if (!s.aiPlanned) {
            s.aiTimer = kDuelCpuThinkSec;
            aiPlan(s);
            return;
        }
        // Replays are paced by the caller (one per rendered frame); if the
        // timer expires first, fire with the best candidate found so far.
        s.game.Fire();
        s.aiPlanned = false;
        s.aiAimCol = -1;
        s.aiFallbackShot = false;
        s.aiHaveChoice = false;
        s.aiTimer = 0.0f;
        return;
    }

    const bool bounceShot = aiPlanStage(s);
    const bool fallback = s.aiFallbackShot;
    s.game.Fire();
    s.aiPlanned = false;
    s.aiAimCol = -1;
    s.aiFallbackShot = false;
    const float pace = s.aiPace;
    if (fallback) {
        s.aiTimer = (s.sparring
                         ? (2.3f + 0.25f * (float)(s.game.Moves() % 3)) * s.fireScale
                         : 0.9f + rnd01() * 0.8f) * pace;
    } else {
        s.aiTimer = ((s.sparring
                          ? (2.2f + 0.25f * (float)(s.game.Moves() % 3)) * s.fireScale
                          : 1.00f + rnd01() * 0.75f) +
                     (bounceShot ? 0.15f : 0.0f)) * pace;
    }
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
