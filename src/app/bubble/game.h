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

struct Particle {
    float x, y, vx, vy, t, life, scale;
    u32   color;
    int   kind;      // 0 = soft dot, 1 = expanding ring
};

struct Falling {
    float x, y, vy, t;
    int   color;
};

// A garbage bubble drifting up from the bottom of a board until it sticks.
struct Riser {
    float x, y;
    int   color;
};

class Game {
public:
    enum State { AIM, FLY, WIN, LOSE };

    Game();

    void Configure(int cols, float x0);
    void Reset(u32 seed = 0x51ED270Bu);
    void SetColors(int n);

    void AimAt(float mx, float my);
    void AimBy(float deltaRad);
    void Fire();
    void Update(float dt);

    State  GetState() const    { return m_state; }
    bool   IsFlying() const    { return m_state == FLY; }
    int    Cell(int r, int c) const;
    int    RowsUsed() const    { return m_rows; }
    int    RowCount(int r) const;
    float  XOf(int r, int c) const;
    static float YOf(int r)    { return kTopY + r * kRowH; }
    float  YOfLive(int r) const { return YOf(r) + m_ceilAnim; }
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

    float  AimAngle() const    { return m_angle; }
    float  ShotX() const       { return m_sx; }
    float  ShotY() const       { return m_sy; }
    int    ShotColor() const   { return m_sc; }

    bool   SolidAt(float x, float y) const { return HitBubble(x, y); }
    bool   Attachable(int r, int c) const { return IsAttachable(r, c); }
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
    void Land(float x, float y, bool fromShot = true);
    void Descend();
    void AdvanceCeiling();
    void Resolve(int r, int c);
    void DropFloating();
    bool GridEmpty() const;
    bool OverLine() const;
    void SpawnPop(float x, float y, int color, int count);
    void spawnRing(float x, float y, u32 color, float scale);

    u8  m_cell[kMaxRows][kColsMax];
    int m_rows;
    int m_shift;
    int m_cols;
    float m_x0;
    int m_colors;
    int m_score;
    int m_moves;
    int m_pops;
    int m_lastPops;
    int m_drops;
    int m_descends;
    float m_idle;
    float m_sinceRes;
    int m_cur, m_next;
    State m_state;
    float m_angle;
    mutable u32 m_rand;

    float m_sx, m_sy, m_dirx, m_diry;
    int   m_sc;
    float m_ceil;
    float m_ceilAnim;

    std::vector<Particle> m_parts;
    std::vector<Falling>  m_fall;
    std::vector<Riser>    m_risers;
};

} // namespace bbl

#endif // BBL_GAME_H
