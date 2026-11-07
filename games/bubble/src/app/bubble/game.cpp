#include "app/bubble/game.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace bbl {

namespace {
// Flight collision radius. Touching bubbles sit exactly kD apart, so a gap
// between two bubbles is only kD wide and a full-diameter test demands a
// pixel-perfect shot. Shrinking the effective radius gives the player a few
// pixels of slack on each side, which reads as "it squeezed through" instead
// of sticking on a graze. 0.86 -> ~5.6px of forgiveness per side.
const float kStickScale = 0.86f;
const float kHitDist2    = kD * kD * kStickScale * kStickScale;
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

void Game::LoadStage(int rows, int shift, const StageCell* cells, int count)
{
    if (rows < 1) rows = 1;
    if (rows > kMaxRows) rows = kMaxRows;
    memset(m_cell, 0, sizeof(m_cell));
    memset(m_corrupt, 0, sizeof(m_corrupt));
    m_rows = rows;
    m_shift = shift & 1;
    m_corruptPopped = 0;
    m_corruptTotal = 0;
    for (int i = 0; i < count; ++i) {
        const StageCell& s = cells[i];
        if (s.r < 0 || s.r >= m_rows || s.c < 0 || s.c >= RowCount(s.r)) continue;
        m_cell[s.r][s.c] = (u8)s.color;
        if (s.corrupt) {
            if (!m_corrupt[s.r][s.c]) ++m_corruptTotal;
            m_corrupt[s.r][s.c] = true;
        }
    }
}

void Game::SetCorrupt(int r, int c)
{
    if (r < 0 || r >= kMaxRows || c < 0 || c >= RowCount(r)) return;
    if (!m_corrupt[r][c]) {
        m_corrupt[r][c] = true;
        ++m_corruptTotal;
    }
}

bool Game::IsCorrupt(int r, int c) const
{
    if (r < 0 || r >= kMaxRows || c < 0 || c >= RowCount(r)) return false;
    return m_corrupt[r][c];
}



void Game::Reset(u32 seed)
{
    memset(m_cell, 0, sizeof(m_cell));
    memset(m_corrupt, 0, sizeof(m_corrupt));
    m_rows  = 4;
    m_shift = 0;
    m_score = 0;
    m_moves = 0;
    m_pops = 0;
    m_lastPops = 0;
    m_lastClear = 0;
    m_idle = 0.0f;
    m_sinceRes = 10.0f;
    m_drops = 0;
    m_descends = 0;
    m_corruptPopped = 0;
    m_corruptTotal = 0;
    m_bombAtMove = -1;
    m_bombEvery = 0;
    m_prismAtMove = -1;
    m_prismEvery = 0;
    m_chamAtMove = -1;
    m_chamEvery = 0;
    m_bombRadius = 1.5f;
    m_riserSpeed = 1.0f;
    m_rowEvery = kRowEvery;
    m_overflow = 0;
    m_landC = -1;
    m_curSpecial = kSpecialNone;
    m_nextSpecial = kSpecialNone;
    m_shotSpecial = kSpecialNone;
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
    m_ceilRowIn = false;

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
    if (m_rowEvery <= 0) return 0;
    return m_rowEvery - (m_moves % m_rowEvery);
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

int Game::CorruptNeighbors(int r, int c, int color) const
{
    int nb[6][2];
    const int n = Neighbors(r, c, nb);
    int k = 0;
    for (int i = 0; i < n; ++i) {
        const int nr = nb[i][0], nc = nb[i][1];
        if (m_cell[nr][nc] == color && m_corrupt[nr][nc]) ++k;
    }
    return k;
}

bool Game::PlantCorrupt()
{
    int cellR[kMaxRows * kColsMax];
    int cellC[kMaxRows * kColsMax];
    int n = 0;
    for (int r = 0; r < m_rows && r < kMaxRows; ++r) {
        const int cols = RowCount(r);
        for (int c = 0; c < cols; ++c) {
            if (!m_cell[r][c] || m_corrupt[r][c]) continue;
            cellR[n] = r;
            cellC[n] = c;
            ++n;
        }
    }
    if (n == 0) return false;
    const int pick = (int)(Rand() % (u32)n);
    m_corrupt[cellR[pick]][cellC[pick]] = true;
    ++m_corruptTotal;
    spawnRing(XOf(cellR[pick], cellC[pick]), YOfLive(cellR[pick]),
              0xFFC080FFu, 2.2f);
    return true;
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
    m_shotSpecial = CurrentSpecial();
    m_state = FLY;
    m_idle = 0.0f;
    ++m_moves;
    m_cur = m_next;
    m_curSpecial = m_nextSpecial;
    m_next = PickColor();
    m_nextSpecial = kSpecialNone;
}

bool Game::HitBubbleCell(float x, float y, int* outR, int* outC) const
{
    // Returns the *closest* bubble in contact: the landing cell is picked from
    // its empty neighbours, so the ball cannot snap across the board.
    float best = kHitDist2;
    int br = -1, bc = -1;
    for (int r = 0; r < m_rows && r < kMaxRows; ++r) {
        const int n = RowCount(r);
        const float dy = YOfLive(r) - y;
        for (int c = 0; c < n; ++c) {
            if (!m_cell[r][c]) continue;
            const float dx = XOf(r, c) - x;
            const float d2 = dx * dx + dy * dy;
            if (d2 < best) { best = d2; br = r; bc = c; }
        }
    }
    if (outR) *outR = br;
    if (outC) *outC = bc;
    return br >= 0;
}

bool Game::HitBubble(float x, float y) const
{
    return HitBubbleCell(x, y, nullptr, nullptr);
}

// Column mix for one attack: most of it lands where the shot landed, the rest
// spills to +-1/+-2, so a big hit presses one column instead of building a
// single tower. Per-column caps and a "never the same column twice in a row"
// rule keep the rising bubbles from stacking into one straight line.
void Game::PickGarbageColumns(int target, int count, int* out)
{
    int have[kColsMax] = { 0 };
    int prev = -1;
    for (int i = 0; i < count; ++i) {
        int chosen = -1;
        // pass 0: all rules, 1: allow repeating the last column, 2: ignore caps
        for (int pass = 0; pass < 3 && chosen < 0; ++pass) {
            static const int off[5] = { 0, -1, 1, -2, 2 };
            static const int wgt[5] = { 6, 2, 2, 1, 1 };   // 60% on the hit column
            int cand[5] = { -1, -1, -1, -1, -1 };
            int wsum = 0;
            for (int k = 0; k < 5; ++k) {
                const int c = target + off[k];
                if (c < 0 || c >= m_cols) continue;
                if (pass < 2 && c == prev) continue;
                const int cap = (pass < 2)
                    ? ((c == target) ? kGarbageCapTarget : kGarbageCapOther)
                    : kMaxQueued;
                if (have[c] >= cap) continue;
                cand[k] = c;
                wsum += wgt[k];
            }
            if (wsum <= 0) continue;
            int r = (int)(Rand() % (u32)wsum);
            for (int k = 0; k < 5; ++k) {
                if (cand[k] < 0) continue;
                if (r < wgt[k]) { chosen = cand[k]; break; }
                r -= wgt[k];
            }
        }
        if (chosen < 0) chosen = target;        // cramped board: never lose one
        out[i] = chosen;
        ++have[chosen];
        prev = chosen;
    }
}

// Queue the bubbles for `cols` (nullptr = random columns). The whole batch gets
// the same short window, a warning lead-in, and big hits are split in two
// waves. Returns how many did not fit in the queue.
int Game::PushGarbage(const int* cols, int count)
{
    int dropped = 0;
    const int firstWave = (count >= 6) ? (count + 1) / 2 : count;

    for (int i = 0; i < count; ++i) {
        if ((int)m_risers.size() >= kMaxQueued) { dropped = count - i; break; }
        Riser r;
        r.col   = cols ? cols[i] : (int)(Rand() % (u32)m_cols);
        r.x     = m_x0 + kR + (float)r.col * kD;
        // Start just under the field so the wave comes in from the bottom edge,
        // with a little jitter so it never reads as one solid bar.
        r.y     = 566.0f + (float)(i % 3) * 13.0f + (float)(Rand() % 41) - 20.0f;
        r.color = 1 + (int)(Rand() % (u32)m_colors);
        r.warn  = kRiserWarnSec + (i < firstWave ? 0.0f : kRiserWaveGap);
        r.warn += 0.02f * (float)(i % 5);       // <= 0.08 s of stagger
        r.t     = 0.0f;
        r.hold  = false;
        m_risers.push_back(r);
    }
    return dropped;
}

void Game::AddGarbage(int count)
{
    if (count <= 0) return;
    PushGarbage(nullptr, count);        // stage mode: random columns, no A11 count
}

void Game::AddGarbageAt(int col, int count)
{
    if (count <= 0) return;
    if (col < 0) col = 0;
    if (col >= m_cols) col = m_cols - 1;

    int cols[kMaxQueued];
    int queued = count;
    if (queued > kMaxQueued) queued = kMaxQueued;
    PickGarbageColumns(col, queued, cols);
    m_overflow += PushGarbage(cols, count);
}

int Game::DepthRows() const
{
    for (int r = m_rows - 1; r >= 0; --r) {
        const int n = RowCount(r);
        for (int c = 0; c < n; ++c)
            if (m_cell[r][c]) return r + 1;
    }
    return 0;
}

int Game::ColumnDepth(int col) const
{
    if (col < 0 || col >= m_cols) return 0;
    for (int r = m_rows - 1; r >= 0; --r) {
        const int n = RowCount(r);
        if (col < n && m_cell[r][col]) return r + 1;
    }
    return 0;
}

int Game::IncomingIn(int col) const
{
    int n = 0;
    for (size_t i = 0; i < m_risers.size(); ++i)
        if (m_risers[i].col == col) ++n;
    return n;
}

int Game::DeepestColumn() const
{
    int best = 0, bestCol = 0;
    for (int c = 0; c < m_cols; ++c) {
        const int d = ColumnDepth(c);
        if (d > best) { best = d; bestCol = c; }
    }
    return bestCol;
}

void Game::MirrorFrom(const Game& src)
{
    m_rows  = src.m_rows;
    m_shift = src.m_shift;
    for (int r = 0; r < kMaxRows; ++r) {
        memset(m_cell[r], 0, sizeof(m_cell[r]));
        memset(m_corrupt[r], 0, sizeof(m_corrupt[r]));
        const int n = RowCount(r);
        const int sn = src.RowCount(r);
        for (int c = 0; c < n; ++c) {
            const int sc = sn - 1 - c;
            if (sc < 0 || sc >= sn) continue;
            m_cell[r][c] = src.m_cell[r][sc];
            m_corrupt[r][c] = src.m_corrupt[r][sc];
        }
    }
    m_corruptTotal  = src.m_corruptTotal;
    m_corruptPopped = src.m_corruptPopped;
    m_landC = -1;
}

void Game::UpdateGarbage(float dt)
{
    // Garbage surging up from the bottom. Every riser gets the same short
    // window no matter how far it has to travel, so a whole attack arrives as
    // one surge instead of feeding in one bubble at a time.
    const float riserWindow = kRiserWindow /
        ((m_riserSpeed > 0.1f) ? m_riserSpeed : 0.1f);
    for (size_t i = 0; i < m_risers.size();) {
        Riser& r = m_risers[i];

        int  hitR = -1, hitC = -1;
        bool arrived = r.hold;      // parked at the contact point last frame

        if (!arrived) {
            float step = dt;
            if (r.warn > 0.0f) {            // warning lead-in, nothing moves
                r.warn -= step;
                if (r.warn > 0.0f) { ++i; continue; }
                step = -r.warn;             // leftovers after the warning
                r.warn = 0.0f;
            }
            r.t += step;

            // Speed that still makes the window: the remaining distance to the
            // ceiling is an upper bound on this bubble's flight.
            float left = riserWindow - r.t;
            if (left < 0.05f) left = 0.05f;
            float speed = (r.y - kCeilY) / left;
            const float speedMin = 1500.0f * m_riserSpeed;
            if (speed < speedMin) speed = speedMin;
            if (speed > 4000.0f) speed = 4000.0f;

            // Small steps, so it reads as a surge and cannot tunnel past
            // a bubble between frames.
            float move = speed * step;
            while (move > 0.0f && !arrived) {
                const float s = move < 6.0f ? move : 6.0f;
                move -= s;
                r.y -= s;
                if (r.y - kR <= kCeilY) arrived = true;
                else if (HitBubbleCell(r.x, r.y, &hitR, &hitC)) arrived = true;
            }
            if (arrived) r.hold = true;
        }

        // Garbage only sticks to a board that is at rest. A riser arriving
        // while a shot is in flight used to force the board back to AIM and
        // swallow the player's bubble; now it parks at the contact point until
        // the board is idle again. A flying shot never collides with a riser
        // either: that check only ever looked at the grid.
        if (!arrived || m_state != AIM) { ++i; continue; }

        // Land() places the bubble through m_sc and clears the pending special,
        // so the player's loaded shot is put back afterwards.
        const int savedColor   = m_sc;
        const int savedSpecial = m_shotSpecial;
        m_sc = r.color;
        m_shotSpecial = kSpecialNone;
        spawnRing(r.x, r.y, 0xFFFFD08Au, 1.6f);
        Land(r.x, r.y, false, hitR, hitC);
        m_sc = savedColor;
        m_shotSpecial = savedSpecial;
        m_risers[i] = m_risers.back();
        m_risers.pop_back();
    }
}

void Game::TickCosmetic(float dt)
{
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
}

void Game::Update(float dt)
{
    m_idle += dt;
    m_sinceRes += dt;
    // The ceiling creeps down in 1/kRowEvery steps per shot; smooth the
    // logical offset for display/collision so it never snaps. This feeds
    // collision geometry, so it stays in the rule update; only particles
    // and debris are free to run during a hand-over banner.
    if (m_ceilAnim != m_ceil) {
        float k = dt * 16.0f;
        if (k > 1.0f) k = 1.0f;
        m_ceilAnim += (m_ceil - m_ceilAnim) * k;
        if (fabsf(m_ceil - m_ceilAnim) < 0.02f) m_ceilAnim = m_ceil;
    }
    TickCosmetic(dt);
    UpdateGarbage(dt);

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
        int hr = -1, hc = -1;
        if (HitBubbleCell(m_sx, m_sy, &hr, &hc)) {
            Land(m_sx, m_sy, true, hr, hc);
            return;
        }
    }
}

int Game::PrismColorAt(int r, int c) const
{
    int nb[6][2];
    const int nn = Neighbors(r, c, nb);
    int cnt[kColors + 1] = {0};
    int best = 0, bestCount = 0;
    for (int i = 0; i < nn; ++i) {
        const int v = m_cell[nb[i][0]][nb[i][1]];
        if (v < 1 || v > kColors) continue;
        ++cnt[v];
        if (cnt[v] > bestCount || (cnt[v] == bestCount && v < best)) {
            bestCount = cnt[v];
            best = v;
        }
    }
    return best;
}

void Game::Land(float x, float y, bool fromShot, int hitR, int hitC)
{
    const int special = m_shotSpecial;
    m_shotSpecial = kSpecialNone;

    // Bombs do not stick: they clear a small radius around the impact and then
    // fall through the normal drop/ceiling bookkeeping.
    if (special == kSpecialBomb) {
        ExplodeAt(x, y, fromShot);
        return;
    }
    if (special == kSpecialPrism)
        spawnRing(x, y, 0xFF80D0FFu, 3.2f);
    else if (special == kSpecialChameleon)
        spawnRing(x, y, 0xFF80FF80u, 3.2f);

    // Landing cell: the empty neighbour of the bubble we actually touched,
    // on the same side as the impact. A global "nearest cell" search could
    // pick a slot on the far side of that bubble (or further), which looked
    // like the shot teleporting across the board.
    int br = -1, bc = -1;

    if (hitR >= 0) {
        int nb[6][2];
        const int nn = Neighbors(hitR, hitC, nb);
        const float hx = XOf(hitR, hitC);
        const float hy = YOfLive(hitR);
        const float ix = x - hx, iy = y - hy;
        float best = 1e30f;
        for (int i = 0; i < nn; ++i) {
            const int nr = nb[i][0], ncc = nb[i][1];
            if (Cell(nr, ncc)) continue;
            if (!IsAttachable(nr, ncc)) continue;
            const float vx = XOf(nr, ncc) - hx;
            const float vy = YOfLive(nr) - hy;
            if (vx * ix + vy * iy <= 0.0f) continue;   // opposite side of host
            const float dx = XOf(nr, ncc) - x;
            const float dy = YOfLive(nr) - y;
            const float d2 = dx * dx + dy * dy;
            if (d2 < best) { best = d2; br = nr; bc = ncc; }
        }
    }

    // Fallback (ceiling hit, cramped pocket, riser without contact): nearest
    // attachable cell, then any free cell.
    for (int pass = 0; pass < 2 && br < 0; ++pass) {
        float best = 1e30f;
        for (int r = 0; r <= m_rows && r < kMaxRows; ++r) {
            const int n = RowCount(r);
            for (int c = 0; c < n; ++c) {
                if (m_cell[r][c]) continue;
                if (pass == 0 && !IsAttachable(r, c)) continue;
                const float dx = XOf(r, c) - x;
                const float dy = YOfLive(r) - y;
                const float d2 = dx * dx + dy * dy;
                if (d2 < best) { best = d2; br = r; bc = c; }
            }
        }
    }

    if (br < 0) { m_state = LOSE; return; }

    int placed = m_sc;
    if (special == kSpecialPrism) {
        const int wild = PrismColorAt(br, bc);
        if (wild > 0) placed = wild;
    }

    m_cell[br][bc] = (u8)placed;
    m_landC = bc;
    if (br + 1 > m_rows) m_rows = br + 1;
    m_state = AIM;
    m_sinceRes = 0.0f;

    // Chameleon paints its six neighbours before the match check, turning a
    // small cluster into a same-colour chain.
    if (special == kSpecialChameleon) {
        int nb[6][2];
        const int nn = Neighbors(br, bc, nb);
        for (int i = 0; i < nn; ++i) {
            const int nr = nb[i][0], nc = nb[i][1];
            if (m_cell[nr][nc]) m_cell[nr][nc] = (u8)m_sc;
        }
    }

    Resolve(br, bc);

    if (GridEmpty()) { m_state = WIN; return; }
    if (fromShot && m_moves > 0) AdvanceCeiling();
    if (m_state != LOSE && OverLine()) m_state = LOSE;
}

void Game::ExplodeAt(float x, float y, bool fromShot)
{
    // A bomb settles at its impact point, so the attack column comes from there.
    {
        const float rel = (x - (m_x0 + kR)) / kD;
        int col = (int)(rel + 0.5f);
        if (col < 0) col = 0;
        if (col >= m_cols) col = m_cols - 1;
        m_landC = col;
    }
    const float rad = m_bombRadius * kD;
    const float r2 = rad * rad;
    int cleared = 0;

    for (int r = 0; r < m_rows && r < kMaxRows; ++r) {
        const int n = RowCount(r);
        for (int c = 0; c < n; ++c) {
            if (!m_cell[r][c]) continue;
            const float dx = XOf(r, c) - x;
            const float dy = YOfLive(r) - y;
            if (dx * dx + dy * dy > r2) continue;
            if (m_corrupt[r][c]) {
                m_corrupt[r][c] = false;
                ++m_corruptPopped;
            }
            SpawnPop(XOf(r, c), YOfLive(r), m_cell[r][c], 5);
            m_cell[r][c] = 0;
            ++cleared;
        }
    }

    m_score += cleared * 10;
    m_pops += cleared;
    m_lastPops = cleared;
    m_lastClear = cleared;
    spawnBlast(x, y, 0xFFFFB050u);
    m_state = AIM;
    m_sinceRes = 0.0f;

    DropFloating();

    if (GridEmpty()) { m_state = WIN; return; }
    if (fromShot) AdvanceCeiling();
    if (m_state != LOSE && OverLine()) m_state = LOSE;
}

void Game::Resolve(int r, int c)
{
    const int col = m_cell[r][c];
    if (!col) return;
    m_lastPops = 0;
    m_lastClear = 0;

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

    m_lastClear = n;
    for (int i = 0; i < n; ++i) {
        const int cr = list[i][0], cc = list[i][1];
        if (m_corrupt[cr][cc]) {
            m_corrupt[cr][cc] = false;
            ++m_corruptPopped;
        }
        SpawnPop(XOf(cr, cc), YOfLive(cr), m_cell[cr][cc], 5);
        m_cell[cr][cc] = 0;
    }
    m_score += n * 10;
    m_pops += n;
    m_lastPops = n;
    spawnBlast(XOf(r, c), YOfLive(r), 0xFFFFD08Au);

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
            if (m_corrupt[r][c]) {
                m_corrupt[r][c] = false;
                ++m_corruptPopped;
            }
            m_cell[r][c] = 0;
            m_score += 20;
            ++m_drops;
            ++m_lastClear;
        }
    }
}

void Game::Descend()
{
    if (m_rows + 1 > kMaxRows) { m_state = LOSE; return; }

    for (int r = kMaxRows - 1; r >= 1; --r) {
        memcpy(m_cell[r], m_cell[r - 1], sizeof(m_cell[0]));
        memcpy(m_corrupt[r], m_corrupt[r - 1], sizeof(m_corrupt[0]));
    }
    memset(m_cell[0], 0, sizeof(m_cell[0]));
    memset(m_corrupt[0], 0, sizeof(m_corrupt[0]));

    m_shift ^= 1;
    ++m_rows;

    const int n = RowCount(0);
    for (int c = 0; c < n; ++c)
        m_cell[0][c] = (u8)(1 + (int)(Rand() % (u32)m_colors));

    // No ring here any more: one going off above the board read as a blast
    // detonating far from the clear that had just started it. The new row is
    // already visible sliding in, so the descent needs no extra mark.
}

void Game::AdvanceCeiling()
{
    // The row for this descent cycle is inserted *up front* and parked one
    // row above the field (m_ceilRowIn), so it slides into view as the ceiling
    // creeps down instead of popping in at the end of the cycle. It is a real
    // grid row, so it collides and can be cleared while partly visible.
    if (!m_ceilRowIn) {
        if (m_rows + 1 > kMaxRows) { m_state = LOSE; return; }
        Descend();
        m_ceilRowIn = true;
        ++m_descends;
    }

    m_ceil += kRowH / (float)m_rowEvery;
    if (m_ceil >= kRowH) {
        m_ceil -= kRowH;
        m_ceilAnim -= kRowH;   // keep the visual offset continuous on wrap
        m_ceilRowIn = false;
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

void Game::spawnRing(float x, float y, u32 color, float scale, float life)
{
    Particle p;
    p.x = x; p.y = y;
    p.vx = 0; p.vy = 0;
    p.t = 0.0f;
    p.life = life;
    p.scale = scale;
    p.kind = 1;
    p.color = color;
    m_parts.push_back(p);
}

void Game::spawnBlast(float x, float y, u32 outer)
{
    spawnRing(x, y, 0xFFFFFFFFu, 2.0f, 0.24f);
    spawnRing(x, y, outer, 3.6f, 0.38f);
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

    // corrupted bubble counted when matched
    if (ok) {
        Reset(17);
        memset(m_cell, 0, sizeof(m_cell));
        memset(m_corrupt, 0, sizeof(m_corrupt));
        m_rows = 3; m_shift = 0; m_state = AIM; m_moves = 0;
        m_cell[2][2] = 1;
        m_cell[2][3] = 1;
        m_corrupt[2][2] = true;
        m_corruptTotal = 1;
        m_corruptPopped = 0;
        m_sc = 1;
        Land(XOf(2, 4), YOf(2) + 3.0f);
        if (CorruptPopped() != 1) {
            ok = false;
            snprintf(err, sizeof(err), "corrupt match not counted");
        }
    }

    // corrupted bubble counted when its anchor is removed
    if (ok) {
        Reset(19);
        memset(m_cell, 0, sizeof(m_cell));
        memset(m_corrupt, 0, sizeof(m_corrupt));
        m_rows = 4; m_shift = 0; m_state = AIM; m_moves = 0;
        m_cell[2][2] = 2;
        m_cell[2][3] = 2;
        m_cell[3][3] = 3;
        m_corrupt[3][3] = true;
        m_corruptTotal = 1;
        m_corruptPopped = 0;
        m_sc = 2;
        Land(XOf(2, 4), YOf(2) + 3.0f);
        if (CorruptPopped() != 1) {
            ok = false;
            snprintf(err, sizeof(err), "corrupt drop not counted");
        }
    }

    // bomb clears a local cluster and does not stick
    if (ok) {
        Reset(23);
        memset(m_cell, 0, sizeof(m_cell));
        memset(m_corrupt, 0, sizeof(m_corrupt));
        m_rows = 3; m_shift = 0; m_state = AIM; m_moves = 0;
        m_cell[0][3] = 1;
        m_cell[1][3] = 2;
        m_cell[1][4] = 3;
        m_cell[2][3] = 4;
        m_sc = 1;
        m_shotSpecial = kSpecialBomb;
        Land(XOf(1, 3), YOf(1) + 3.0f);
        if (BubblesLeft() != 0) {
            ok = false;
            snprintf(err, sizeof(err), "bomb did not clear cluster");
        }
    }

    // bomb-every mod lands on the expected shot
    if (ok) {
        Reset(29);
        SetBombEvery(3);
        m_moves = 2;
        if (CurrentSpecial() != kSpecialBomb) {
            ok = false;
            snprintf(err, sizeof(err), "bomb-every current shot wrong");
        }
        m_moves = 1;
        if (NextSpecial() != kSpecialBomb) {
            ok = false;
            snprintf(err, sizeof(err), "bomb-every next shot wrong");
        }
        SetBombEvery(0);
    }

    // prism adopts the neighbouring colour and completes the match
    if (ok) {
        Reset(31);
        memset(m_cell, 0, sizeof(m_cell));
        m_rows = 3; m_shift = 0; m_state = AIM; m_moves = 0;
        m_cell[2][2] = 2;
        m_cell[2][3] = 2;
        m_sc = 1;
        m_shotSpecial = kSpecialPrism;
        Land(XOf(2, 4), YOf(2) + 3.0f);
        if (Cell(2, 2) || Cell(2, 3) || Cell(2, 4)) {
            ok = false;
            snprintf(err, sizeof(err), "prism did not adopt matching colour");
        }
    }

    // chameleon paints its neighbours and builds a chain
    if (ok) {
        Reset(37);
        memset(m_cell, 0, sizeof(m_cell));
        m_rows = 3; m_shift = 0; m_state = AIM; m_moves = 0;
        m_cell[2][3] = 3;
        m_cell[1][4] = 3;
        m_sc = 1;
        m_shotSpecial = kSpecialChameleon;
        Land(XOf(2, 4), YOf(2) + 3.0f);
        if (Cell(2, 3) || Cell(2, 4) || Cell(1, 4)) {
            ok = false;
            snprintf(err, sizeof(err), "chameleon did not paint a chain");
        }
    }

    // scripted and repeating special shots
    if (ok) {
        Reset(41);
        SetPrismAtMove(3);
        m_moves = 3;
        if (CurrentSpecial() != kSpecialPrism) {
            ok = false;
            snprintf(err, sizeof(err), "prism-at-move wrong");
        }
        SetChameleonAtMove(5);
        m_moves = 5;
        if (CurrentSpecial() != kSpecialChameleon) {
            ok = false;
            snprintf(err, sizeof(err), "chameleon-at-move wrong");
        }
        SetPrismEvery(5);
        m_moves = 4;
        if (CurrentSpecial() != kSpecialPrism) {
            ok = false;
            snprintf(err, sizeof(err), "prism-every wrong");
        }
        SetChameleonEvery(7);
        m_moves = 6;
        if (CurrentSpecial() != kSpecialChameleon) {
            ok = false;
            snprintf(err, sizeof(err), "chameleon-every wrong");
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
