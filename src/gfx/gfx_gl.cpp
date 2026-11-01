// OpenGL ES 2.0 / GL 2.1 renderer.
//
// Design notes that matter for porting HGE:
//  * HGE stores colours as 0xAARRGGBB, i.e. B,G,R,A in memory. We upload and
//    feed that layout straight through and swizzle .bgra in the shader, so no
//    per-pixel CPU work and no dependency on GL_BGRA (not core in ES2).
//  * HGE blits into its textures from the CPU (font atlases, sprite packing),
//    so every texture keeps a RAM shadow copy; the GL texture is only updated
//    on unlock/upload. Reading back from GLES would be far too slow.
//  * All geometry goes through one dynamic VBO and one draw call per state
//    change, replacing HGE's DrawPrimitiveUP-per-quad.

#include "gfx/igfx.h"
#include "gfx/gl_api.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

const int kMaxQuads = 4096;

class GfxTextureGL : public IGfxTexture {
public:
    GLuint id;
    int    w, h;
    u8*    shadow;      // 0xAARRGGBB pixels, PITCH = w*4
    GfxTextureGL() : id(0), w(0), h(0), shadow(0) {}
    ~GfxTextureGL() { delete[] shadow; }
};

// ---- small column-major 4x4 helpers -------------------------------------
void matIdentity(f32* m)
{
    memset(m, 0, sizeof(f32) * 16);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

// out = a * b
void matMul(f32* out, const f32* a, const f32* b)
{
    f32 r[16];
    for (int c = 0; c < 4; ++c) {
        for (int row = 0; row < 4; ++row) {
            r[c * 4 + row] = a[0 * 4 + row] * b[c * 4 + 0] +
                             a[1 * 4 + row] * b[c * 4 + 1] +
                             a[2 * 4 + row] * b[c * 4 + 2] +
                             a[3 * 4 + row] * b[c * 4 + 3];
        }
    }
    memcpy(out, r, sizeof(r));
}

// top-left origin, y down
void matOrtho(f32* m, f32 w, f32 h)
{
    matIdentity(m);
    m[0]  =  2.0f / w;
    m[5]  = -2.0f / h;
    m[12] = -1.0f;
    m[13] =  1.0f;
}

// scale+rotate about (px,py), then translate by (dx,dy)
void matAffine2D(f32* m, f32 px, f32 py, f32 dx, f32 dy,
                 f32 rot, f32 hs, f32 vs)
{
    if (hs == 0.0f) hs = 1.0f;
    if (vs == 0.0f) vs = 1.0f;
    const f32 c = cosf(rot), s = sinf(rot);
    const f32 a =  c * hs, b = s * hs;
    const f32 cc = -s * vs, d = c * vs;
    const f32 e = px + dx - (a * px + cc * py);
    const f32 f = py + dy - (b * px + d  * py);

    matIdentity(m);
    m[0] = a;  m[1] = b;
    m[4] = cc; m[5] = d;
    m[12] = e; m[13] = f;
}

// ---- shaders ------------------------------------------------------------
const char* kVS_GL =
    "#version 120\n"
    "attribute vec3 aPos;\n"
    "attribute vec4 aColor;\n"      // uploaded as B,G,R,A bytes
    "attribute vec2 aUV;\n"
    "uniform mat4 uMVP;\n"
    "varying vec4 vColor;\n"
    "varying vec2 vUV;\n"
    "void main() {\n"
    "    gl_Position = uMVP * vec4(aPos, 1.0);\n"
    "    vColor = aColor.bgra;\n"   // -> R,G,B,A
    "    vUV = aUV;\n"
    "}\n";

const char* kFS_GL =
    "#version 120\n"
    "uniform sampler2D uTex;\n"
    "varying vec4 vColor;\n"
    "varying vec2 vUV;\n"
    "void main() {\n"
    "    gl_FragColor = vColor * texture2D(uTex, vUV).bgra;\n"
    "}\n";

const char* kVS_ES =
    "#version 100\n"
    "precision highp float;\n"
    "attribute vec3 aPos;\n"
    "attribute vec4 aColor;\n"
    "attribute vec2 aUV;\n"
    "uniform mat4 uMVP;\n"
    "varying vec4 vColor;\n"
    "varying vec2 vUV;\n"
    "void main() {\n"
    "    gl_Position = uMVP * vec4(aPos, 1.0);\n"
    "    vColor = aColor.bgra;\n"
    "    vUV = aUV;\n"
    "}\n";

const char* kFS_ES =
    "#version 100\n"
    "precision mediump float;\n"
    "uniform sampler2D uTex;\n"
    "varying vec4 vColor;\n"
    "varying vec2 vUV;\n"
    "void main() {\n"
    "    gl_FragColor = vColor * texture2D(uTex, vUV).bgra;\n"
    "}\n";

GLuint compileShader(GLenum type, const char* src)
{
    GLuint sh = ggl::gl.CreateShader(type);
    ggl::gl.ShaderSource(sh, 1, &src, 0);
    ggl::gl.CompileShader(sh);
    GLint ok = 0;
    ggl::gl.GetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048] = {0};
        ggl::gl.GetShaderInfoLog(sh, sizeof(log) - 1, 0, log);
        fprintf(stderr, "[gfx] shader compile failed:\n%s\n", log);
        ggl::gl.DeleteShader(sh);
        return 0;
    }
    return sh;
}

GLuint buildProgram()
{
#ifdef HGE_GLES
    const char* vs = kVS_ES;
    const char* fs = kFS_ES;
#else
    const char* vs = kVS_GL;
    const char* fs = kFS_GL;
#endif
    GLuint v = compileShader(GL_VERTEX_SHADER, vs);
    GLuint f = compileShader(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return 0;

    GLuint p = ggl::gl.CreateProgram();
    ggl::gl.AttachShader(p, v);
    ggl::gl.AttachShader(p, f);
    ggl::gl.LinkProgram(p);
    ggl::gl.DeleteShader(v);
    ggl::gl.DeleteShader(f);

    GLint ok = 0;
    ggl::gl.GetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048] = {0};
        ggl::gl.GetProgramInfoLog(p, sizeof(log) - 1, 0, log);
        fprintf(stderr, "[gfx] program link failed:\n%s\n", log);
        ggl::gl.DeleteProgram(p);
        return 0;
    }
    return p;
}

void copyRows(u8* dst, const u8* src, int w, int h, int srcPitch)
{
    const int rowBytes = w * 4;
    if (srcPitch == rowBytes) {
        memcpy(dst, src, (size_t)rowBytes * h);
    } else {
        for (int y = 0; y < h; ++y)
            memcpy(dst + (size_t)y * rowBytes, src + (size_t)y * srcPitch, rowBytes);
    }
}

class GfxGL : public IGfx {
public:
    GfxGL()
        : m_vbo(0), m_ibo(0), m_iboTri(0), m_prog(0), m_uMVP(-1), m_uTex(-1),
          m_aPos(-1), m_aColor(-1), m_aUV(-1),
          m_w(0), m_h(0), m_vertCount(0), m_quadCount(0),
          m_curTex(0), m_blend(GFX_BLEND_ALPHA), m_filter(GFX_FILTER_NEAREST),
          m_scissorOn(false), m_sx(0), m_sy(0), m_sw(0), m_sh(0),
          m_maxTex(0), m_white(0), m_infoReady(false)
    {
        memset(m_verts, 0, sizeof(m_verts));
        memset(m_idx, 0, sizeof(m_idx));
        matIdentity(m_proj);
        matIdentity(m_xform);
        matIdentity(m_mvp);
        m_info[0] = 0;
        m_dropped = 0;
    }

    bool Init(void* nativeWindow, GfxProcGetter getProc) override
    {
        (void)nativeWindow;
        char missing[512] = {0};
        if (!ggl::Load(getProc, missing, sizeof(missing))) {
            fprintf(stderr, "[gfx] missing GL entry points: %s\n", missing);
            return false;
        }

        m_prog = buildProgram();
        if (!m_prog) return false;

        m_aPos   = ggl::gl.GetAttribLocation(m_prog, "aPos");
        m_aColor = ggl::gl.GetAttribLocation(m_prog, "aColor");
        m_aUV    = ggl::gl.GetAttribLocation(m_prog, "aUV");
        m_uMVP   = ggl::gl.GetUniformLocation(m_prog, "uMVP");
        m_uTex   = ggl::gl.GetUniformLocation(m_prog, "uTex");

        // index buffer: 2 triangles per quad, TL,TR,BL,BR
        for (int q = 0; q < kMaxQuads; ++q) {
            const GLushort b = (GLushort)(q * 4);
            GLushort* d = &m_idx[q * 6];
            d[0] = b + 0; d[1] = b + 1; d[2] = b + 2;
            d[3] = b + 1; d[4] = b + 3; d[5] = b + 2;
        }

        ggl::gl.GenBuffers(1, &m_vbo);
        ggl::gl.BindBuffer(GL_ARRAY_BUFFER, m_vbo);
        ggl::gl.BufferData(GL_ARRAY_BUFFER, sizeof(GfxVertex) * kMaxQuads * 4,
                           nullptr, GL_DYNAMIC_DRAW);

        ggl::gl.GenBuffers(1, &m_ibo);
        ggl::gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo);
        ggl::gl.BufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(m_idx), m_idx, GL_DYNAMIC_DRAW);

        ggl::gl.GenBuffers(1, &m_iboTri);
        ggl::gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_iboTri);
        ggl::gl.BufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(m_triIdx), nullptr, GL_DYNAMIC_DRAW);

        ggl::gl.GetIntegerv(GL_MAX_TEXTURE_SIZE, &m_maxTex);

        ggl::gl.Disable(GL_DEPTH_TEST);
        ggl::gl.Disable(GL_CULL_FACE);
        ggl::gl.Enable(GL_BLEND);
        applyBlend();
        ggl::gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
        ggl::gl.PixelStorei(GL_PACK_ALIGNMENT, 1);

        createWhiteTexture();
        buildInfo();
        return true;
    }

    void Shutdown() override
    {
        if (m_white) { GfxTextureGL* t = static_cast<GfxTextureGL*>(m_white); delete t; m_white = 0; }
        if (m_iboTri) ggl::gl.DeleteBuffers(1, &m_iboTri);
        if (m_ibo) ggl::gl.DeleteBuffers(1, &m_ibo);
        if (m_vbo) ggl::gl.DeleteBuffers(1, &m_vbo);
        if (m_prog) ggl::gl.DeleteProgram(m_prog);
        m_iboTri = m_ibo = m_vbo = m_prog = 0;
    }

    void OnResize(int width, int height) override
    {
        m_w = width; m_h = height;
        if (m_w < 1) m_w = 1;
        if (m_h < 1) m_h = 1;
        ggl::gl.Viewport(0, 0, m_w, m_h);
        matOrtho(m_proj, (f32)m_w, (f32)m_h);
        recomputeMVP();
        applyScissor();
    }

    void BeginFrame() override
    {
        m_vertCount = m_quadCount = 0;
    }

    void EndFrame() override { flush(); }

    void Clear(u32 argb) override
    {
        flush();
        const bool hadScissor = m_scissorOn;
        if (hadScissor) ggl::gl.Disable(GL_SCISSOR_TEST);
        ggl::gl.ClearColor(((argb >> 16) & 0xFF) / 255.0f,
                           ((argb >> 8) & 0xFF) / 255.0f,
                           (argb & 0xFF) / 255.0f,
                           ((argb >> 24) & 0xFF) / 255.0f);
        ggl::gl.Clear(GL_COLOR_BUFFER_BIT);
        if (hadScissor) applyScissor();
    }

    void SetScissor(int x, int y, int w, int h) override
    {
        flush();
        m_sx = x; m_sy = y; m_sw = w; m_sh = h;
        m_scissorOn = (w > 0 && h > 0);
        applyScissor();
    }

    void SetTransform(f32 x, f32 y, f32 dx, f32 dy,
                      f32 rot, f32 hscale, f32 vscale) override
    {
        flush();
        matAffine2D(m_xform, x, y, dx, dy, rot, hscale, vscale);
        recomputeMVP();
    }

    void SetBlend(GfxBlend mode) override
    {
        if (mode == m_blend) return;
        flush();
        m_blend = mode;
        applyBlend();
    }

    void SetTextureFilter(GfxFilter filter) override
    {
        m_filter = filter;
        if (m_curTex) {
            GfxTextureGL* t = static_cast<GfxTextureGL*>(m_curTex);
            bindTexture(t);
        }
    }

    void DrawQuads(IGfxTexture* tex, const GfxVertex* verts, int quadCount) override
    {
        if (!verts || quadCount <= 0) return;
        for (int i = 0; i < quadCount; ++i) {
            if (m_quadCount >= kMaxQuads) flush();
            if (tex != m_curTex)  { flush(); bindTexture(static_cast<GfxTextureGL*>(tex)); }
            GfxVertex* dst = &m_verts[m_vertCount];
            memcpy(dst, verts + i * 4, sizeof(GfxVertex) * 4);
            m_vertCount += 4;
            m_quadCount += 1;
        }
    }

    void DrawTriangles(IGfxTexture* tex, const GfxVertex* verts, int vertexCount) override
    {
        // Triangles are rare in 2D (HGE's Gfx_RenderTriple). ES2 forbids
        // client-side arrays, so they go through their own index buffer to
        // avoid clobbering the quad index buffer used by flush().
        if (!verts || vertexCount < 3) return;
        flush();
        if (tex != m_curTex) bindTexture(static_cast<GfxTextureGL*>(tex));

        ggl::gl.UseProgram(m_prog);
        ggl::gl.UniformMatrix4fv(m_uMVP, 1, GL_FALSE, m_mvp);
        ggl::gl.Uniform1i(m_uTex, 0);
        ggl::gl.ActiveTexture(GL_TEXTURE0);
        ggl::gl.BindTexture(GL_TEXTURE_2D, m_curTex ? static_cast<GfxTextureGL*>(m_curTex)->id
                                                   : whiteId());

        ggl::gl.BindBuffer(GL_ARRAY_BUFFER, m_vbo);
        ggl::gl.BufferData(GL_ARRAY_BUFFER, sizeof(GfxVertex) * vertexCount, verts, GL_STREAM_DRAW);
        setupAttribs();

        ggl::gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_iboTri);
        for (int i = 0; i < vertexCount; ++i) m_triIdx[i] = (GLushort)i;
        ggl::gl.BufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0,
                              (GLsizeiptr)(sizeof(GLushort) * vertexCount), &m_triIdx[0]);
        ggl::gl.DrawElements(GL_TRIANGLES, vertexCount, GL_UNSIGNED_SHORT, (const void*)0);
    }

    IGfxTexture* CreateTexture(int w, int h, const void* argb, int pitch) override
    {
        if (w <= 0 || h <= 0) return 0;
        if (m_maxTex && (w > m_maxTex || h > m_maxTex)) {
            fprintf(stderr, "[gfx] texture %dx%d exceeds GL_MAX_TEXTURE_SIZE %d\n", w, h, m_maxTex);
            return 0;
        }
        GfxTextureGL* t = new GfxTextureGL();
        t->w = w; t->h = h;
        t->shadow = new u8[(size_t)w * h * 4];
        memset(t->shadow, 0, (size_t)w * h * 4);
        if (argb) copyRows(t->shadow, static_cast<const u8*>(argb), w, h,
                           pitch > 0 ? pitch : w * 4);

        ggl::gl.GenTextures(1, &t->id);
        ggl::gl.BindTexture(GL_TEXTURE_2D, t->id);
        // ARGB memory == B,G,R,A; uploaded as RGBA and swizzled in the shader.
        ggl::gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                           GL_RGBA, GL_UNSIGNED_BYTE, t->shadow);
        applyTextureParams();
        return t;
    }

    bool UpdateTexture(IGfxTexture* tex, int x, int y, int w, int h,
                       const void* argb, int pitch) override
    {
        if (!tex || !argb || w <= 0 || h <= 0) return false;
        GfxTextureGL* t = static_cast<GfxTextureGL*>(tex);
        if (x < 0 || y < 0 || x + w > t->w || y + h > t->h) return false;

        const int dstPitch = t->w * 4;
        copyRows(t->shadow + (size_t)y * dstPitch + (size_t)x * 4,
                 static_cast<const u8*>(argb), w, h, pitch > 0 ? pitch : w * 4);

        ggl::gl.BindTexture(GL_TEXTURE_2D, t->id);
        ggl::gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
        ggl::gl.TexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h,
                              GL_RGBA, GL_UNSIGNED_BYTE, argb);
        return true;
    }

    void ReleaseTexture(IGfxTexture* tex) override
    {
        if (!tex) return;
        GfxTextureGL* t = static_cast<GfxTextureGL*>(tex);
        if (t == m_curTex) m_curTex = 0;
        if (t->id) ggl::gl.DeleteTextures(1, &t->id);
        delete t;
    }

    int TextureWidth(IGfxTexture* t)  override { return t ? static_cast<GfxTextureGL*>(t)->w : 0; }
    int TextureHeight(IGfxTexture* t) override { return t ? static_cast<GfxTextureGL*>(t)->h : 0; }

    bool ReadPixels(int x, int y, int w, int h, u32* outArgb) override
    {
        if (!outArgb || w <= 0 || h <= 0) return false;
        if (x < 0 || y < 0 || x + w > m_w || y + h > m_h) return false;
        flush();

        std::vector<u8> tmp((size_t)w * h * 4);
        ggl::gl.PixelStorei(GL_PACK_ALIGNMENT, 1);
        ggl::gl.ReadPixels(x, m_h - y - h, w, h, GL_RGBA, GL_UNSIGNED_BYTE, &tmp[0]);

        for (int j = 0; j < h; ++j) {
            const u8* src = &tmp[(size_t)(h - 1 - j) * w * 4];
            u32* dst = outArgb + (size_t)j * w;
            for (int i = 0; i < w; ++i) {
                dst[i] = ((u32)src[i * 4 + 3] << 24) |
                         ((u32)src[i * 4 + 0] << 16) |
                         ((u32)src[i * 4 + 1] << 8)  |
                         ((u32)src[i * 4 + 2]);
            }
        }
        return true;
    }

    const char* Info() const override
    {
        return m_infoReady ? m_info : "gfx not initialised";
    }

    // exposed for debugging
    int droppedQuads() const { return m_dropped; }

private:
    GLuint whiteId() const
    {
        return m_white ? static_cast<GfxTextureGL*>(m_white)->id : 0;
    }

    void createWhiteTexture()
    {
        u32 px = 0xFFFFFFFF;
        m_white = CreateTexture(1, 1, &px, 4);
    }

    void bindTexture(GfxTextureGL* t)
    {
        m_curTex = t;
        if (!t) return;
        ggl::gl.BindTexture(GL_TEXTURE_2D, t->id);
        ggl::gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                              m_filter == GFX_FILTER_LINEAR ? GL_LINEAR : GL_NEAREST);
        ggl::gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                              m_filter == GFX_FILTER_LINEAR ? GL_LINEAR : GL_NEAREST);
    }

    void applyTextureParams()
    {
        // NPOT textures are everywhere (768x768, 960x1152, ...) and ES2 only
        // guarantees CLAMP_TO_EDGE + no mipmaps for them. Lock that in.
        ggl::gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        ggl::gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        ggl::gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                              m_filter == GFX_FILTER_LINEAR ? GL_LINEAR : GL_NEAREST);
        ggl::gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                              m_filter == GFX_FILTER_LINEAR ? GL_LINEAR : GL_NEAREST);
    }

    void applyBlend()
    {
        switch (m_blend) {
        case GFX_BLEND_NONE:
            ggl::gl.Disable(GL_BLEND);
            return;
        case GFX_BLEND_ADD:
            ggl::gl.Enable(GL_BLEND);
            ggl::gl.BlendFunc(GL_SRC_ALPHA, GL_ONE);
            break;
        case GFX_BLEND_MUL:
            ggl::gl.Enable(GL_BLEND);
            ggl::gl.BlendFunc(GL_DST_COLOR, GL_ZERO);
            break;
        case GFX_BLEND_ALPHA:
        default:
            ggl::gl.Enable(GL_BLEND);
            ggl::gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            break;
        }
        if (ggl::gl.BlendEquation) ggl::gl.BlendEquation(GL_FUNC_ADD);
    }

    void applyScissor()
    {
        if (m_scissorOn) {
            ggl::gl.Enable(GL_SCISSOR_TEST);
            ggl::gl.Scissor(m_sx, m_h - m_sy - m_sh, m_sw, m_sh);
        } else {
            ggl::gl.Disable(GL_SCISSOR_TEST);
        }
    }

    void recomputeMVP()
    {
        matMul(m_mvp, m_proj, m_xform);
    }

    void setupAttribs()
    {
        ggl::gl.EnableVertexAttribArray(m_aPos);
        ggl::gl.VertexAttribPointer(m_aPos, 3, GL_FLOAT, GL_FALSE,
                                    (GLsizei)sizeof(GfxVertex), (const void*)0);
        ggl::gl.EnableVertexAttribArray(m_aColor);
        ggl::gl.VertexAttribPointer(m_aColor, 4, GL_UNSIGNED_BYTE, GL_TRUE,
                                    (GLsizei)sizeof(GfxVertex), (const void*)12);
        ggl::gl.EnableVertexAttribArray(m_aUV);
        ggl::gl.VertexAttribPointer(m_aUV, 2, GL_FLOAT, GL_FALSE,
                                    (GLsizei)sizeof(GfxVertex), (const void*)16);
    }

    void flush()
    {
        if (m_quadCount == 0) return;

        ggl::gl.UseProgram(m_prog);
        ggl::gl.UniformMatrix4fv(m_uMVP, 1, GL_FALSE, m_mvp);
        ggl::gl.Uniform1i(m_uTex, 0);
        ggl::gl.ActiveTexture(GL_TEXTURE0);
        ggl::gl.BindTexture(GL_TEXTURE_2D, m_curTex ? static_cast<GfxTextureGL*>(m_curTex)->id
                                                   : whiteId());

        ggl::gl.BindBuffer(GL_ARRAY_BUFFER, m_vbo);
        ggl::gl.BufferSubData(GL_ARRAY_BUFFER, 0,
                              (GLsizeiptr)(sizeof(GfxVertex) * m_vertCount), m_verts);
        setupAttribs();

        ggl::gl.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ibo);
        ggl::gl.DrawElements(GL_TRIANGLES, m_quadCount * 6, GL_UNSIGNED_SHORT, (const void*)0);

        m_vertCount = 0;
        m_quadCount = 0;
    }

    void buildInfo()
    {
        const char* ver = (const char*)ggl::gl.GetString(GL_VERSION);
        const char* ren = (const char*)ggl::gl.GetString(GL_RENDERER);
        const char* ven = (const char*)ggl::gl.GetString(GL_VENDOR);
        snprintf(m_info, sizeof(m_info),
                 "GL %s | %s | %s | maxtex %d",
                 ver ? ver : "?", ren ? ren : "?", ven ? ven : "?", m_maxTex);
        m_infoReady = true;
    }

    GLuint m_vbo, m_ibo, m_iboTri, m_prog;
    GLint  m_uMVP, m_uTex, m_aPos, m_aColor, m_aUV;
    int    m_w, m_h;

    GfxVertex m_verts[kMaxQuads * 4];
    GLushort  m_idx[kMaxQuads * 6];
    GLushort  m_triIdx[kMaxQuads * 4];
    int       m_vertCount;
    int       m_quadCount;

    IGfxTexture* m_curTex;
    GfxBlend     m_blend;
    GfxFilter    m_filter;

    bool m_scissorOn;
    int  m_sx, m_sy, m_sw, m_sh;

    int    m_maxTex;
    int    m_dropped;

    f32 m_proj[16];
    f32 m_xform[16];
    f32 m_mvp[16];

    IGfxTexture* m_white;
    char         m_info[256];
    bool         m_infoReady;
};

} // namespace

IGfx* CreateGfxGL()
{
    return new GfxGL();
}
