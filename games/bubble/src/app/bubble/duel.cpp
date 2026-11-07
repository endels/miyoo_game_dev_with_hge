#include "app/bubble/duel.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace bbl {

int DuelAttackForShot(int pops, int drops)
{
    int atk = DuelAttackForPops(pops) + DuelAttackForDrops(drops);
    if (pops + drops >= 10) ++atk;                          // A6
    if (atk > kAttackCapPerShot) atk = kAttackCapPerShot;   // A7
    if (atk < 0) atk = 0;
    return atk;
}

int DuelDescendEvery(int globalShots)
{
    if (globalShots >= kSuddenDeath2Shots) return kDescendFast2Shots;
    if (globalShots >= kSuddenDeath1Shots) return kDescendFast1Shots;
    return kDescendEveryShots;
}

void DuelMatch::Reset(u32 seed)
{
    for (int i = 0; i < 2; ++i) {
        Side& s = m_sides[i];
        s.game.Configure(kDuelCols, kBoardX[i]);
        s.game.SetColors(kMaxColors);
        s.game.Reset(seed + (u32)i * 7919u);
        s.energy = 0.0f;
        s.aiTimer = kAIStartDelay;
        s.ai = (i == 1);
        s.duelMode = true;
        s.aiPlanned = false;
        s.aiAimCol = -1;
        s.aiFallbackShot = false;
        s.aiFocusCol = -1;
        // Only the CPU side follows the dial; the player side stays at the
        // default so --duel-sim and autoplay remain deterministic.
        aiApplyLevel(s, i == 1 ? m_aiLevel : kAiLevelDefault);
        // Duel ignores the stage energy/garbage chain: attacks are table driven
        // and applied by this class.
        s.sparring = false;
        s.keepWin = false;
        s.stun = 0.0f;
        s.clearStun = 0.0f;
        s.fireScale = 1.0f;
        s.garbageAfterMoves = 1000000000;
        s.energyGain = 0.0f;
        s.stageRules = false;
        s.dropEnergy = 0.0f;
        s.waveSize = 0;
        s.attackBonus = 0;
        s.chainPay = false;
        s.lastPops = s.game.Pops();
        s.lastDrops = s.game.Drops();
    }

    // G6: the right board is a column mirror of the left one, so both sides
    // start on an identical puzzle.
    m_sides[1].game.MirrorFrom(m_sides[0].game);
    m_sides[0].game.SetRowEvery(kDescendEveryShots);
    m_sides[1].game.SetRowEvery(kDescendEveryShots);

    m_turn = 0;
    m_shotsLeft = kShotsPerTurn;
    m_sub = kDuelPlay;
    m_handoverPending = false;
    m_handoverStall = 0.0f;
    m_result = kDuelRunning;
    m_globalShots = 0;
    m_shotOpen[0] = m_shotOpen[1] = false;
    m_basePops[0] = m_basePops[1] = 0;
    m_baseDrops[0] = m_baseDrops[1] = 0;
    m_turnShotIdx = 0;
    m_attackOut = 0;
    m_attackOutSide = -1;
    m_attackEvent = 0;
    m_attackEventSide = -1;
    m_attackEventCol = 0;
    m_overflow = 0;
    m_maxAtk[0] = m_maxAtk[1] = 0;
    memset(m_colHits, 0, sizeof(m_colHits));
    memset(m_turnGarbage, 0, sizeof(m_turnGarbage));
    m_garbageRng = seed ^ 0x9E3779B9u;
    if (m_garbageRng == 0) m_garbageRng = 1;
    m_turnCount = 1;
    m_log.clear();

    for (int i = 0; i < 2; ++i)
        m_depth[i] = m_sides[i].game.DepthRows();

    m_sides[0].aiFocusW = 0.0f;                 // the human side is never biased
    m_sides[1].aiFocusCol = m_sides[0].game.DeepestColumn();
}

void DuelMatch::SetAiLevel(int level)
{
    m_aiLevel = aiClampLevel(level);
    aiApplyLevel(m_sides[1], m_aiLevel);
}

void DuelMatch::beginTurn(int side)
{
    m_turn = side;
    m_shotsLeft = kShotsPerTurn;
    m_sub = kDuelPlay;
    m_handoverPending = false;
    m_handoverStall = 0.0f;
    m_turnShotIdx = 0;
    if (side == 0) ++m_turnCount;       // one round = player turn + CPU turn
    memset(m_turnGarbage, 0, sizeof(m_turnGarbage));

    const int other = 1 - side;
    Side& s = m_sides[side];
    s.aiTimer = kAIStartDelay;
    s.aiPlanned = false;
    s.aiAimCol = -1;
    s.aiFallbackShot = false;
    // AI4: value shots that land in the column the opponent is deepest in, so
    // the attack lands where it hurts.
    s.aiFocusCol = m_sides[other].game.DeepestColumn();
    // Column bias is per level, not per turn (the value used to be a hard 0.3
    // for both sides; keeping it table-driven leaves level 3 bit-identical).
    s.aiFocusW = aiLevelInfo(s.aiLevel).focusW;
    setRowEvery();
}

bool DuelMatch::CanPlayerFire() const
{
    return m_result == kDuelRunning &&
           m_turn == 0 &&
           m_shotsLeft > 0 &&
           m_sides[0].game.GetState() == Game::AIM;
}

DuelHudState DuelMatch::PlayerHudState() const
{
    // The renderer only sees this enum; it must never test turn/flying itself.
    // CanPlayerFire() is the first check so the HUD and the fire gate cannot
    // disagree while the player still has shots.
    if (m_turn != 0) return kDuelHudCpuShot;
    if (CanPlayerFire()) return kDuelHudYourShot;
    if (m_sides[0].game.IsFlying() || m_shotOpen[0]) return kDuelHudInFlight;
    // Player's turn with 0 shots left and no shot in the air: the per-shot
    // HUD must show 0/N instead of a fake "IN FLIGHT" while the boards settle.
    return kDuelHudYourShot;
}

int DuelMatch::CpuAimCol() const
{
    if (m_result != kDuelRunning || m_turn != 1) return -1;
    const Side& s = m_sides[1];
    if (!s.aiPlanned || s.aiTimer <= 0.0f) return -1;
    return s.aiAimCol;
}

float DuelMatch::CpuThinkLeft() const
{
    if (m_result != kDuelRunning || m_turn != 1) return 0.0f;
    const Side& s = m_sides[1];
    if (!s.aiPlanned) return 0.0f;
    return s.aiTimer > 0.0f ? s.aiTimer : 0.0f;
}

bool DuelMatch::readyToHandOver() const
{
    // Step 1+2: a hand-over may only happen once neither board has a shot in
    // the air and all incoming garbage has landed on both sides. This is the
    // single condition used by the turn flow (and later by the HUD).
    return !m_shotOpen[0] && !m_shotOpen[1] &&
           !m_sides[0].game.IsFlying() && !m_sides[1].game.IsFlying() &&
           m_sides[0].game.Risers().empty() && m_sides[1].game.Risers().empty();
}

void DuelMatch::flushRisers()
{
    // Watchdog only: a riser lands only when its board is at rest, so advance
    // the full game (not just UpdateGarbage) in small steps. WIN is parked to
    // AIM first; 600 frames is far beyond the normal rise window and exists
    // only to bound a pathological board.
    for (int i = 0; i < 2; ++i) {
        Game& g = m_sides[i].game;
        if (g.GetState() == Game::WIN) g.ResumeIfWon();
        for (int guard = 0; guard < 600 && !g.Risers().empty(); ++guard)
            g.Update(1.0f / 60.0f);
    }
}

// Pick one garbage column for the current shot. Weights mirror
// Game::PickGarbageColumns (6 on the hit column, 2 on +/-1, 1 on +/-2), but the
// budget is per turn: a column may take at most 3 in one shot and at most 4
// across the whole turn. When the local ring is full, spill outward to the
// nearest columns with budget instead of dropping damage.
int DuelMatch::pickGarbageCol(int target, int hitCol, const int have[kDuelCols])
{
    if (hitCol < 0) hitCol = 0;
    if (hitCol >= kDuelCols) hitCol = kDuelCols - 1;

    static const int off[5] = { 0, -1, 1, -2, 2 };
    static const int wgt[5] = { 6, 2, 2, 1, 1 };
    int cand[5] = { -1, -1, -1, -1, -1 };
    int wsum = 0;
    for (int k = 0; k < 5; ++k) {
        const int c = hitCol + off[k];
        if (c < 0 || c >= kDuelCols) continue;
        int cap = 4 - m_turnGarbage[target][c];
        if (cap > 3) cap = 3;
        if (have[c] >= cap) continue;
        cand[k] = c;
        wsum += wgt[k];
    }

    m_garbageRng = m_garbageRng * 1664525u + 1013904223u;
    int r = (int)((m_garbageRng >> 8) % (u32)(wsum > 0 ? wsum : 1));
    if (wsum > 0) {
        for (int k = 0; k < 5; ++k) {
            if (cand[k] < 0) continue;
            if (r < wgt[k]) return cand[k];
            r -= wgt[k];
        }
    }

    // Local ring full: walk outward and take the nearest columns that still
    // have budget. Still no column exceeds the per-turn cap.
    for (int dist = 3; dist < kDuelCols; ++dist) {
        int opts[2];
        int n = 0;
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            const int c = hitCol + sgn * dist;
            if (c < 0 || c >= kDuelCols) continue;
            int cap = 4 - m_turnGarbage[target][c];
            if (cap > 3) cap = 3;
            if (have[c] >= cap) continue;
            opts[n++] = c;
        }
        if (n > 0) {
            m_garbageRng = m_garbageRng * 1664525u + 1013904223u;
            return opts[(m_garbageRng >> 8) % (u32)n];
        }
    }

    // Last resort: any column with room. With 10 columns and at most 16
    // garbage per shot this cannot be reached, but never index out of bounds.
    for (int c = 0; c < kDuelCols; ++c) {
        int cap = 4 - m_turnGarbage[target][c];
        if (cap > 3) cap = 3;
        if (have[c] < cap) return c;
    }
    return -1;
}

void DuelMatch::setRowEvery()
{
    const int every = DuelDescendEvery(m_globalShots);
    m_sides[0].game.SetRowEvery(every);
    m_sides[1].game.SetRowEvery(every);
}

int DuelMatch::NextDescendIn() const
{
    const int every = m_sides[m_turn].game.RowEvery();
    if (every <= 0) return 1;
    const int left = every - (m_sides[m_turn].game.Moves() % every);
    return left < 1 ? 1 : left;
}

int DuelMatch::ColumnHits(int side, int col) const
{
    if (side < 0 || side > 1 || col < 0 || col >= kDuelCols) return 0;
    return m_colHits[side][col];
}

bool DuelMatch::TakeAttackEvent(int* side, int* atk, int* col)
{
    if (m_attackEvent <= 0) return false;
    if (side) *side = m_attackEventSide;
    if (atk) *atk = m_attackEvent;
    if (col) *col = m_attackEventCol;
    m_attackEvent = 0;
    m_attackEventSide = -1;
    return true;
}

void DuelMatch::AimAt(float mx, float my)
{
    if (m_result != kDuelRunning) return;
    m_sides[0].game.AimAt(mx, my);
}

void DuelMatch::AimBy(float deltaRad)
{
    if (m_result != kDuelRunning) return;
    m_sides[0].game.AimBy(deltaRad);
}

bool DuelMatch::Fire()
{
    // The only gate: running / player's turn / board at rest / shots left.
    // No phase switches and no "keep the click for later" here.
    if (!CanPlayerFire()) return false;

    m_sides[0].game.Fire();
    m_shotOpen[0] = true;
    m_basePops[0] = m_sides[0].game.Pops();
    m_baseDrops[0] = m_sides[0].game.Drops();
    --m_shotsLeft;
    return true;
}

void DuelMatch::settleShot(int side)
{
    Side& s = m_sides[side];
    Game& g = s.game;
    const int pops = g.Pops() - m_basePops[side];
    const int drops = g.Drops() - m_baseDrops[side];
    int atk = DuelAttackForShot(pops, drops);

    // AI6: the top two levels convert a connecting shot into more garbage. Aim
    // stopped paying off once the bot was already accurate (an A/B over 40
    // seeds found no gain from replaying candidates), so SHARP and MASTER buy
    // raw attack power instead; NORMAL, the default, buys nothing.
    const int bonus = aiLevelInfo(m_sides[side].aiLevel).atkBonus;
    if (atk > 0 && bonus > 0) {
        atk += bonus;
        if (atk > kAttackCapPerShot + bonus) atk = kAttackCapPerShot + bonus;
    }

    // A7.1: clearing the whole board is a one-off attack, then play continues.
    if (g.GetState() == Game::WIN) {
        atk += kClearBoardBonus;
        g.ResumeIfWon();
    }

    const int col = g.LastLandCol();
    const int target = 1 - side;
    int cols[32];
    int ncols = 0;
    int have[kDuelCols] = { 0 };
    for (int i = 0; i < atk; ++i) {
        const int c = pickGarbageCol(target, col, have);
        if (c < 0) break;       // unreachable: 10 columns x 4 > max shot
        cols[ncols++] = c;
        ++have[c];
    }
    if (ncols > 0) {
        const int before = m_sides[target].game.OverflowCount();
        m_sides[target].game.AddGarbageCols(cols, ncols);
        m_overflow += m_sides[target].game.OverflowCount() - before;
        for (int i = 0; i < ncols; ++i)
            ++m_turnGarbage[target][cols[i]];
    }

    m_shotOpen[side] = false;
    ++m_globalShots;
    m_depth[side] = g.DepthRows();
    m_depth[target] = m_sides[target].game.DepthRows();

    if (atk > m_maxAtk[side]) m_maxAtk[side] = atk;
    if (col >= 0 && col < kDuelCols) ++m_colHits[side][col];

    if (atk > 0) {
        m_attackOut = atk;
        m_attackOutSide = side;
        m_attackEvent = atk;
        m_attackEventSide = side;
        m_attackEventCol = col;
    }

    DuelShotLog rec;
    rec.globalShot = m_globalShots;
    rec.side = side;
    rec.idx = m_turnShotIdx++;
    rec.landCol = col;
    rec.pops = pops;
    rec.drops = drops;
    rec.atk = atk;
    rec.targetCol = col;
    rec.depth0 = m_depth[0];
    rec.depth1 = m_depth[1];
    m_log.push_back(rec);

    char colsBuf[64];
    colsBuf[0] = 0;
    int colsOff = 0;
    for (int i = 0; i < ncols && colsOff < (int)sizeof(colsBuf) - 4; ++i) {
        colsOff += snprintf(colsBuf + colsOff, sizeof(colsBuf) - (size_t)colsOff,
                            "%s%d", i ? ":" : "", cols[i]);
    }

    fprintf(stderr,
            "[shot] g=%d side=%d turn=%d idx=%d col=%d clear=%d pops=%d drops=%d "
            "atk=%d tgt=%d depth0=%d depth1=%d desc=%d cols=%s\n",
            m_globalShots, side, m_turn, rec.idx, col, pops + drops,
            pops, drops, atk, col, m_depth[0], m_depth[1],
            DuelDescendEvery(m_globalShots), colsBuf);

    setRowEvery();
    checkOver();
    if (m_result != kDuelRunning) return;
    // The hand-over itself is decided by Tick() after this shot is closed, so
    // both boards can keep resolving garbage until they are at rest.
}

void DuelMatch::checkOver()
{
    if (m_result != kDuelRunning) return;

    // A board can also die on the engine's own limits before DepthRows() ever
    // reaches kDepthMax: the kMaxRows watermark (it never shrinks, so a board
    // whose lower rows were all cleared can still hit it) or a land with no
    // free cell. Left unnoticed, that board refuses Fire() forever and the
    // match hangs instead of ending.
    const bool p0 = m_depth[0] >= kDepthMax ||
                    m_sides[0].game.GetState() == Game::LOSE;
    const bool p1 = m_depth[1] >= kDepthMax ||
                    m_sides[1].game.GetState() == Game::LOSE;
    if (p0 && p1) {
        m_result = kDuelDraw;
    } else if (p1) {
        m_result = kDuelPlayerWin;
    } else if (p0) {
        m_result = kDuelPlayerLose;
    } else if (m_globalShots >= kMaxGlobalShots) {
        m_result = kDuelDraw;
    }

    if (m_result != kDuelRunning) {
        m_sub = kDuelOver;
        fprintf(stderr, "[duel] result=%d shots=%d depth0=%d depth1=%d overflow=%d\n",
                m_result, m_globalShots, m_depth[0], m_depth[1], m_overflow);
    }
}

void DuelMatch::Tick(float dt, bool playerBot)
{
    if (m_result != kDuelRunning) return;

    m_depth[0] = m_sides[0].game.DepthRows();
    m_depth[1] = m_sides[1].game.DepthRows();
    checkOver();
    if (m_result != kDuelRunning) return;

    if (m_sub != kDuelPlay) return;

    // The bot may act only while its turn still has shots and no hand-over is
    // waiting for garbage to land.
    if (!m_handoverPending && (m_turn == 1 || playerBot) &&
        !m_shotOpen[m_turn] && m_shotsLeft > 0) {
        Side& s = m_sides[m_turn];
        if (s.game.GetState() == Game::AIM) {
            // One plan + a fresh 0.30 s think per shot. The plan is what the
            // telegraph shows; aiUpdate() only counts down and fires.
            if (!s.aiPlanned) {
                s.aiTimer = kDuelCpuThinkSec;
                aiPlan(s);
            } else {
                const int movesBefore = s.game.Moves();
                aiUpdate(s, dt);
                if (s.game.Moves() != movesBefore) {
                    // The bot fired: open the same settlement window a human
                    // shot would have opened.
                    m_shotOpen[m_turn] = true;
                    m_basePops[m_turn] = s.game.Pops();
                    m_baseDrops[m_turn] = s.game.Drops();
                    --m_shotsLeft;
                }
            }
        }
    }

    // Both boards always run the full update in the single play phase. This
    // also advances the particles and lands incoming garbage while a turn is
    // waiting to hand over.
    m_sides[0].game.Update(dt);
    m_sides[1].game.Update(dt);

    // Close the settlement window once the shot has come to rest.
    for (int i = 0; i < 2; ++i) {
        if (!m_shotOpen[i]) continue;
        if (m_sides[i].game.IsFlying()) continue;
        const Game::State st = m_sides[i].game.GetState();
        if (st != Game::AIM && st != Game::WIN) continue;
        settleShot(i);
        if (m_result != kDuelRunning) return;
    }

    // Hand over as soon as the last shot is settled and both boards are at
    // rest (no shot in the air, no garbage still rising). There is no banner
    // phase and no fixed delay; the wait is only the boards settling.
    if (m_shotsLeft <= 0 && !m_shotOpen[0] && !m_shotOpen[1]) {
        if (readyToHandOver()) {
            m_handoverPending = false;
            beginTurn(1 - m_turn);
        } else if (!m_handoverPending) {
            m_handoverPending = true;
            m_handoverStall = 0.0f;
        } else {
            m_handoverStall += dt;
            if (m_handoverStall >= kHandoverStallSec) {
                const int r0 = (int)m_sides[0].game.Risers().size();
                const int r1 = (int)m_sides[1].game.Risers().size();
                flushRisers();
                fprintf(stderr,
                        "[duel] handover stall %.2fs risers=%d,%d flying=%d,%d "
                        "after_flush=%d,%d\n",
                        m_handoverStall, r0, r1,
                        (int)m_sides[0].game.IsFlying(),
                        (int)m_sides[1].game.IsFlying(),
                        (int)m_sides[0].game.Risers().size(),
                        (int)m_sides[1].game.Risers().size());
                m_handoverPending = false;
                beginTurn(1 - m_turn);
            }
        }
    }

    // Garbage can finish a board off between shots. A board left in WIN with
    // no settlement open refuses the next Fire() forever, which stalls the
    // whole match, so clear it here. A shot's own clear is settled (and paid
    // the clear-board bonus) in settleShot before this runs.
    for (int i = 0; i < 2; ++i)
        if (!m_shotOpen[i]) m_sides[i].game.ResumeIfWon();

    m_depth[0] = m_sides[0].game.DepthRows();
    m_depth[1] = m_sides[1].game.DepthRows();
    checkOver();
}

} // namespace bbl
