#ifndef BBL_GAME_H
#define BBL_GAME_H

// Puzzle-Bobble grid logic, no rendering, no platform dependency, so it can be
// exercised headless by --selftest.
//
// Grid: odd-r hexagonal offset. Row parity is derived from (row + shift); the
// shift toggles on every ceiling descent, which keeps existing bubbles on the
// same lattice x while a new row of the opposite parity is inserted on top.
//
// The board is positioned by Configure(): a battle has two independent boards
// side by side, each with its own origin and column count.

#include "hge_types.h"
#include <vector>

namespace bbl {

const float kD         = 40.0f;
const float kR         = 20.0f;
const float kRowH      = 34.6410161f;   // kD * sqrt(3)/2
const int   kColsMax   = 16;
const int   kMaxRows   = 26;
const float kTopY      = 28.0f;         // row 0 centre y
const float kCeilY     = 8.0f;          // ceiling surface
const float kDeathY    = 470.0f;
const float kShooterY  = 498.0f;
const float kSpeed     = 760.0f;        // px/s
const int   kColors    = 6;
const int   kMaxColors = 4;             // colours used per board
const int   kRowEvery  = 16;            // shots between ceiling descents
const int   kEnergyPerWave = 16;        // cleared bubbles per garbage wave
const int   kWaveSize  = 2;             // bubbles per wave
const int   kMaxQueued = 8;             // cap on pending garbage

// Garbage delivery. A whole attack arrives as one short surge instead of a slow
// drip: every bubble of a wave shares kRiserWindow, so nothing is still drifting
// in long after the hit landed.
const float kRiserWindow      = 0.24f;  // seconds from launch to contact
const float kRiserWarnSec     = 0.25f;  // red column warning before the surge
const float kRiserWaveGap     = 0.18f;  // gap before the 2nd surge of a big hit
const int   kGarbageCapTarget = 3;      // max garbage in the column that was hit
const int   kGarbageCapOther  = 2;      // max garbage in a spilled-to column

struct Particle {
    float x, y, vx, vy, t, life, scale;
    u32   color;
    int   kind;      // 0 = soft dot, 1 = expanding ring
};

struct Falling {
    float x, y, vy, t;
    int   color;
};

// A garbage bubble surging up from the bottom of a board until it sticks.
struct Riser {
    float x, y;
    int   color;
    int   col;      // column it will land in (drives the incoming warning)
    float warn;     // seconds left before it starts to rise
    float t;        // seconds it has been rising
    bool  hold;     // reached contact, waiting for a settled board
};

// Special payload carried by a shot bubble. Colours stay 1..kColors; the
// special is an extra property so the grid/resolve code does not change.
enum Special {
    kSpecialNone = 0,
    kSpecialBomb = 1,
    kSpecialPrism = 2,      // wildcard: adopts the colour it lands next to
    kSpecialChameleon = 3   // paints its six neighbours with its own colour
};

// One cell of a hand-authored stage board. LoadStage() replaces Reset()'s
// random starting rows with this list.
struct StageCell {
    int  r, c;
    int  color;
    bool corrupt;
};

class Game {
public:
    enum State { AIM, FLY, WIN, LOSE };

    Game();

    void Configure(int cols, float x0);
    void Reset(u32 seed = 0x51ED270Bu);
    void SetColors(int n);

    // Replace the random starting board with a fixed layout. Call after Reset()
    // so counters, queues and particles are already in a clean state.
    void LoadStage(int rows, int shift, const StageCell* cells, int count);
    void SetCorrupt(int r, int c);

    // Corrupted bubbles are the stage objective: they count as popped whether
    // the player matches them or drops them with their anchor.
    bool IsCorrupt(int r, int c) const;
    int  CorruptPopped() const { return m_corruptPopped; }
    int  CorruptTotal() const  { return m_corruptTotal; }

    // Test/screenshot helper: force the board into the LOSE state.
    void DebugLose() { m_state = LOSE; }

    // Scripted queue for the tutorial stage (colour 1..kColors).
    void SetQueue(int cur, int next) { m_cur = cur; m_next = next; }

    // Scripted bomb delivery for the tutorial and for "bomb start" mods.
    void SetBombAtMove(int move) { m_bombAtMove = move; }
    void SetBombEvery(int n) { m_bombEvery = n; }
    void SetPrismAtMove(int move) { m_prismAtMove = move; }
    void SetPrismEvery(int n) { m_prismEvery = n; }
    void SetChameleonAtMove(int move) { m_chamAtMove = move; }
    void SetChameleonEvery(int n) { m_chamEvery = n; }
    void GiveBombNow() { m_curSpecial = kSpecialBomb; }
    void GivePrismNow() { m_curSpecial = kSpecialPrism; }
    void GiveChameleonNow() { m_curSpecial = kSpecialChameleon; }
    void SetBombRadius(float r) { m_bombRadius = r; }
    void SetRiserSpeed(float s) { m_riserSpeed = s; }

    // ---- duel v2 -----------------------------------------------------
    // Rows from the top down to the lowest bubble (0 = empty board). The win
    // condition is DepthRows() >= kDepthMax, which is also what the HUD reads.
    int  DepthRows() const;
    int  ColumnDepth(int c) const;      // lowest bubble row in one column + 1
    int  DeepestColumn() const;         // the column with the lowest bubble
    // Column the last landed shot occupied (0-based), -1 before any shot.
    int  LastLandCol() const { return m_landC; }

    // Garbage aimed at one column, raising the queue cap overflow counter when
    // more is requested than kMaxQueued allows.
    void AddGarbageAt(int col, int count);
    int  OverflowCount() const { return m_overflow; }

    // Garbage still on its way up: the total, and how many are aimed at one
    // column. The renderer lights that column up before the surge arrives.
    int  IncomingCount() const { return (int)m_risers.size(); }
    int  IncomingIn(int col) const;

    // Ceiling descent cadence. Stage mode keeps the compile-time default; duel
    // changes it as the match speeds up.
    void SetRowEvery(int n) { m_rowEvery = (n < 1) ? 1 : n; }
    int  RowEvery() const { return m_rowEvery; }

    // Copy another board's cells column-mirrored (same rows / shift).
    void MirrorFrom(const Game& src);

    // Scripted one-shots beat repeating cycles, so the tutorial sequence is
    // never skipped by a draft mod.
    int  ScriptedSpecial(int move) const
    {
        if (m_bombAtMove >= 0 && move == m_bombAtMove) return kSpecialBomb;
        if (m_prismAtMove >= 0 && move == m_prismAtMove) return kSpecialPrism;
        if (m_chamAtMove >= 0 && move == m_chamAtMove) return kSpecialChameleon;
        if (m_bombEvery > 0 && (move + 1) % m_bombEvery == 0) return kSpecialBomb;
        if (m_prismEvery > 0 && (move + 1) % m_prismEvery == 0) return kSpecialPrism;
        if (m_chamEvery > 0 && (move + 1) % m_chamEvery == 0) return kSpecialChameleon;
        return kSpecialNone;
    }
    int  CurrentSpecial() const
    {
        const int s = ScriptedSpecial(m_moves);
        return s != kSpecialNone ? s : m_curSpecial;
    }
    int  NextSpecial() const
    {
        const int s = ScriptedSpecial(m_moves + 1);
        return s != kSpecialNone ? s : m_nextSpecial;
    }
    int  ShotSpecial() const { return m_shotSpecial; }

    void AimAt(float mx, float my);
    void AimBy(float deltaRad);
    void Fire();
    void Update(float dt);
    // Risers only. Duel hand-over banners freeze shots, not garbage that is
    // already on its way, so that path calls this instead of Update().
    void UpdateGarbage(float dt);
    // Cosmetics only: particle rings and falling debris. Deciding logic stays
    // in Update(); duel hand-over banners use this so an explosion in flight
    // keeps moving through the pause instead of hanging mid-frame.
    void TickCosmetic(float dt);

    State  GetState() const    { return m_state; }
    bool   IsFlying() const    { return m_state == FLY; }
    int    Cell(int r, int c) const;
    int    RowsUsed() const    { return m_rows; }
    int    RowCount(int r) const;
    float  XOf(int r, int c) const;
    static float YOf(int r)    { return kTopY + r * kRowH; }
    float  YOfLive(int r) const { return YOf(r) + m_ceilAnim - (m_ceilRowIn ? kRowH : 0.0f); }
    float  CeilingOffset() const { return m_ceilAnim; }

    float  X0() const          { return m_x0; }
    float  FieldW() const      { return m_cols * kD; }
    int    Cols() const        { return m_cols; }
    float  ShooterX() const    { return m_x0 + m_cols * kD * 0.5f; }

    int    Score() const       { return m_score; }
    int    Moves() const       { return m_moves; }
    int    MovesUntilRow() const;
    int    CurrentColor() const{ return m_cur; }
    int    NextColor() const   { return m_next; }
    int    Colors() const      { return m_colors; }
    int    BubblesLeft() const;
    int    Pops() const        { return m_pops; }
    int    Drops() const       { return m_drops; }
    int    Descends() const    { return m_descends; }
    float  IdleTime() const    { return m_idle; }
    float  SinceResolve() const{ return m_sinceRes; }
    int    LastPops() const    { return m_lastPops; }
    int    LastClear() const   { return m_lastClear; }

    float  AimAngle() const    { return m_angle; }
    float  ShotX() const       { return m_sx; }
    float  ShotY() const       { return m_sy; }
    int    ShotColor() const   { return m_sc; }

    bool   SolidAt(float x, float y) const { return HitBubble(x, y); }
    bool   HitBubbleCell(float x, float y, int* outR, int* outC) const;
    bool   Attachable(int r, int c) const { return IsAttachable(r, c); }
    int    CorruptNeighbors(int r, int c, int color) const;
    bool   PlantCorrupt();      // duel: seed one more attack bubble
    int    SameNeighbors(int r, int c, int color) const;

    // an empty board in versus mode just keeps going
    void   ResumeIfWon() { if (m_state == WIN) m_state = AIM; }

    // garbage attack
    void   AddGarbage(int count);
    const std::vector<Riser>& Risers() const { return m_risers; }

    const std::vector<Particle>& Particles() const { return m_parts; }
    const std::vector<Falling>&  FallingBubbles() const { return m_fall; }

    bool   SelfTest(char* msg, int cap);

private:
    u32  Rand() const { m_rand = m_rand * 1664525u + 1013904223u; return m_rand >> 8; }
    int  PickColor() const;
    int  Neighbors(int r, int c, int out[6][2]) const;
    bool IsAttachable(int r, int c) const;
    bool HitBubble(float x, float y) const;
    int  PrismColorAt(int r, int c) const;
    void Land(float x, float y, bool fromShot = true, int hitR = -1, int hitC = -1);
    void ExplodeAt(float x, float y, bool fromShot);
    void Descend();
    void AdvanceCeiling();
    void Resolve(int r, int c);
    void DropFloating();
    bool GridEmpty() const;
    bool OverLine() const;
    int  PushGarbage(const int* cols, int count);
    void PickGarbageColumns(int target, int count, int* out);
    void SpawnPop(float x, float y, int color, int count);
    void spawnRing(float x, float y, u32 color, float scale, float life = 0.28f);
    // White core plus a wider ring, both centred on the same point, so a clear
    // or a bomb reads as one blast instead of a lone ring drifting away.
    void spawnBlast(float x, float y, u32 outer);

    u8  m_cell[kMaxRows][kColsMax];
    bool m_corrupt[kMaxRows][kColsMax];
    int m_rows;
    int m_shift;
    int m_cols;
    float m_x0;
    int m_colors;
    int m_score;
    int m_moves;
    int m_pops;
    int m_lastPops;
    int m_lastClear;
    int m_drops;
    int m_descends;
    int m_corruptPopped;
    int m_corruptTotal;
    float m_idle;
    float m_sinceRes;
    int m_cur, m_next;
    int m_curSpecial, m_nextSpecial, m_shotSpecial;
    int m_bombAtMove;
    int m_bombEvery;
    int m_prismAtMove;
    int m_prismEvery;
    int m_chamAtMove;
    int m_chamEvery;
    float m_bombRadius;
    float m_riserSpeed;
    int   m_rowEvery;
    int   m_overflow;
    int   m_landC;
    State m_state;
    float m_angle;
    mutable u32 m_rand;

    float m_sx, m_sy, m_dirx, m_diry;
    int   m_sc;
    float m_ceil;
    float m_ceilAnim;
    bool  m_ceilRowIn;   // the row for the current descent cycle is already in

    std::vector<Particle> m_parts;
    std::vector<Falling>  m_fall;
    std::vector<Riser>    m_risers;
};

} // namespace bbl

#endif // BBL_GAME_H
