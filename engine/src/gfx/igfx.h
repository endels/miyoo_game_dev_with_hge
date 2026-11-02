#ifndef IGFX_H
#define IGFX_H

// Renderer interface. This is the seam that replaces HGE's Direct3D 8 usage:
// HGE 1.81 exposes exactly 10 Gfx_* + 7 Texture_* functions, all of which map
// onto the methods below. Nothing above this layer knows which API is used.

#include "hge_types.h"

// Same layout as HGE's internal vertex (3 floats + 0xAARRGGBB + 2 floats).
// 24 bytes on every platform. Keep it POD: HGE blits these arrays verbatim.
struct GfxVertex {
    f32 x, y, z;
    u32 color;   // 0xAARRGGBB
    f32 u, v;
};

enum GfxBlend {
    GFX_BLEND_NONE = 0,   // opaque
    GFX_BLEND_ALPHA,      // src*a + dst*(1-a)
    GFX_BLEND_ADD,        // src*a + dst
    GFX_BLEND_MUL         // src*dst
};

enum GfxFilter {
    GFX_FILTER_NEAREST = 0,
    GFX_FILTER_LINEAR
};

typedef void* (*GfxProcGetter)(const char*);

// Opaque to callers; the backend derives its own object from this.
class IGfxTexture {
public:
    virtual ~IGfxTexture() {}
};

class IGfx {
public:
    virtual ~IGfx() {}

    virtual bool Init(void* nativeWindow, GfxProcGetter getProc) = 0;
    virtual void Shutdown() = 0;
    virtual void OnResize(int width, int height) = 0;

    virtual void BeginFrame() = 0;
    virtual void EndFrame() = 0;          // flushes the batch
    virtual void Clear(u32 argb) = 0;

    // x,y,w,h in top-left origin pixels; w<=0 disables clipping.
    virtual void SetScissor(int x, int y, int w, int h) = 0;

    // Same semantics as HGE Gfx_SetTransform: scale and rotate about (x,y),
    // then translate by (dx,dy). hscale/vscale == 0 means 1.
    virtual void SetTransform(f32 x, f32 y, f32 dx, f32 dy,
                              f32 rot, f32 hscale, f32 vscale) = 0;

    virtual void SetBlend(GfxBlend mode) = 0;
    virtual void SetTextureFilter(GfxFilter filter) = 0;

    // quadCount quads, 4 vertices each, order TL,TR,BL,BR
    virtual void DrawQuads(IGfxTexture* tex, const GfxVertex* verts, int quadCount) = 0;
    virtual void DrawTriangles(IGfxTexture* tex, const GfxVertex* verts, int vertexCount) = 0;

    // argb: 0xAARRGGBB pixels, pitch in bytes (0 = width*4)
    virtual IGfxTexture* CreateTexture(int w, int h, const void* argb, int pitch) = 0;
    virtual bool UpdateTexture(IGfxTexture* t, int x, int y, int w, int h,
                               const void* argb, int pitch) = 0;
    virtual void ReleaseTexture(IGfxTexture* t) = 0;
    virtual int  TextureWidth(IGfxTexture* t) = 0;
    virtual int  TextureHeight(IGfxTexture* t) = 0;

    // Reads back RGBA8 in top-left origin, converting to 0xAARRGGBB.
    virtual bool ReadPixels(int x, int y, int w, int h, u32* outArgb) = 0;

    virtual const char* Info() const = 0;
};

IGfx* CreateGfxGL();

#endif // IGFX_H
