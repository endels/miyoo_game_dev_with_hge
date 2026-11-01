#include "app/bubble/game.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace bbl {

namespace {
const float kHitDist2 = kD * kD * 0.90f;   // 37.9px: touching but not overlapping
const float kPi = 3.14159265359f;
}

Game::Game()
{
    m_colors = kMaxColors;      // must be valid before the first Reset()
    Configure(10, 0.0f);
    Reset();
}

void Game::Configure(int cols, float x0)
{
    if (cols < 4) cols = 4;
    if (cols > kColsMax) cols = kColsMax;
    m_cols = cols;
    m_x0 = x0;
}

void Game::SetColors(int n)
{
    if (n < 1) n = 1;
    if (n > kMaxColors) n = kMaxColors;
    m_colors = n;
}

void Game::Reset(u32 seed)
{
    memset(m_cell, 0, sizeof(m_cell));
    m_rows  = 4;
    m_shift = 0;
    m_score = 0;
    m_moves = 0;
    m_pops = 0;
    m_lastPops = 0;
    m_idle = 0.0f;
    m_sinceRes = 10.0f;
    m_drops = 0;
    m_descends = 0;
    m_state = AIM;
    m_angle = -kPi * 0.5f;
    m_rand  = seed ? seed : 1u;
    m_parts.clear();
    m_fall.clear();
    m_risers.clear();

    if (m_colors <= 0) m_colors = kMaxColors;

    m_sx = ShooterX();
    m_sy = kShooterY;
    m_dirx = 0.0f; m_diry = -1.0f; m_sc = 1;
    m_ceil = 0.0f;
    m_ceilAnim = 0.0f;

    for (int r = 0; r < m_rows; ++r) {
        const int n = RowCount(r);
        int p1 = 0, p2 = 0;
        for (int c = 0; c < n; ++c) {
            int col = 1 + (int)(Rand() % m_colors);
            int guard = 0;
            while (guard++ < 24 && col == p1 && col == p2)
                col = 1 + (int)(Rand() % m_colors);
            m_cell[r][c] = (u8)col;
            p2 = p1; p1 = col;
        }
    }
    m_cur  = PickColor();
    m_next = PickColor();
}

int Game::RowCount(int r) const
{
    if (r < 0 || r >= kMaxRows) return 0;
    return ((r + m_shift) & 1) ? (m_cols - 1) : m_cols;
}

float Game::XOf(int r, int c) const
{
    const float off = ((r + m_shift) & 1) ? kR : 0.0f;
    return m_x0 + kR + c * kD + off;
}

int Game::Cell(int r, int c) const
{
    if (r < 0 || r >= kMaxRows || c < 0 || c >= RowCount(r)) return 0;
    return m_cell[r][c];
}

int Game::BubblesLeft() const
{
    int n = 0;
    for (int r = 0; r < m_rows; ++r)
        for (int c = 0; c < RowCount(r); ++c)
            if (m_cell[r][c]) ++n;
    return n;
}

int Game::MovesUntilRow() const
{
    if (kRowEvery <= 0) return 0;
    return kRowEvery - (m_moves % kRowEvery);
}

int Game::PickColor() const
{
    int present[kColors + 1] = {0};
    int n = 0;
    for (int r = 0; r < m_rows; ++r)
        for (int c = 0; c < RowCount(r); ++c) {
            const int v = m_cell[r][c];
            if (!v || v > kColors) continue;
            bool have = false;
            for (int i = 0; i < n; ++i) if (present[i] == v) { have = true; break; }
            if (!have && n < kColors) present[n++] = v;
        }
    if (n == 0) return 1 + (int)((m_rand = m_rand * 1664525u + 1013904223u) >> 8) % (u32)m_colors;
    const u32 r = (m_rand = m_rand * 1664525u + 1013904223u) >> 8;
    return present[r % (u32)n];
}

int Game::Neighbors(int r, int c, int out[6][2]) const
{
    const int p = (r + m_shift) & 1;
    const int cand[6][2] = {
        { r,     c - 1 },
        { r,     c + 1 },
        { r - 1, c + (p ? 0 : -1) },
        { r - 1, c + (p ? 1 : 0) },
        { r + 1, c + (p ? 0 : -1) },
        { r + 1, c + (p ? 1 : 0) }
    };
    int n = 0;
    for (int i = 0; i < 6; ++i) {
        const int nr = cand[i][0], nc = cand[i][1];
        if (nr < 0 || nr >= kMaxRows) continue;
        if (nc < 0 || nc >= RowCount(nr)) continue;
        out[n][0] = nr; out[n][1] = nc; ++n;
    }
    return n;
}

int Game::SameNeighbors(int r, int c, int color) const
{
    int nb[6][2];
    const int n = Neighbors(r, c, nb);
    int k = 0;
    for (int i = 0; i < n; ++i)
        if (m_cell[nb[i][0]][nb[i][1]] == color) ++k;
    return k;
}

bool Game::IsAttachable(int r, int c) const
{
    if (r == 0) return true;
    int nb[6][2];
    const int n = Neighbors(r, c, nb);
    for (int i = 0; i < n; ++i)
        if (m_cell[nb[i][0]][nb[i][1]]) return true;
    return false;
}

void Game::AimAt(float mx, float my)
{
    float dx = mx - ShooterX();
    float dy = my - kShooterY;
    if (dy > -12.0f) dy = -12.0f;
    float a = atan2f(dy, dx);
    if (a > -0.16f) a = -0.16f;
    if (a < -kPi + 0.16f) a = -kPi + 0.16f;
    m_angle = a;
}

void Game::AimBy(float d)
{
    m_angle += d;
    if (m_angle > -0.16f) m_angle = -0.16f;
    if (m_angle < -kPi + 0.16f) m_angle = -kPi + 0.16f;
}

void Game::Fire()
{
    if (m_state != AIM) return;
    m_sx = ShooterX();
    m_sy = kShooterY;
    m_dirx = cosf(m_angle);
    m_diry = sinf(m_angle);
    m_sc = m_cur;
    m_state = FLY;
    m_idle = 0.0f;
    ++m_moves;
    m_cur = m_next;
    m_next = PickColor();
}

bool Game::HitBubble(float x, float y) const
{
    for (int r = 0; r < m_rows && r < kMaxRows; ++r) {
        const int n = RowCount(r);
        const float dy = YOfLive(r) - y;
        for (int c = 0; c < n; ++c) {
            if (!m_cell[r][c]) continue;
            const float dx = XOf(r, c) - x;
            if (dx * dx + dy * dy < kHitDist2) return true;
        }
    }
    return false;
}

void Game::AddGarbage(int count)
{
    for (int i = 0; i < count; ++i) {
        if ((int)m_risers.size() >= kMaxQueued) break;
        Riser r;
        const int col = (int)(Rand() % (u32)m_cols);
        r.x = m_x0 + kR + col * kD;
        r.y = 560.0f + (float)(m_risers.size() % 6) * 46.0f;
        r.color = 1 + (int)(Rand() % (u32)m_colors);
        m_risers.push_back(r);
    }
}

void Game::Update(float dt)
{
    m_idle += dt;
    m_sinceRes += dt;

    // The ceiling creeps down in 1/kRowEvery steps per shot; smooth the
    // logical offset for display/collision so it never snaps.
    if (m_ceilAnim != m_ceil) {
        float k = dt * 16.0f;
        if (k > 1.0f) k = 1.0f;
        m_ceilAnim += (m_ceil - m_ceilAnim) * k;
        if (fabsf(m_ceil - m_ceilAnim) < 0.02f) m_ceilAnim = m_ceil;
    }

    for (size_t i = 0; i < m_parts.size();) {
        Particle& p = m_parts[i];
        p.t += dt;
        if (p.t >= p.life) { m_parts[i] = m_parts.back(); m_parts.pop_back(); continue; }
        if (p.kind == 0) {
            p.x += p.vx * dt;
            p.y += p.vy * dt;
            p.vy += 260.0f * dt;
            p.vx *= 1.0f - 1.5f * dt;
        }
        ++i;
    }

    for (size_t i = 0; i < m_fall.size();) {
        Falling& f = m_fall[i];
        f.vy += 1200.0f * dt;
        f.y += f.vy * dt;
        f.t += dt;
        if (f.y > 620.0f) { m_fall[i] = m_fall.back(); m_fall.pop_back(); continue; }
        ++i;
    }

    // garbage drifting up from the bottom
    for (size_t i = 0; i < m_risers.size();) {
        Riser& r = m_risers[i];
        // Fast, in small steps, so it reads as an attack surge and cannot
        // tunnel past a bubble between frames.
        float move = 800.0f * dt;
        bool landed = false;
        while (move > 0.0f && !landed) {
            const float s = move < 6.0f ? move : 6.0f;
            move -= s;
            r.y -= s;
            if ((r.y - kR <= kCeilY) || HitBubble(r.x, r.y)) landed = true;
        }
        if (landed) {
            m_sc = r.color;
            m_state = AIM;
            spawnRing(r.x, r.y, 0xFFFFD08Au, 1.6f);
            Land(r.x, r.y, false);
            m_risers[i] = m_risers.back();
            m_risers.pop_back();
            continue;
        }
        ++i;
    }

    if (m_state != FLY) return;

    float remain = kSpeed * dt;
    while (remain > 0.0f) {
        const float s = remain < 2.0f ? remain : 2.0f;
        remain -= s;
        m_sx += m_dirx * s;
        m_sy += m_diry * s;

        if (m_sx < m_x0 + kR)          { m_sx = m_x0 + kR;          m_dirx = -m_dirx; }
        if (m_sx > m_x0 + FieldW() - kR) { m_sx = m_x0 + FieldW() - kR; m_dirx = -m_dirx; }

        if (m_sy - kR <= kCeilY) { Land(m_sx, m_sy); return; }
        if (HitBubble(m_sx, m_sy)) { Land(m_sx, m_sy); return; }
    }
}

void Game::Land(float x, float y, bool fromShot)
{
    int br = -1, bc = -1;
    float best = 1e30f;

    for (int pass = 0; pass < 2 && br < 0; ++pass) {
        for (int r = 0; r <= m_rows && r < kMaxRows; ++r) {
            const int n = RowCount(r);
            for (int c = 0; c < n; ++c) {
                if (m_cell[r][c]) continue;
                if (pass == 0 && !IsAttachable(r, c)) continue;
                const float dx = XOf(r, c) - x;
                const float dy = YOf(r) - y;
                const float d2 = dx * dx + dy * dy;
                if (d2 < best) { best = d2; br = r; bc = c; }
            }
        }
        best = 1e30f;
    }

    if (br < 0) { m_state = LOSE; return; }

    m_cell[br][bc] = (u8)m_sc;
    if (br + 1 > m_rows) m_rows = br + 1;
    m_state = AIM;
    m_sinceRes = 0.0f;

    Resolve(br, bc);

    if (GridEmpty()) { m_state = WIN; return; }
    if (fromShot && m_moves > 0) AdvanceCeiling();
    if (m_state != LOSE && OverLine()) m_state = LOSE;
}

void Game::Resolve(int r, int c)
{
    const int col = m_cell[r][c];
    if (!col) return;
    m_lastPops = 0;

    bool seen[kMaxRows][kColsMax];
    memset(seen, 0, sizeof(seen));

    int stack[kMaxRows * kColsMax][2];
    int list[kMaxRows * kColsMax][2];
    int sp = 0, n = 0;

    seen[r][c] = true;
    stack[sp][0] = r; stack[sp][1] = c; ++sp;

    while (sp > 0) {
        --sp;
        const int cr = stack[sp][0], cc = stack[sp][1];
        list[n][0] = cr; list[n][1] = cc; ++n;

        int nb[6][2];
        const int nn = Neighbors(cr, cc, nb);
        for (int i = 0; i < nn; ++i) {
            const int nr = nb[i][0], nc = nb[i][1];
            if (!seen[nr][nc] && m_cell[nr][nc] == col) {
                seen[nr][nc] = true;
                stack[sp][0] = nr; stack[sp][1] = nc; ++sp;
            }
        }
    }

    if (n < 3) return;

    for (int i = 0; i < n; ++i) {
        const int cr = list[i][0], cc = list[i][1];
        SpawnPop(XOf(cr, cc), YOfLive(cr), m_cell[cr][cc], 5);
        m_cell[cr][cc] = 0;
    }
    m_score += n * 10;
    m_pops += n;
    m_lastPops = n;
    spawnRing(XOf(r, c), YOfLive(r), 0xFFFFFFFFu, 2.2f);

    DropFloating();
}

void Game::DropFloating()
{
    bool seen[kMaxRows][kColsMax];
    memset(seen, 0, sizeof(seen));

    int stack[kMaxRows * kColsMax][2];
    int sp = 0;

    const int n0 = RowCount(0);
    for (int c = 0; c < n0; ++c) {
        if (m_cell[0][c]) { seen[0][c] = true; stack[sp][0] = 0; stack[sp][1] = c; ++sp; }
    }

    while (sp > 0) {
        --sp;
        const int cr = stack[sp][0], cc = stack[sp][1];
        int nb[6][2];
        const int nn = Neighbors(cr, cc, nb);
        for (int i = 0; i < nn; ++i) {
            const int nr = nb[i][0], nc = nb[i][1];
            if (m_cell[nr][nc] && !seen[nr][nc]) {
                seen[nr][nc] = true;
                stack[sp][0] = nr; stack[sp][1] = nc; ++sp;
            }
        }
    }

    for (int r = 0; r < m_rows; ++r) {
        const int n = RowCount(r);
        for (int c = 0; c < n; ++c) {
            if (!m_cell[r][c] || seen[r][c]) continue;
            Falling f;
            f.x = XOf(r, c);
            f.y = YOfLive(r);
            f.vy = 40.0f;
            f.t = 0.0f;
            f.color = m_cell[r][c];
            m_fall.push_back(f);
            m_cell[r][c] = 0;
            m_score += 20;
            ++m_drops;
        }
    }
}

void Game::Descend()
{
    if (m_rows + 1 > kMaxRows) { m_state = LOSE; return; }

    for (int r = kMaxRows - 1; r >= 1; --r)
        memcpy(m_cell[r], m_cell[r - 1], sizeof(m_cell[0]));
    memset(m_cell[0], 0, sizeof(m_cell[0]));

    m_shift ^= 1;
    ++m_rows;

    const int n = RowCount(0);
    for (int c = 0; c < n; ++c)
        m_cell[0][c] = (u8)(1 + (int)(Rand() % (u32)m_colors));

    spawnRing(m_x0 + FieldW() * 0.5f, kCeilY + 6.0f, 0xFF8090FFu, 6.0f);
}

void Game::AdvanceCeiling()
{
    m_ceil += kRowH / (float)kRowEvery;
    if (m_ceil >= kRowH) {
        m_ceil -= kRowH;
        Descend();
        ++m_descends;
        m_ceilAnim -= kRowH;
        if (m_ceilAnim < 0.0f) m_ceilAnim = 0.0f;
    }
}

bool Game::GridEmpty() const
{
    for (int r = 0; r < m_rows; ++r) {
        const int n = RowCount(r);
        for (int c = 0; c < n; ++c)
            if (m_cell[r][c]) return false;
    }
    return true;
}

bool Game::OverLine() const
{
    for (int r = 0; r < m_rows; ++r) {
        const int n = RowCount(r);
        for (int c = 0; c < n; ++c) {
            if (!m_cell[r][c]) continue;
            if (YOfLive(r) + kR >= kDeathY) return true;
        }
    }
    return false;
}

void Game::SpawnPop(float x, float y, int color, int count)
{
    for (int i = 0; i < count; ++i) {
        const float a = (float)(Rand() % 628) * 0.01f;
        const float sp = 60.0f + (float)(Rand() % 140);
        Particle p;
        p.x = x; p.y = y;
        p.vx = cosf(a) * sp;
        p.vy = sinf(a) * sp - 40.0f;
        p.t = 0.0f;
        p.life = 0.30f + (float)(Rand() % 30) * 0.01f;
        p.scale = 0.25f + (float)(Rand() % 25) * 0.01f;
        p.kind = 0;
        p.color = 0xFFFFFFFFu;
        (void)color;
        m_parts.push_back(p);
    }
}

void Game::spawnRing(float x, float y, u32 color, float scale)
{
    Particle p;
    p.x = x; p.y = y;
    p.vx = 0; p.vy = 0;
    p.t = 0.0f;
    p.life = 0.28f;
    p.scale = scale;
    p.kind = 1;
    p.color = color;
    m_parts.push_back(p);
}

bool Game::SelfTest(char* msg, int cap)
{
    Reset(0xB0BB1E01u);

    int shots = 0;
    char err[200] = {0};
    bool ok = true;

    for (int i = 0; i < 400 && m_state != WIN && m_state != LOSE; ++i) {
        const float a = -3.0f + 2.8f * ((float)(Rand() % 1000) / 1000.0f);
        AimAt(ShooterX() + cosf(a) * 300.0f, kShooterY + sinf(a) * 300.0f);
        Fire();
        ++shots;

        int guard = 0;
        while (m_state == FLY && guard++ < 4000) Update(1.0f / 240.0f);
        if (m_state == FLY) {
            ok = false;
            snprintf(err, sizeof(err), "shot %d never landed", shots);
            break;
        }

        for (int r = 0; r < kMaxRows && ok; ++r) {
            const int n = RowCount(r);
            for (int c = 0; c < n; ++c) {
                const int v = m_cell[r][c];
                if (v && (v < 1 || v > kColors)) {
                    ok = false;
                    snprintf(err, sizeof(err), "bad colour r%d c%d=%d", r, c, v);
                    break;
                }
                if (v && r >= m_rows) {
                    ok = false;
                    snprintf(err, sizeof(err), "cell beyond rows r%d", r);
                    break;
                }
            }
        }
        if (!ok) break;

        bool seen[kMaxRows][kColsMax];
        memset(seen, 0, sizeof(seen));
        int st[kMaxRows * kColsMax][2];
        int sp = 0;
        const int n0 = RowCount(0);
        for (int c = 0; c < n0; ++c)
            if (m_cell[0][c]) { seen[0][c] = true; st[sp][0] = 0; st[sp][1] = c; ++sp; }
        while (sp > 0) {
            --sp;
            const int cr = st[sp][0], cc = st[sp][1];
            int nb[6][2];
            const int nn = Neighbors(cr, cc, nb);
            for (int k = 0; k < nn; ++k) {
                const int nr = nb[k][0], nc = nb[k][1];
                if (m_cell[nr][nc] && !seen[nr][nc]) {
                    seen[nr][nc] = true;
                    st[sp][0] = nr; st[sp][1] = nc; ++sp;
                }
            }
        }
        for (int r = 0; r < m_rows && ok; ++r) {
            const int n = RowCount(r);
            for (int c = 0; c < n; ++c) {
                if (m_cell[r][c] && !seen[r][c]) {
                    ok = false;
                    snprintf(err, sizeof(err), "floating bubble r%d c%d", r, c);
                    break;
                }
            }
        }
        if (!ok) break;
    }

    const int   soakShots = shots;
    const int   soakRows  = m_rows;
    const int   soakLeft  = BubblesLeft();
    const State soakState = m_state;

    // ---- targeted logic checks (deterministic boards) ------------------
    Reset(7);
    memset(m_cell, 0, sizeof(m_cell));
    m_rows = 3; m_shift = 0; m_state = AIM; m_moves = 0;
    m_cell[2][2] = 1;
    m_cell[2][3] = 1;
    m_sc = 1;
    Land(XOf(2, 4), YOf(2) + 3.0f);
    if (Cell(2, 2) || Cell(2, 3) || Cell(2, 4)) {
        ok = false;
        snprintf(err, sizeof(err), "match-3 did not clear");
    } else if (m_state != WIN) {
        ok = false;
        snprintf(err, sizeof(err), "empty board did not report WIN");
    }

    Reset(9);
    memset(m_cell, 0, sizeof(m_cell));
    m_rows = 6; m_shift = 0; m_state = AIM; m_moves = 0;
    m_cell[0][0] = 2;
    m_cell[0][1] = 2;
    m_cell[3][5] = 3;
    m_cell[3][6] = 3;
    m_sc = 2;
    Land(XOf(0, 2), YOf(0));
    if (ok && (Cell(3, 5) || Cell(3, 6))) {
        ok = false;
        snprintf(err, sizeof(err), "floating bubbles not dropped");
    }

    Reset(11);
    memset(m_cell, 0, sizeof(m_cell));
    m_rows = 2; m_shift = 0; m_state = AIM; m_moves = 0;
    m_cell[0][0] = 4;
    m_sc = 5;
    Land(XOf(1, 0), YOf(1));
    if (ok && Cell(1, 0) != 5) {
        ok = false;
        snprintf(err, sizeof(err), "single bubble did not stick");
    }

    // garbage must rise and stick without advancing the ceiling
    if (ok) {
        Reset(13);
        memset(m_cell, 0, sizeof(m_cell));
        m_rows = 1; m_shift = 0; m_state = AIM; m_moves = 0;
        m_cell[0][4] = 2;
        const int before = BubblesLeft();
        AddGarbage(1);
        int guard = 0;
        while (!m_risers.empty() && guard++ < 4000) Update(1.0f / 120.0f);
        if (!m_risers.empty()) {
            ok = false;
            snprintf(err, sizeof(err), "garbage never landed");
        } else if (BubblesLeft() != before + 1) {
            ok = false;
            snprintf(err, sizeof(err), "garbage did not stick (%d -> %d)",
                     before, BubblesLeft());
        }
    }

    if (ok && soakState == WIN && soakLeft != 0) {
        ok = false;
        snprintf(err, sizeof(err), "WON with bubbles left");
    }
    if (ok && soakState == AIM) {
        ok = false;
        snprintf(err, sizeof(err), "still aiming after 400 shots");
    }

    if (msg) {
        snprintf(msg, cap, "shots=%d rows=%d left=%d state=%s %s",
                 soakShots, soakRows, soakLeft,
                 soakState == WIN ? "WIN" : (soakState == LOSE ? "LOSE" : "AIM"),
                 ok ? "ok" : err);
    }
    return ok;
}

} // namespace bbl
