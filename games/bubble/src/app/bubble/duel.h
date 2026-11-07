#ifndef BUBBLE_DUEL_H
#define BUBBLE_DUEL_H

// Duel v2 rules: turn flow, attack conversion and the win condition.
//
// This module owns *all* duel state and drives the two boards, but it has no
// rendering and no platform dependency, so --duel-sim can run whole matches
// headless and deterministically. BubbleApp is only an adapter: it forwards
// input, ticks this class and mirrors its state into the HUD.

#include "app/bubble/battle.h"
#include "app/bubble/game.h"

#include <vector>

namespace bbl {

// ---------------------------------------------------------------- tunables
// Every number below is a design §5.2 parameter; keep this list as the single
// place where the rules are tuned.
const int   kShotsPerTurn       = 5;      // T1
const float kHandoverSec        = 0.50f;  // T2
const int   kDescendEveryShots  = 4;      // T3
const int   kDescendFast1Shots  = 3;      // T4 (global >= kSuddenDeath1Shots)
const int   kDescendFast2Shots  = 2;      // T5 (global >= kSuddenDeath2Shots)
const int   kSuddenDeath1Shots  = 100;
const int   kSuddenDeath2Shots  = 160;
const int   kMaxGlobalShots     = 300;    // T6: hard stop -> DRAW
const int   kAttackCapPerShot   = 8;      // A7
const int   kClearBoardBonus    = 6;      // A7.1
const int   kDepthMax           = 13;     // DepthRows() >= this = over the line
const float kTurnStartSec       = 0.50f;  // TURN_START banner
const float kAIStartDelay       = 0.40f;  // T7
const float kAIInterval         = 0.55f;  // AI2 NORMAL
const int   kDuelCols           = 10;

// Attack conversion (A1..A6). One shot settles exactly once.
inline int DuelAttackForPops(int pops)
{
    if (pops >= 12) return 5;
    if (pops >= 8)  return 3;
    if (pops >= 5)  return 2;
    if (pops >= 3)  return 1;
    return 0;
}

inline int DuelAttackForDrops(int drops)
{
    const int a = drops / 2;
    return a > 4 ? 4 : a;
}

int DuelAttackForShot(int pops, int drops);

// Descend cadence for a global shot count (T3/T4/T5).
int DuelDescendEvery(int globalShots);

// --------------------------------------------------------------- match state
enum DuelResult {
    kDuelRunning = 0,
    kDuelPlayerWin,
    kDuelPlayerLose,
    kDuelDraw
};

enum DuelSubPhase {
    kDuelTurnStart = 0,
    kDuelShooting,
    kDuelHandover,
    kDuelOver
};

// One settled shot, for the [shot] dump line and for the stats screen.
struct DuelShotLog {
    int globalShot;
    int side;
    int idx;        // shot index inside the turn (0-based)
    int landCol;
    int pops;
    int drops;
    int atk;
    int targetCol;
    int depth0;
    int depth1;
};

class DuelMatch {
public:
    void Reset(u32 seed);

    // Advance timers, the active bot and both boards. `playerBot` lets the
    // player side be driven by the same AI (used by --duel-sim / --autoplay).
    void Tick(float dt, bool playerBot);

    // Player input. Ignored unless it is the player's shooting phase.
    void AimAt(float mx, float my);
    void AimBy(float deltaRad);
    bool Fire();

    // ---- read-only state for the app and the HUD
    int  Turn() const           { return m_turn; }
    int  ShotsLeft() const      { return m_shotsLeft; }
    int  ShotsPerTurn() const   { return kShotsPerTurn; }
    int  SubPhase() const       { return m_sub; }
    int  GlobalShots() const    { return m_globalShots; }
    int  Result() const         { return m_result; }
    int  Depth(int side) const  { return m_depth[side]; }
    int  NextDescendIn() const;             // 4..1 for the active side
    int  AttackOut() const      { return m_attackOut; }
    int  AttackOutSide() const  { return m_attackOutSide; }
    int  Overflow() const       { return m_overflow; }
    int  MaxAttack(int side) const { return m_maxAtk[side]; }
    int  ColumnHits(int side, int col) const;
    const Game& Board(int side) const { return m_sides[side].game; }
    const Side& SideRef(int side) const { return m_sides[side]; }
    // The app hands this to the renderer; the boards are only read there.
    Side* Sides() { return m_sides; }
    int  ShotCount() const      { return (int)m_log.size(); }
    const DuelShotLog& ShotAt(int i) const { return m_log[i]; }

    // Set once per settled shot, cleared by the app when consumed.
    bool TakeAttackEvent(int* side, int* atk, int* col);

private:
    void beginTurn(int side);
    void setRowEvery();
    void settleShot(int side);
    void checkOver();

    Side m_sides[2];
    int  m_turn = 0;
    int  m_shotsLeft = kShotsPerTurn;
    int  m_sub = kDuelTurnStart;
    float m_timer = 0.0f;
    int  m_result = kDuelRunning;
    int  m_globalShots = 0;
    int  m_depth[2] = { 0, 0 };

    bool m_shotOpen[2] = { false, false };
    int  m_basePops[2] = { 0, 0 };
    int  m_baseDrops[2] = { 0, 0 };
    int  m_turnShotIdx = 0;

    int  m_attackOut = 0;
    int  m_attackOutSide = -1;
    int  m_attackEvent = 0;
    int  m_attackEventSide = -1;
    int  m_attackEventCol = 0;

    int  m_overflow = 0;
    int  m_maxAtk[2] = { 0, 0 };
    int  m_colHits[2][kDuelCols] = {{ 0 }};

    std::vector<DuelShotLog> m_log;
};

} // namespace bbl

#endif // BUBBLE_DUEL_H
