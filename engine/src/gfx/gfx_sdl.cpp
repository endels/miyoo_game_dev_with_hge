// SDL2 renderer backend implementing IGfx via SDL_RenderCopy (hardware).
//
// This mirrors how the working Miyoo SDL apps (OpenBOR, Drastic, PICO-8) use
// SDL2: ONE renderer created on the window with the "mmiyoo" driver, drawing
// sprites with SDL_RenderCopy and presenting with SDL_RenderPresent. That path
// displays correctly, uses the MI_GFX 2D accelerator, and tears down cleanly
// (returns to MainUI without a black screen).
//
// The MMIYOO renderer does NOT implement SDL_RenderCopyEx, so rotation is
// rasterized on the CPU into a scratch texture and then blitted with the same
// SDL_RenderCopy path as every other sprite. Horizontal flip (dino sprites)
// still uses a pre-mirrored texture copy on the fast path.
// Per-quad colour is uniform in this game, so colour/alpha modulation +
// texture blend mode reproduce the HGE colour behaviour.

#include "gfx/gfx_sdl.h"
#include "platform/plat.h"

#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

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

// Barycentric weights for a point in a triangle (winding independent).
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

class GfxTextureSDL : public IGfxTexture {
public:
    SDL_Texture* tex = nullptr;
    SDL_Texture* flip = nullptr;  // horizontally mirrored copy (for flipped quads)
    std::vector<u32> src;         // tight ARGB shadow for CPU rotation
    int w = 0, h = 0;
    // Cached per-texture state (index 0 = tex, 1 = flip). SDL's software
    // renderer makes SetTextureColorMod/AlphaMod/BlendMode expensive, and this
    // game redraws hundreds of quads per frame with mostly identical state.
    bool colorSet[2] = {false, false};
    bool blendSet[2] = {false, false};
    u8 cr[2] = {0, 0}, cg[2] = {0, 0}, cb[2] = {0, 0}, ca[2] = {0, 0};
    SDL_BlendMode cblend[2] = {SDL_BLENDMODE_NONE, SDL_BLENDMODE_NONE};
    ~GfxTextureSDL() {
        if (tex) SDL_DestroyTexture(tex);
        if (flip) SDL_DestroyTexture(flip);
    }
};

class GfxSDL : public IGfx {
public:
    bool Init(void* /*nativeWindow*/, GfxProcGetter /*getProc*/) override
    {
        m_r = static_cast<SDL_Renderer*>(plat::Renderer());
        if (!m_r) {
            fprintf(stderr, "[gfx_sdl] no SDL_Renderer from platform layer\n");
            return false;
        }
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
        // Aspect-preserving scale + letterbox from the 960x540 logical space to
        // the renderer output (window). No SDL_RenderSetLogicalSize (broken on
        // MMIYOO).
        {
            int ow = 0, oh = 0;
            SDL_GetRendererOutputSize(m_r, &ow, &oh);
            int lw = plat::Width(), lh = plat::Height();
            if (lw <= 0) lw = 960;
            if (lh <= 0) lh = 540;
            if (ow > 0 && oh > 0) {
                float sx = (float)ow / (float)lw;
                float sy = (float)oh / (float)lh;
                m_s = sx < sy ? sx : sy;
                m_ox = (ow - lw * m_s) * 0.5f;
                m_oy = (oh - lh * m_s) * 0.5f;
            }
        }
        SDL_RendererInfo info;
        if (SDL_GetRendererInfo(m_r, &info) == 0) {
            m_maxTexW = info.max_texture_width;
            m_maxTexH = info.max_texture_height;
            snprintf(m_info, sizeof(m_info), "SDL2 renderer: %s (max tex %dx%d)",
                     info.name, m_maxTexW, m_maxTexH);
        } else {
            snprintf(m_info, sizeof(m_info), "SDL2 renderer: ?");
        }
        u32 white = 0xFFFFFFFF;
        m_white = static_cast<GfxTextureSDL*>(CreateTexture(1, 1, &white, 4));
        return true;
    }

    void Shutdown() override
    {
        if (m_scratch) { SDL_DestroyTexture(m_scratch); m_scratch = nullptr; }
        m_scratchW = m_scratchH = 0;
        if (m_white) { delete m_white; m_white = nullptr; }
    }

    void OnResize(int width, int height) override
    {
        m_w = width > 0 ? width : 1;
        m_h = height > 0 ? height : 1;
    }

    void BeginFrame() override
    {
        if (++m_frameCount % 120 == 0) {
            fprintf(stderr, "[gfx_sdl] draws ok=%ld fail=%ld (frame %ld t=%.3f) "
                    "clear=%.2fms state=%.2fms copy=%.2fms calls=%ld px=%ldK\n",
                    m_drawOk, m_drawFail, m_frameCount, plat::Time(),
                    m_tClear / 120 * 1000.0, m_tState / 120 * 1000.0,
                    m_tCopy / 120 * 1000.0, m_calls / 120, m_pixels / 120 / 1024);
            m_tClear = m_tState = m_tCopy = 0;
            m_calls = m_pixels = 0;
        }
    }
    void EndFrame() override {}

    void Clear(u32 argb) override
    {
        SDL_SetRenderDrawColor(m_r, (u8)((argb >> 16) & 0xFF), (u8)((argb >> 8) & 0xFF),
                               (u8)(argb & 0xFF), (u8)((argb >> 24) & 0xFF));
        const double tc = plat::Time();
        SDL_RenderClear(m_r);
        m_tClear += plat::Time() - tc;
    }

    void SetScissor(int x, int y, int w, int h) override
    {
        if (w > 0 && h > 0) { SDL_Rect r{ x, y, w, h }; SDL_RenderSetClipRect(m_r, &r); }
        else SDL_RenderSetClipRect(m_r, nullptr);
    }

    void SetTransform(f32 x, f32 y, f32 dx, f32 dy,
                      f32 rot, f32 hs, f32 vs) override
    {
        affine2D(m_xf, x, y, dx, dy, rot, hs, vs);
        m_xfActive = !affineIdentity(m_xf);
    }

    void SetBlend(GfxBlend mode) override { m_blend = mode; }
    void SetTextureFilter(GfxFilter filter) override { m_filter = filter; }

    void DrawQuads(IGfxTexture* tex, const GfxVertex* verts, int quadCount) override
    {
        if (!verts || quadCount <= 0) return;
        GfxTextureSDL* t = tex ? static_cast<GfxTextureSDL*>(tex) : m_white;
        if (!t || !t->tex) return;
        for (int i = 0; i < quadCount; ++i)
            drawQuad(t, verts + i * 4);
    }

    void DrawTriangles(IGfxTexture*, const GfxVertex*, int) override {}

    IGfxTexture* CreateTexture(int w, int h, const void* argb, int pitch) override
    {
        if (w <= 0 || h <= 0) return nullptr;

        int tw = w, th = h;
        bool needScale = (m_maxTexW > 0 && w > m_maxTexW) || (m_maxTexH > 0 && h > m_maxTexH);

        std::vector<u32> pix((size_t)tw * th);
        if (needScale && argb) {
            float s = 1.0f;
            if (m_maxTexW > 0 && w > m_maxTexW) s = fminf(s, (float)m_maxTexW / (float)w);
            if (m_maxTexH > 0 && h > m_maxTexH) s = fminf(s, (float)m_maxTexH / (float)h);
            tw = (int)(w * s); if (tw < 1) tw = 1;
            th = (int)(h * s); if (th < 1) th = 1;
            pix.assign((size_t)tw * th, 0);
            const u8* src = static_cast<const u8*>(argb);
            const int sp = pitch > 0 ? pitch : w * 4;
            for (int dy = 0; dy < th; ++dy) {
                int sy0 = (int)((long long)dy * h / th);
                int sy1 = (int)((long long)(dy + 1) * h / th);
                if (sy1 <= sy0) sy1 = sy0 + 1;
                for (int dx = 0; dx < tw; ++dx) {
                    int sx0 = (int)((long long)dx * w / tw);
                    int sx1 = (int)((long long)(dx + 1) * w / tw);
                    if (sx1 <= sx0) sx1 = sx0 + 1;
                    unsigned acc[4] = {0,0,0,0}; unsigned n = 0;
                    for (int yy = sy0; yy < sy1; ++yy)
                        for (int xx = sx0; xx < sx1; ++xx) {
                            const u8* p = src + (size_t)yy * sp + (size_t)xx * 4;
                            acc[0]+=p[0]; acc[1]+=p[1]; acc[2]+=p[2]; acc[3]+=p[3]; ++n;
                        }
                    u8* d = (u8*)&pix[(size_t)dy * tw + dx];
                    d[0]=(u8)(acc[0]/n); d[1]=(u8)(acc[1]/n); d[2]=(u8)(acc[2]/n); d[3]=(u8)(acc[3]/n);
                }
            }
            fprintf(stderr, "[gfx_sdl] downscale %dx%d -> %dx%d\n", w, h, tw, th);
        } else {
            pix.assign((size_t)tw * th, 0);
            if (argb) {
                const u8* src = static_cast<const u8*>(argb);
                const int sp = pitch > 0 ? pitch : w * 4;
                for (int y = 0; y < th; ++y)
                    memcpy(&pix[(size_t)y * tw], src + (size_t)y * sp, (size_t)tw * 4);
            }
        }

        GfxTextureSDL* t = new GfxTextureSDL();
        t->w = tw; t->h = th;
        t->tex = SDL_CreateTexture(m_r, SDL_PIXELFORMAT_ARGB8888,
                                   SDL_TEXTUREACCESS_STATIC, tw, th);
        if (!t->tex) {
            fprintf(stderr, "[gfx_sdl] CreateTexture %dx%d failed: %s\n", tw, th, SDL_GetError());
            delete t;
            return nullptr;
        }
        SDL_UpdateTexture(t->tex, nullptr, &pix[0], tw * 4);

        // Horizontally mirrored copy for flip draws.
        std::vector<u32> f((size_t)tw * th);
        for (int y = 0; y < th; ++y)
            for (int x = 0; x < tw; ++x)
                f[(size_t)y * tw + (tw - 1 - x)] = pix[(size_t)y * tw + x];
        t->flip = SDL_CreateTexture(m_r, SDL_PIXELFORMAT_ARGB8888,
                                    SDL_TEXTUREACCESS_STATIC, tw, th);
        if (t->flip)
            SDL_UpdateTexture(t->flip, nullptr, &f[0], tw * 4);
        t->src = pix;
        return t;
    }

    bool UpdateTexture(IGfxTexture* tex, int x, int y, int w, int h,
                       const void* argb, int pitch) override
    {
        if (!tex || !argb || w <= 0 || h <= 0) return false;
        GfxTextureSDL* t = static_cast<GfxTextureSDL*>(tex);
        if (!t->tex) return false;
        const int sp = pitch > 0 ? pitch : w * 4;
        SDL_Rect r{ x, y, w, h };
        bool ok;
        if (sp == w * 4) {
            ok = SDL_UpdateTexture(t->tex, &r, argb, sp) == 0;
        } else {
            std::vector<u8> tmp((size_t)w * h * 4);
            const u8* src = static_cast<const u8*>(argb);
            for (int j = 0; j < h; ++j)
                memcpy(&tmp[(size_t)j * w * 4], src + (size_t)j * sp, (size_t)w * 4);
            ok = SDL_UpdateTexture(t->tex, &r, &tmp[0], w * 4) == 0;
        }
        if (ok && !t->src.empty()) {
            const int n = (x >= 0) ? std::min(w, t->w - x) : 0;
            if (n > 0) {
                for (int j = 0; j < h; ++j) {
                    const int dy = y + j;
                    if (dy < 0 || dy >= t->h) continue;
                    memcpy(&t->src[(size_t)dy * t->w + x],
                           static_cast<const u8*>(argb) + (size_t)j * sp,
                           (size_t)n * 4);
                }
            }
        }
        return ok;
    }

    void ReleaseTexture(IGfxTexture* tex) override { delete static_cast<GfxTextureSDL*>(tex); }
    int  TextureWidth(IGfxTexture* t) override { return t ? static_cast<GfxTextureSDL*>(t)->w : 0; }
    int  TextureHeight(IGfxTexture* t) override { return t ? static_cast<GfxTextureSDL*>(t)->h : 0; }

    bool ReadPixels(int x, int y, int w, int h, u32* outArgb) override
    {
        if (!outArgb || w <= 0 || h <= 0) return false;
        std::vector<u8> tmp((size_t)w * h * 4);
        SDL_Rect r{ x, y, w, h };
        if (SDL_RenderReadPixels(m_r, &r, SDL_PIXELFORMAT_ARGB8888, &tmp[0], w * 4) != 0)
            return false;
        for (int i = 0; i < w * h; ++i) {
            const u8* s = &tmp[(size_t)i * 4];
            outArgb[i] = ((u32)s[3] << 24) | ((u32)s[2] << 16) | ((u32)s[1] << 8) | (u32)s[0];
        }
        return true;
    }

    const char* Info() const override { return m_info; }

private:
    static SDL_BlendMode toSdlBlend(GfxBlend b)
    {
        switch (b) {
        case GFX_BLEND_NONE: return SDL_BLENDMODE_NONE;
        case GFX_BLEND_ADD:  return SDL_BLENDMODE_ADD;
        case GFX_BLEND_MUL:  return SDL_BLENDMODE_MOD;
        case GFX_BLEND_ALPHA:
        default:             return SDL_BLENDMODE_BLEND;
        }
    }

    bool ensureScratch(int w, int h)
    {
        if (m_scratch && m_scratchW >= w && m_scratchH >= h) return true;
        if (m_scratch) { SDL_DestroyTexture(m_scratch); m_scratch = nullptr; }
        m_scratchW = (w + 63) & ~63;
        m_scratchH = (h + 63) & ~63;
        if (m_scratchW < 64) m_scratchW = 64;
        if (m_scratchH < 64) m_scratchH = 64;
        // STATIC matches the path used for all game textures; the MMIYOO
        // driver has been proven to support CreateTexture + UpdateTexture.
        m_scratch = SDL_CreateTexture(m_r, SDL_PIXELFORMAT_ARGB8888,
                                      SDL_TEXTUREACCESS_STATIC,
                                      m_scratchW, m_scratchH);
        if (!m_scratch) {
            fprintf(stderr, "[gfx_sdl] scratch texture %dx%d failed: %s\n",
                    m_scratchW, m_scratchH, SDL_GetError());
            m_scratchW = m_scratchH = 0;
            return false;
        }
        return true;
    }

    void drawQuadTransformed(GfxTextureSDL* t, const GfxVertex* q)
    {
        f32 vx[4], vy[4];
        f32 minx = 1.0e30f, miny = 1.0e30f;
        f32 maxx = -1.0e30f, maxy = -1.0e30f;
        for (int i = 0; i < 4; ++i) {
            const f32 lx = m_xf[0] * q[i].x + m_xf[2] * q[i].y + m_xf[4];
            const f32 ly = m_xf[1] * q[i].x + m_xf[3] * q[i].y + m_xf[5];
            vx[i] = m_ox + lx * m_s;
            vy[i] = m_oy + ly * m_s;
            if (vx[i] < minx) minx = vx[i];
            if (vx[i] > maxx) maxx = vx[i];
            if (vy[i] < miny) miny = vy[i];
            if (vy[i] > maxy) maxy = vy[i];
        }

        int x0 = (int)floorf(minx), y0 = (int)floorf(miny);
        int x1 = (int)ceilf(maxx),  y1 = (int)ceilf(maxy);
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > m_w) x1 = m_w;
        if (y1 > m_h) y1 = m_h;
        const int w = x1 - x0, h = y1 - y0;
        if (w <= 0 || h <= 0) return;
        if (!ensureScratch(w, h)) return;

        m_scratchPix.assign((size_t)w * h, 0u);
        for (int py = 0; py < h; ++py) {
            const f32 fy = (f32)(y0 + py) + 0.5f;
            u32* row = &m_scratchPix[(size_t)py * w];
            for (int px = 0; px < w; ++px) {
                const f32 fx = (f32)(x0 + px) + 0.5f;
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
                int tx = (int)(u * (f32)t->w);
                int ty = (int)(v * (f32)t->h);
                if (tx < 0) tx = 0; else if (tx >= t->w) tx = t->w - 1;
                if (ty < 0) ty = 0; else if (ty >= t->h) ty = t->h - 1;
                row[px] = t->src[(size_t)ty * t->w + tx];
            }
        }

        const u32 c = q[0].color;
        const u8 r = (u8)((c >> 16) & 0xFF), g = (u8)((c >> 8) & 0xFF);
        const u8 b = (u8)(c & 0xFF), a = (u8)((c >> 24) & 0xFF);
        const double ts = plat::Time();
        SDL_SetTextureColorMod(m_scratch, r, g, b);
        SDL_SetTextureAlphaMod(m_scratch, a);
        SDL_SetTextureBlendMode(m_scratch, toSdlBlend(m_blend));
        m_tState += plat::Time() - ts;

        SDL_Rect rect{ 0, 0, w, h };
        SDL_Rect dst{ x0, y0, w, h };
        const double tr = plat::Time();
        const bool ok = SDL_UpdateTexture(m_scratch, &rect, &m_scratchPix[0], w * 4) == 0 &&
                        SDL_RenderCopy(m_r, m_scratch, &rect, &dst) == 0;
        if (!ok) {
            if (!m_failLogged) {
                fprintf(stderr, "[gfx_sdl] transformed copy failed: %s\n", SDL_GetError());
                m_failLogged = true;
            }
            ++m_drawFail;
        } else {
            ++m_drawOk;
        }
        m_tCopy += plat::Time() - tr;
        ++m_calls;
        m_pixels += (long)w * h;
    }

    void drawQuad(GfxTextureSDL* t, const GfxVertex* q)
    {
        if (m_xfActive) { drawQuadTransformed(t, q); return; }

        // Local quad is axis-aligned (TL,TR,BL,BR).
        // Map logical (960x540) coords -> window pixels (scale + letterbox).
        int x = (int)lroundf(m_ox + q[0].x * m_s);
        int y = (int)lroundf(m_oy + q[0].y * m_s);
        int w = (int)lroundf((q[1].x - q[0].x) * m_s);
        int h = (int)lroundf((q[2].y - q[0].y) * m_s);
        if (w <= 0) w = (int)lroundf((q[3].x - q[0].x) * m_s);
        if (h <= 0) h = (int)lroundf((q[3].y - q[0].y) * m_s);
        if (w <= 0 || h <= 0) return;

        f32 u0 = q[0].u, v0 = q[0].v, u1 = q[3].u, v1 = q[3].v;
        bool flip = false;
        if (u1 < u0) { f32 t2 = u0; u0 = u1; u1 = t2; flip = true; }
        if (v1 < v0) { f32 t2 = v0; v0 = v1; v1 = t2; }

        SDL_Rect src;
        src.x = (int)lroundf(u0 * t->w);
        src.y = (int)lroundf(v0 * t->h);
        src.w = (int)lroundf((u1 - u0) * t->w);
        src.h = (int)lroundf((v1 - v0) * t->h);
        if (src.w <= 0) src.w = 1;
        if (src.h <= 0) src.h = 1;
        if (src.x < 0) src.x = 0;
        if (src.y < 0) src.y = 0;
        if (src.x + src.w > t->w) src.w = t->w - src.x;
        if (src.y + src.h > t->h) src.h = t->h - src.y;
        if (src.w <= 0 || src.h <= 0) return;

        SDL_Rect dst{ x, y, w, h };
        const u32 c = q[0].color;
        const int slot = (flip && t->flip) ? 1 : 0;
        SDL_Texture* tt = slot ? t->flip : t->tex;
        const u8 r = (u8)((c >> 16) & 0xFF), g = (u8)((c >> 8) & 0xFF);
        const u8 b = (u8)(c & 0xFF), a = (u8)((c >> 24) & 0xFF);
        const double ts = plat::Time();
        if (!t->colorSet[slot] || t->cr[slot] != r || t->cg[slot] != g ||
            t->cb[slot] != b || t->ca[slot] != a) {
            SDL_SetTextureColorMod(tt, r, g, b);
            SDL_SetTextureAlphaMod(tt, a);
            t->cr[slot] = r; t->cg[slot] = g; t->cb[slot] = b; t->ca[slot] = a;
            t->colorSet[slot] = true;
        }
        const SDL_BlendMode bm = toSdlBlend(m_blend);
        if (!t->blendSet[slot] || t->cblend[slot] != bm) {
            SDL_SetTextureBlendMode(tt, bm);
            t->cblend[slot] = bm;
            t->blendSet[slot] = true;
        }
        m_tState += plat::Time() - ts;
        const double tr = plat::Time();
        if (SDL_RenderCopy(m_r, tt, &src, &dst) != 0) {
            if (!m_failLogged) {
                fprintf(stderr, "[gfx_sdl] RenderCopy failed: %s\n", SDL_GetError());
                m_failLogged = true;
            }
            ++m_drawFail;
        } else {
            ++m_drawOk;
        }
        m_tCopy += plat::Time() - tr;
        ++m_calls;
        m_pixels += (long)dst.w * dst.h;
    }

    SDL_Renderer* m_r = nullptr;
    GfxTextureSDL* m_white = nullptr;
    int m_maxTexW = 0, m_maxTexH = 0;
    float m_s = 1.0f, m_ox = 0.0f, m_oy = 0.0f;  // logical -> window scale + letterbox offset
    int m_w = 960, m_h = 540;
    GfxBlend m_blend = GFX_BLEND_ALPHA;
    GfxFilter m_filter = GFX_FILTER_NEAREST;
    f32 m_xf[6] = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    bool m_xfActive = false;
    SDL_Texture* m_scratch = nullptr;
    int m_scratchW = 0, m_scratchH = 0;
    std::vector<u32> m_scratchPix;
    long m_drawOk = 0, m_drawFail = 0, m_frameCount = 0;
    bool m_failLogged = false;
    // profiling
    double m_tClear = 0, m_tState = 0, m_tCopy = 0;
    long   m_calls = 0, m_pixels = 0;
    char m_info[128] = "gfx_sdl not initialised";
};

} // namespace

IGfx* CreateGfxSDL()
{
    return new GfxSDL();
}
