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
    m_sub = kDuelTurnStart;
    m_timer = kTurnStartSec;
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
    m_sub = kDuelTurnStart;
    m_timer = kTurnStartSec;
    m_turnShotIdx = 0;

    const int other = 1 - side;
    Side& s = m_sides[side];
    s.aiTimer = kAIStartDelay;
    // AI4: value shots that land in the column the opponent is deepest in, so
    // the attack lands where it hurts.
    s.aiFocusCol = m_sides[other].game.DeepestColumn();
    // Column bias is per level, not per turn (the value used to be a hard 0.3
    // for both sides; keeping it table-driven leaves level 3 bit-identical).
    s.aiFocusW = aiLevelInfo(s.aiLevel).focusW;
    setRowEvery();
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
    if (m_result != kDuelRunning) return false;
    if (m_sub != kDuelShooting) return false;      // TURN_START/HANDOVER swallow it
    if (m_turn != 0) return false;
    if (m_sides[0].game.GetState() != Game::AIM) return false;
    if (m_shotsLeft <= 0) return false;

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
    const int before = m_sides[target].game.OverflowCount();
    if (atk > 0) m_sides[target].game.AddGarbageAt(col, atk);
    m_overflow += m_sides[target].game.OverflowCount() - before;

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

    fprintf(stderr,
            "[shot] g=%d side=%d turn=%d idx=%d col=%d clear=%d pops=%d drops=%d "
            "atk=%d tgt=%d depth0=%d depth1=%d desc=%d\n",
            m_globalShots, side, m_turn, rec.idx, col, pops + drops,
            pops, drops, atk, col, m_depth[0], m_depth[1],
            DuelDescendEvery(m_globalShots));

    setRowEvery();
    checkOver();
    if (m_result != kDuelRunning) return;

    if (m_shotsLeft <= 0) {
        m_sub = kDuelHandover;      // freeze both boards while handing over
        m_timer = kHandoverSec;
    }
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

    // Garbage already on its way keeps rising through the PASS banner. T2
    // freezes *shots* so a settle can never be decided by motion nobody can
    // see; this is damage that was committed while the shooter was watching,
    // and its own column warning is the only warning it gets.
    if (m_sub == kDuelTurnStart || m_sub == kDuelHandover) {
        m_sides[0].game.UpdateGarbage(dt);
        m_sides[1].game.UpdateGarbage(dt);
    }

    switch (m_sub) {
    case kDuelTurnStart:
        // Frozen boards (design T2): nothing *lands from a shot* during the
        // banner, so the hand-over can never be decided by motion you cannot
        // see. Garbage in flight is handled above.
        m_timer -= dt;
        if (m_timer <= 0.0f) {
            m_sub = kDuelShooting;
            m_timer = 0.0f;
        }
        break;

    case kDuelHandover:
        m_timer -= dt;
        if (m_timer <= 0.0f) beginTurn(1 - m_turn);
        break;

    case kDuelShooting: {
        if ((m_turn == 1 || playerBot) && !m_shotOpen[m_turn] && m_shotsLeft > 0) {
            Side& s = m_sides[m_turn];
            if (s.game.GetState() == Game::AIM) {
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

        // Only hand over once the last shot has actually come to rest.
        if (m_shotsLeft <= 0 && !m_shotOpen[0] && !m_shotOpen[1] &&
            m_sub == kDuelShooting) {
            m_sub = kDuelHandover;
            m_timer = kHandoverSec;
        }
        break;
    }

    default:
        break;
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
