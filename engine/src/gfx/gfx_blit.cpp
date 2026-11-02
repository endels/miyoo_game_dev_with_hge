// Low-power CPU 2D rasterizer. Implements IGfx by collecting axis-aligned
// quads and rasterizing them into an ARGB8888 framebuffer with integer math.
// The frame is rendered in two horizontal bands on the A7's two cores.
//
// The game calls BeginFrame/Clear/DrawQuads and then plat::SwapBuffers(). Since
// the game does not call EndFrame, the libretro platform layer calls
// BlitFlush() from SwapBuffers right before yielding, which is the true frame
// boundary.

#include "gfx/gfx_blit.h"
#include "platform/plat.h"
#include "hge/hge_config.h"

#include <algorithm>
#include <cmath>
#if HGE_FEATURE_THREADS
#include <condition_variable>
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#if HGE_FEATURE_THREADS
#include <mutex>
#include <thread>
#endif
#include <vector>

namespace {

const int OUT_W = 640;
const int OUT_H = 480;

inline int div255(int x) { return (x + 1 + (x >> 8)) >> 8; }

// [a b c d e f]: x' = a*x + c*y + e, y' = b*x + d*y + f.
// Keep in sync with matAffine2D() in gfx_gl.cpp.
void affine2D(f32* m, f32 px, f32 py, f32 dx, f32 dy,
              f32 rot, f32 hs, f32 vs)
{
    if (hs == 0.0f) hs = 1.0f;
    if (vs == 0.0f) vs = 1.0f;
    const f32 c = cosf(rot), s = sinf(rot);
    const f32 a =  c * hs, b = s * hs;
    const f32 cc = -s * vs, d = c * vs;
    m[0] = a;  m[1] = b;
    m[2] = cc; m[3] = d;
    m[4] = px + dx - (a * px + cc * py);
    m[5] = py + dy - (b * px + d  * py);
}

bool affineIdentity(const f32* m)
{
    return m[0] == 1.0f && m[1] == 0.0f && m[2] == 0.0f &&
           m[3] == 1.0f && m[4] == 0.0f && m[5] == 0.0f;
}

bool barycentric(f32 px, f32 py, f32 ax, f32 ay, f32 bx, f32 by,
                 f32 cx, f32 cy, f32* w0, f32* w1, f32* w2)
{
    const f32 d = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
    if (fabsf(d) < 1.0e-6f) return false;
    *w0 = ((by - cy) * (px - cx) + (cx - bx) * (py - cy)) / d;
    *w1 = ((cy - ay) * (px - cx) + (ax - cx) * (py - cy)) / d;
    *w2 = 1.0f - *w0 - *w1;
    const f32 eps = -1.0e-4f;
    return *w0 >= eps && *w1 >= eps && *w2 >= eps;
}

class BlitTexture : public IGfxTexture {
public:
    int w = 0, h = 0;
    bool opaque = false;
    std::vector<u32> px;
};

struct QuadItem {
    BlitTexture* tex;
    GfxVertex v[4];
    GfxBlend blend;
    int cx0, cy0, cx1, cy1;   // captured scissor (unclamped)
    bool xf;
    f32 xfm[6];
};

const void* g_fbPtr = nullptr;
int g_fbW = 0, g_fbH = 0, g_fbPitch = 0;

class GfxBlit;

class GfxBlit : public IGfx {
public:
    bool Init(void*, GfxProcGetter) override
    {
        int lw = plat::Width(), lh = plat::Height();
        if (lw <= 0) lw = 960;
        if (lh <= 0) lh = 540;

        m_w = OUT_W;
        m_h = OUT_H;
        m_fb.assign((size_t)m_w * m_h, 0xFF000000u);

        const float sx = (float)m_w / (float)lw;
        const float sy = (float)m_h / (float)lh;
        m_s  = sx < sy ? sx : sy;
        m_ox = (m_w - (float)lw * m_s) * 0.5f;
        m_oy = (m_h - (float)lh * m_s) * 0.5f;

        g_fbPtr   = m_fb.data();
        g_fbW     = m_w;
        g_fbH     = m_h;
        g_fbPitch = m_w * 4;
        m_blend   = GFX_BLEND_ALPHA;
        m_clip    = false;
        m_quads.reserve(4096);

        const char* im = getenv("BUBBLE_IMMEDIATE");
        m_defer = !(im && im[0] == '1');
#if HGE_FEATURE_THREADS
        if (m_defer && !getenv("BUBBLE_NO_THREADS")) {
            try {
                m_worker = std::thread([this] { workerLoop(); });
                m_hasWorker = true;
            } catch (...) {
                m_hasWorker = false;
            }
        }
#endif
        snprintf(m_info, sizeof(m_info), "blit %dx%d (logical %dx%d) %s%s",
                 m_w, m_h, lw, lh, m_defer ? "defer" : "immediate",
                 m_hasWorker ? "+mt" : "");
        fprintf(stderr, "[blit] %s\n", m_info);
        return true;
    }

    void Shutdown() override
    {
#if HGE_FEATURE_THREADS
        if (m_hasWorker) {
            {
                std::lock_guard<std::mutex> lk(m_mtx);
                m_quit = true;
            }
            m_cvJob.notify_one();
            m_worker.join();
            m_hasWorker = false;
        }
#endif
        g_fbPtr = nullptr;
        m_fb.clear();
    }

    void OnResize(int width, int height) override
    {
        if (width <= 0 || height <= 0) return;
        const float sx = (float)m_w / (float)width;
        const float sy = (float)m_h / (float)height;
        m_s  = sx < sy ? sx : sy;
        m_ox = (m_w - (float)width * m_s) * 0.5f;
        m_oy = (m_h - (float)height * m_s) * 0.5f;
    }

    void BeginFrame() override {}
    void EndFrame() override { FlushFrame(); }

    void Clear(u32 argb) override
    {
        const u32 v = 0xFF000000u | (argb & 0x00FFFFFFu);
        std::fill(m_fb.begin(), m_fb.end(), v);
    }

    void SetScissor(int x, int y, int w, int h) override
    {
        if (w > 0 && h > 0) { m_clip = true; m_cx = x; m_cy = y; m_cw = w; m_ch = h; }
        else m_clip = false;
    }

    void SetTransform(f32 x, f32 y, f32 dx, f32 dy,
                      f32 rot, f32 hs, f32 vs) override
    {
        affine2D(m_xf, x, y, dx, dy, rot, hs, vs);
        m_xfActive = !affineIdentity(m_xf);
    }
    void SetBlend(GfxBlend mode) override { m_blend = mode; }
    void SetTextureFilter(GfxFilter) override {}

    void DrawQuads(IGfxTexture* tex, const GfxVertex* verts, int quadCount) override
    {
        if (!verts || quadCount <= 0) return;
        BlitTexture* t = static_cast<BlitTexture*>(tex);

        const f32* xf = m_xfActive ? m_xf : nullptr;
        if (!m_defer) {
            int cx0 = 0, cy0 = 0, cx1 = m_w, cy1 = m_h;
            if (m_clip) { cx0 = m_cx; cy0 = m_cy; cx1 = m_cx + m_cw; cy1 = m_cy + m_ch; }
            for (int i = 0; i < quadCount; ++i) {
                const GfxVertex* v = verts + i * 4;
                if (t) drawQuad(t, v, xf, m_blend, cx0, cy0, cx1, cy1);
                else   drawSolidQuad(v, xf, m_blend, cx0, cy0, cx1, cy1);
            }
            return;
        }

        for (int i = 0; i < quadCount; ++i) {
            const GfxVertex* v = verts + i * 4;
            QuadItem q;
            q.tex = t;
            q.v[0] = v[0]; q.v[1] = v[1]; q.v[2] = v[2]; q.v[3] = v[3];
            q.blend = m_blend;
            q.xf = m_xfActive;
            for (int k = 0; k < 6; ++k) q.xfm[k] = m_xf[k];
            if (m_clip) { q.cx0 = m_cx; q.cy0 = m_cy; q.cx1 = m_cx + m_cw; q.cy1 = m_cy + m_ch; }
            else        { q.cx0 = 0;     q.cy0 = 0;     q.cx1 = m_w;        q.cy1 = m_h; }
            m_quads.push_back(q);
        }
    }

    void DrawTriangles(IGfxTexture*, const GfxVertex*, int) override {}

    IGfxTexture* CreateTexture(int w, int h, const void* argb, int pitch) override
    {
        if (w <= 0 || h <= 0) return nullptr;
        const int sp = pitch > 0 ? pitch : w * 4;
        const u8* src = (const u8*)argb;

        int nw = w, nh = h;
        if (w > OUT_W || h > OUT_H) {
            const float sx = (float)OUT_W / (float)w;
            const float sy = (float)OUT_H / (float)h;
            const float sc = sx < sy ? sx : sy;
            nw = (int)(w * sc); if (nw < 1) nw = 1;
            nh = (int)(h * sc); if (nh < 1) nh = 1;
        }

        BlitTexture* t = new BlitTexture();
        t->w = nw; t->h = nh;
        t->px.resize((size_t)nw * nh);
        bool opaque = true;

        if (nw == w && nh == h) {
            for (int y = 0; y < h; ++y) {
                const u8* s = src ? src + (size_t)y * sp : nullptr;
                u32* d = &t->px[(size_t)y * w];
                for (int x = 0; x < w; ++x) {
                    const u32 v = s ? ((const u32*)s)[x] : 0u;
                    d[x] = v;
                    if ((v >> 24) != 255) opaque = false;
                }
            }
        } else {
            for (int dy = 0; dy < nh; ++dy) {
                const int sy0 = (int)((long long)dy * h / nh);
                int sy1 = (int)((long long)(dy + 1) * h / nh);
                if (sy1 <= sy0) sy1 = sy0 + 1;
                u32* drow = &t->px[(size_t)dy * nw];
                for (int dx = 0; dx < nw; ++dx) {
                    const int sx0 = (int)((long long)dx * w / nw);
                    int sx1 = (int)((long long)(dx + 1) * w / nw);
                    if (sx1 <= sx0) sx1 = sx0 + 1;
                    unsigned ar = 0, ag = 0, ab = 0, aa = 0, n = 0;
                    for (int sy = sy0; sy < sy1; ++sy) {
                        const u32* srow = (const u32*)(src + (size_t)sy * sp);
                        for (int sx = sx0; sx < sx1; ++sx) {
                            const u32 v = srow[sx];
                            ar += (v >> 16) & 0xFF; ag += (v >> 8) & 0xFF;
                            ab += v & 0xFF; aa += v >> 24; ++n;
                        }
                    }
                    const u32 v = ((aa / n) << 24) | ((ar / n) << 16) | ((ag / n) << 8) | (ab / n);
                    drow[dx] = v;
                    if ((v >> 24) != 255) opaque = false;
                }
            }
        }
        t->opaque = opaque && argb;
        return t;
    }

    bool UpdateTexture(IGfxTexture* tex, int x, int y, int w, int h,
                       const void* argb, int pitch) override
    {
        if (!tex || !argb || w <= 0 || h <= 0) return false;
        BlitTexture* t = static_cast<BlitTexture*>(tex);
        const int sp = pitch > 0 ? pitch : w * 4;
        const u8* src = (const u8*)argb;
        for (int j = 0; j < h; ++j) {
            const int dy = y + j;
            if (dy < 0 || dy >= t->h) continue;
            const int n = std::min(w, t->w - x);
            if (n <= 0) continue;
            memcpy(&t->px[(size_t)dy * t->w + x], src + (size_t)j * sp, (size_t)n * 4);
        }
        t->opaque = false;
        return true;
    }

    void ReleaseTexture(IGfxTexture* tex) override { delete static_cast<BlitTexture*>(tex); }
    int  TextureWidth(IGfxTexture* t) override { return t ? static_cast<BlitTexture*>(t)->w : 0; }
    int  TextureHeight(IGfxTexture* t) override { return t ? static_cast<BlitTexture*>(t)->h : 0; }

    bool ReadPixels(int x, int y, int w, int h, u32* outArgb) override
    {
        if (!outArgb || w <= 0 || h <= 0) return false;
        for (int j = 0; j < h; ++j) {
            const int sy = y + j;
            if (sy < 0 || sy >= m_h) { memset(outArgb + (size_t)j * w, 0, (size_t)w * 4); continue; }
            for (int i = 0; i < w; ++i) {
                const int sx = x + i;
                outArgb[(size_t)j * w + i] =
                    (sx >= 0 && sx < m_w) ? m_fb[(size_t)sy * m_w + sx] : 0u;
            }
        }
        return true;
    }

    const char* Info() const override { return m_info; }

    // True frame boundary: rasterize everything queued since the last flush.
    void FlushFrame()
    {
        if (!m_defer) return;
        if (getenv("BUBBLE_BLIT_LOG")) {
            static int fl = 0;
            if (++fl % 60 == 0)
                fprintf(stderr, "[blit] flush=%d quads=%d worker=%d\n",
                        fl, (int)m_quads.size(), (int)m_hasWorker);
        }
        if (m_quads.empty()) return;
#if HGE_FEATURE_THREADS
        const int mid = m_h / 2;
        if (!m_hasWorker || m_quads.size() < 16) {
            renderBand(0, m_h);
        } else {
            {
                std::lock_guard<std::mutex> lk(m_mtx);
                m_bandY0 = mid;
                m_bandY1 = m_h;
                m_jobPending = true;
                m_jobDone = false;
            }
            m_cvJob.notify_one();
            renderBand(0, mid);
            {
                std::unique_lock<std::mutex> lk(m_mtx);
                m_cvDone.wait(lk, [this] { return m_jobDone; });
            }
        }
#else
        renderBand(0, m_h);
#endif
        m_quads.clear();
    }

private:
#if HGE_FEATURE_THREADS
    void workerLoop()
    {
        for (;;) {
            int by0 = 0, by1 = 0;
            {
                std::unique_lock<std::mutex> lk(m_mtx);
                m_cvJob.wait(lk, [this] { return m_jobPending || m_quit; });
                if (m_quit) return;
                m_jobPending = false;
                by0 = m_bandY0;
                by1 = m_bandY1;
            }
            renderBand(by0, by1);
            {
                std::lock_guard<std::mutex> lk(m_mtx);
                m_jobDone = true;
            }
            m_cvDone.notify_one();
        }
    }
#endif

    void renderBand(int by0, int by1)
    {
        for (const QuadItem& q : m_quads) {
            const int cx0 = std::max(q.cx0, 0);
            const int cy0 = std::max(q.cy0, by0);
            const int cx1 = std::min(q.cx1, m_w);
            const int cy1 = std::min(q.cy1, by1);
            if (cx0 >= cx1 || cy0 >= cy1) continue;
            const f32* xf = q.xf ? q.xfm : nullptr;
            if (q.tex) drawQuad(q.tex, q.v, xf, q.blend, cx0, cy0, cx1, cy1);
            else       drawSolidQuad(q.v, xf, q.blend, cx0, cy0, cx1, cy1);
        }
    }

    void fillSolid(u32 c, GfxBlend blend, int x0, int y0, int x1, int y1)
    {
        const int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
        const int a = (c >> 24) & 0xFF;
        const u32 srgb = ((u32)r << 16) | ((u32)g << 8) | (u32)b;
        if (x0 >= x1 || y0 >= y1) return;

        for (int dy = y0; dy < y1; ++dy) {
            u32* d = &m_fb[(size_t)dy * m_w];
            for (int dx = x0; dx < x1; ++dx) {
                const u32 dst = d[dx];
                if (blend == GFX_BLEND_NONE || a == 255) {
                    d[dx] = 0xFF000000u | srgb;
                } else if (a == 0) {
                    continue;
                } else if (blend == GFX_BLEND_ALPHA) {
                    const int dr = (dst >> 16) & 0xFF, dg = (dst >> 8) & 0xFF, db = dst & 0xFF;
                    const int ia = 255 - a;
                    d[dx] = 0xFF000000u
                          | ((u32)div255(r * a + dr * ia) << 16)
                          | ((u32)div255(g * a + dg * ia) << 8)
                          |  (u32)div255(b * a + db * ia);
                } else if (blend == GFX_BLEND_ADD) {
                    const int dr = (dst >> 16) & 0xFF, dg = (dst >> 8) & 0xFF, db = dst & 0xFF;
                    d[dx] = 0xFF000000u
                          | ((u32)std::min(255, dr + r) << 16)
                          | ((u32)std::min(255, dg + g) << 8)
                          |  (u32)std::min(255, db + b);
                } else { // MUL
                    const int dr = (dst >> 16) & 0xFF, dg = (dst >> 8) & 0xFF, db = dst & 0xFF;
                    d[dx] = 0xFF000000u
                          | ((u32)div255(dr * r) << 16)
                          | ((u32)div255(dg * g) << 8)
                          |  (u32)div255(db * b);
                }
            }
        }
    }

    void drawTransformedQuad(BlitTexture* t, const GfxVertex* q, const f32* xf,
                             GfxBlend blend, int cx0, int cy0, int cx1, int cy1)
    {
        f32 vx[4], vy[4];
        f32 minx = 1.0e30f, miny = 1.0e30f;
        f32 maxx = -1.0e30f, maxy = -1.0e30f;
        for (int i = 0; i < 4; ++i) {
            const f32 lx = xf[0] * q[i].x + xf[2] * q[i].y + xf[4];
            const f32 ly = xf[1] * q[i].x + xf[3] * q[i].y + xf[5];
            vx[i] = m_ox + lx * m_s;
            vy[i] = m_oy + ly * m_s;
            if (vx[i] < minx) minx = vx[i];
            if (vx[i] > maxx) maxx = vx[i];
            if (vy[i] < miny) miny = vy[i];
            if (vy[i] > maxy) maxy = vy[i];
        }

        int x0 = std::max(std::max((int)floorf(minx), cx0), 0);
        int y0 = std::max(std::max((int)floorf(miny), cy0), 0);
        int x1 = std::min(std::min((int)ceilf(maxx), cx1), m_w);
        int y1 = std::min(std::min((int)ceilf(maxy), cy1), m_h);
        if (x0 >= x1 || y0 >= y1) return;

        const bool hasTex = t && !t->px.empty();
        const int tw = hasTex ? t->w : 1;
        const int th = hasTex ? t->h : 1;
        const u32 c = q[0].color;
        const int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
        const int a = (c >> 24) & 0xFF;
        const bool whiteMod = (r == 255 && g == 255 && b == 255);

        for (int dy = y0; dy < y1; ++dy) {
            const f32 fy = (f32)dy + 0.5f;
            u32* drow = &m_fb[(size_t)dy * m_w];
            for (int dx = x0; dx < x1; ++dx) {
                const f32 fx = (f32)dx + 0.5f;
                f32 b0, b1, b2, u, v;
                if (barycentric(fx, fy, vx[0], vy[0], vx[1], vy[1],
                                vx[2], vy[2], &b0, &b1, &b2)) {
                    u = b0 * q[0].u + b1 * q[1].u + b2 * q[2].u;
                    v = b0 * q[0].v + b1 * q[1].v + b2 * q[2].v;
                } else if (barycentric(fx, fy, vx[1], vy[1], vx[3], vy[3],
                                       vx[2], vy[2], &b0, &b1, &b2)) {
                    u = b0 * q[1].u + b1 * q[3].u + b2 * q[2].u;
                    v = b0 * q[1].v + b1 * q[3].v + b2 * q[2].v;
                } else {
                    continue;
                }

                u32 texel = 0xFFFFFFFFu;
                if (hasTex) {
                    int tx = (int)(u * (f32)tw);
                    int ty = (int)(v * (f32)th);
                    if (tx < 0) tx = 0; else if (tx >= tw) tx = tw - 1;
                    if (ty < 0) ty = 0; else if (ty >= th) ty = th - 1;
                    texel = t->px[(size_t)ty * tw + tx];
                }

                int ta = (int)(texel >> 24);
                if (ta == 0 && blend == GFX_BLEND_ALPHA) continue;
                if (a != 255) ta = div255(ta * a);
                int tr = (int)((texel >> 16) & 0xFF);
                int tg = (int)((texel >> 8) & 0xFF);
                int tb = (int)(texel & 0xFF);
                if (!whiteMod) {
                    tr = div255(tr * r); tg = div255(tg * g); tb = div255(tb * b);
                }

                u32* d = &drow[dx];
                const u32 dst = *d;
                if (blend == GFX_BLEND_NONE || ta == 255) {
                    *d = 0xFF000000u | ((u32)tr << 16) | ((u32)tg << 8) | (u32)tb;
                } else if (blend == GFX_BLEND_ALPHA) {
                    const int dr = (dst >> 16) & 0xFF, dg = (dst >> 8) & 0xFF;
                    const int db = dst & 0xFF, ia = 255 - ta;
                    *d = 0xFF000000u
                       | ((u32)div255(tr * ta + dr * ia) << 16)
                       | ((u32)div255(tg * ta + dg * ia) << 8)
                       |  (u32)div255(tb * ta + db * ia);
                } else if (blend == GFX_BLEND_ADD) {
                    const int dr = (dst >> 16) & 0xFF, dg = (dst >> 8) & 0xFF;
                    const int db = dst & 0xFF;
                    *d = 0xFF000000u
                       | ((u32)std::min(255, dr + tr) << 16)
                       | ((u32)std::min(255, dg + tg) << 8)
                       |  (u32)std::min(255, db + tb);
                } else {
                    const int dr = (dst >> 16) & 0xFF, dg = (dst >> 8) & 0xFF;
                    const int db = dst & 0xFF;
                    *d = 0xFF000000u
                       | ((u32)div255(dr * tr) << 16)
                       | ((u32)div255(dg * tg) << 8)
                       |  (u32)div255(db * tb);
                }
            }
        }
    }

    void drawSolidQuad(const GfxVertex* q, const f32* xf, GfxBlend blend,
                       int cx0, int cy0, int cx1, int cy1)
    {
        if (xf) {
            drawTransformedQuad(nullptr, q, xf, blend, cx0, cy0, cx1, cy1);
            return;
        }
        int x = (int)lroundf(m_ox + q[0].x * m_s);
        int y = (int)lroundf(m_oy + q[0].y * m_s);
        int w = (int)lroundf((q[1].x - q[0].x) * m_s);
        int h = (int)lroundf((q[2].y - q[0].y) * m_s);
        if (w <= 0) w = (int)lroundf((q[3].x - q[0].x) * m_s);
        if (h <= 0) h = (int)lroundf((q[3].y - q[0].y) * m_s);
        if (w <= 0 || h <= 0) return;
        int x0 = std::max(std::max(x, cx0), 0);
        int y0 = std::max(std::max(y, cy0), 0);
        int x1 = std::min(std::min(x + w, cx1), m_w);
        int y1 = std::min(std::min(y + h, cy1), m_h);
        if (x0 >= x1 || y0 >= y1) return;
        fillSolid(q[0].color, blend, x0, y0, x1, y1);
    }

    void drawQuad(BlitTexture* t, const GfxVertex* q, const f32* xf, GfxBlend blend,
                  int cx0, int cy0, int cx1, int cy1)
    {
        if (xf) {
            drawTransformedQuad(t, q, xf, blend, cx0, cy0, cx1, cy1);
            return;
        }
        int x = (int)lroundf(m_ox + q[0].x * m_s);
        int y = (int)lroundf(m_oy + q[0].y * m_s);
        int w = (int)lroundf((q[1].x - q[0].x) * m_s);
        int h = (int)lroundf((q[2].y - q[0].y) * m_s);
        if (w <= 0) w = (int)lroundf((q[3].x - q[0].x) * m_s);
        if (h <= 0) h = (int)lroundf((q[3].y - q[0].y) * m_s);
        if (w <= 0 || h <= 0) return;

        int x0 = std::max(x, cx0), y0 = std::max(y, cy0);
        int x1 = std::min(x + w, cx1), y1 = std::min(y + h, cy1);
        x0 = std::max(x0, 0); y0 = std::max(y0, 0);
        x1 = std::min(x1, m_w); y1 = std::min(y1, m_h);
        if (x0 >= x1 || y0 >= y1) return;

        const u32 c = q[0].color;
        if (!t || t->px.empty()) {
            fillSolid(c, blend, x0, y0, x1, y1);
            return;
        }

        const int tw = t->w, th = t->h;
        const f32 uA = q[0].u, uB = q[3].u, vA = q[0].v, vB = q[3].v;

        // Fast path: full texture at 1:1 (the pre-scaled background).
        if (tw == w && th == h && uA == 0.0f && vA == 0.0f && uB == 1.0f && vB == 1.0f) {
            const int r0 = (c >> 16) & 0xFF, g0 = (c >> 8) & 0xFF, b0 = c & 0xFF;
            const int a0 = (c >> 24) & 0xFF;
            const int n = x1 - x0;
            const bool copyRows = t->opaque && r0 == 255 && g0 == 255 && b0 == 255 &&
                                  a0 == 255 &&
                                  (blend == GFX_BLEND_NONE || blend == GFX_BLEND_ALPHA);
            for (int dy = y0; dy < y1; ++dy) {
                const u32* srow = &t->px[(size_t)(dy - y) * tw + (x0 - x)];
                u32* drow = &m_fb[(size_t)dy * m_w];
                if (copyRows) {
                    memcpy(drow + x0, srow, (size_t)n * 4);
                    continue;
                }
                for (int dx = x0; dx < x1; ++dx) {
                    const u32 texel = *srow++;
                    int ta = (int)(texel >> 24);
                    if (ta == 0 && blend == GFX_BLEND_ALPHA) continue;
                    if (a0 != 255) ta = div255(ta * a0);
                    int tr = (int)((texel >> 16) & 0xFF);
                    int tg = (int)((texel >> 8) & 0xFF);
                    int tb = (int)(texel & 0xFF);
                    if (!(r0 == 255 && g0 == 255 && b0 == 255)) {
                        tr = div255(tr * r0); tg = div255(tg * g0); tb = div255(tb * b0);
                    }
                    const u32 dst = drow[dx];
                    if (blend == GFX_BLEND_NONE || ta == 255) {
                        drow[dx] = 0xFF000000u | ((u32)tr << 16) | ((u32)tg << 8) | (u32)tb;
                    } else if (blend == GFX_BLEND_ALPHA) {
                        const int dr = (dst >> 16) & 0xFF, dg = (dst >> 8) & 0xFF, db = dst & 0xFF;
                        const int ia = 255 - ta;
                        drow[dx] = 0xFF000000u
                                 | ((u32)div255(tr * ta + dr * ia) << 16)
                                 | ((u32)div255(tg * ta + dg * ia) << 8)
                                 |  (u32)div255(tb * ta + db * ia);
                    } else if (blend == GFX_BLEND_ADD) {
                        const int dr = (dst >> 16) & 0xFF, dg = (dst >> 8) & 0xFF, db = dst & 0xFF;
                        drow[dx] = 0xFF000000u
                                 | ((u32)std::min(255, dr + tr) << 16)
                                 | ((u32)std::min(255, dg + tg) << 8)
                                 |  (u32)std::min(255, db + tb);
                    } else {
                        const int dr = (dst >> 16) & 0xFF, dg = (dst >> 8) & 0xFF, db = dst & 0xFF;
                        drow[dx] = 0xFF000000u
                                 | ((u32)div255(dr * tr) << 16)
                                 | ((u32)div255(dg * tg) << 8)
                                 |  (u32)div255(db * tb);
                    }
                }
            }
            return;
        }

        int su0 = (int)std::floor(uA * tw);
        int su1 = (int)std::floor(uB * tw);
        int sv0 = (int)std::floor(vA * th);
        int sv1 = (int)std::floor(vB * th);
        su0 = std::max(0, std::min(tw - 1, su0));
        su1 = std::max(0, std::min(tw - 1, su1));
        sv0 = std::max(0, std::min(th - 1, sv0));
        sv1 = std::max(0, std::min(th - 1, sv1));

        const int stepx = (int)(((long long)(su1 - su0) << 16) / w);
        const int stepy = (int)(((long long)(sv1 - sv0) << 16) / h);
        long long sxf = ((long long)su0 << 16) + (long long)stepx * (x0 - x);
        long long syf = ((long long)sv0 << 16) + (long long)stepy * (y0 - y);

        const int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
        const int a = (c >> 24) & 0xFF;
        const bool whiteMod = (r == 255 && g == 255 && b == 255);
        const bool plainCopy = t->opaque && whiteMod && a == 255 &&
                               (blend == GFX_BLEND_NONE || blend == GFX_BLEND_ALPHA);

        for (int dy = y0; dy < y1; ++dy) {
            int ty = (int)(syf >> 16);
            syf += stepy;
            if (ty < 0) ty = 0; else if (ty >= th) ty = th - 1;
            const u32* srow = &t->px[(size_t)ty * tw];
            u32* drow = &m_fb[(size_t)dy * m_w];
            long long sx = sxf;
            for (int dx = x0; dx < x1; ++dx) {
                int tx = (int)(sx >> 16);
                sx += stepx;
                if (tx < 0) tx = 0; else if (tx >= tw) tx = tw - 1;
                const u32 texel = srow[tx];

                if (plainCopy) {
                    drow[dx] = 0xFF000000u | (texel & 0x00FFFFFFu);
                    continue;
                }

                int ta = (int)(texel >> 24);
                if (ta == 0 && blend == GFX_BLEND_ALPHA) continue;
                if (a != 255) ta = div255(ta * a);

                int tr = (int)((texel >> 16) & 0xFF);
                int tg = (int)((texel >> 8) & 0xFF);
                int tb = (int)(texel & 0xFF);
                if (!whiteMod) {
                    tr = div255(tr * r); tg = div255(tg * g); tb = div255(tb * b);
                }

                const u32 dst = drow[dx];
                if (blend == GFX_BLEND_NONE || ta == 255) {
                    drow[dx] = 0xFF000000u | ((u32)tr << 16) | ((u32)tg << 8) | (u32)tb;
                } else if (blend == GFX_BLEND_ALPHA) {
                    const int dr = (dst >> 16) & 0xFF, dg = (dst >> 8) & 0xFF, db = dst & 0xFF;
                    const int ia = 255 - ta;
                    drow[dx] = 0xFF000000u
                             | ((u32)div255(tr * ta + dr * ia) << 16)
                             | ((u32)div255(tg * ta + dg * ia) << 8)
                             |  (u32)div255(tb * ta + db * ia);
                } else if (blend == GFX_BLEND_ADD) {
                    const int dr = (dst >> 16) & 0xFF, dg = (dst >> 8) & 0xFF, db = dst & 0xFF;
                    drow[dx] = 0xFF000000u
                             | ((u32)std::min(255, dr + tr) << 16)
                             | ((u32)std::min(255, dg + tg) << 8)
                             |  (u32)std::min(255, db + tb);
                } else {
                    const int dr = (dst >> 16) & 0xFF, dg = (dst >> 8) & 0xFF, db = dst & 0xFF;
                    drow[dx] = 0xFF000000u
                             | ((u32)div255(dr * tr) << 16)
                             | ((u32)div255(dg * tg) << 8)
                             |  (u32)div255(db * tb);
                }
            }
        }
    }

    std::vector<u32> m_fb;
    std::vector<QuadItem> m_quads;
    int m_w = OUT_W, m_h = OUT_H;
    float m_s = 1.0f, m_ox = 0.0f, m_oy = 0.0f;
    GfxBlend m_blend = GFX_BLEND_ALPHA;
    f32 m_xf[6] = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    bool m_xfActive = false;
    bool m_defer = false;
    bool m_clip = false;
    int m_cx = 0, m_cy = 0, m_cw = 0, m_ch = 0;
    char m_info[128] = "blit not initialised";

    bool m_hasWorker = false;
#if HGE_FEATURE_THREADS
    std::thread m_worker;
    std::mutex m_mtx;
    std::condition_variable m_cvJob, m_cvDone;
    bool m_jobPending = false, m_jobDone = false, m_quit = false;
    int m_bandY0 = 0, m_bandY1 = 0;
#endif
};

GfxBlit* g_blit = nullptr;

} // namespace

IGfx* CreateGfxBlit()
{
    GfxBlit* g = new GfxBlit();
    g_blit = g;
    return g;
}

const void* BlitFramebuffer(int* w, int* h, int* pitch)
{
    if (w) *w = g_fbW;
    if (h) *h = g_fbH;
    if (pitch) *pitch = g_fbPitch;
    return g_fbPtr;
}

void BlitFlush()
{
    if (g_blit) g_blit->FlushFrame();
}
