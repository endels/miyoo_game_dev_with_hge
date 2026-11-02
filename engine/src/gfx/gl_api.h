#ifndef GL_API_H
#define GL_API_H

// Minimal, dependency-free OpenGL ES 2.0 / GL 2.1 loader.
//
// We deliberately do NOT include <GL/gl.h> or <GLES2/gl2.h>: the engine must
// build with zero SDK headers, and every entry point (even the GL 1.1 ones)
// is resolved through SDL_GL_GetProcAddress. That keeps Windows, Linux and
// embedded builds identical and avoids link-time differences.
//
// Supported floor: OpenGL ES 2.0 (all embedded GPUs) and desktop GL 2.1,
// which expose the same feature set for 2D sprite work.

#include <stdint.h>
#include <stddef.h>

typedef unsigned int   GLenum;
typedef unsigned char  GLboolean;
typedef unsigned int   GLbitfield;
typedef void           GLvoid;
typedef signed char    GLbyte;
typedef short          GLshort;
typedef int            GLint;
typedef int            GLsizei;
typedef unsigned char  GLubyte;
typedef unsigned short GLushort;
typedef unsigned int   GLuint;
typedef float          GLfloat;
typedef float          GLclampf;
typedef double         GLdouble;
typedef char           GLchar;
typedef ptrdiff_t      GLsizeiptr;
typedef ptrdiff_t      GLintptr;

// ---- constants (GLES2 core + the few GL2 items we use) ------------------
#define GL_FALSE                0
#define GL_TRUE                 1
#define GL_ZERO                 0
#define GL_ONE                  1
#define GL_TRIANGLES            0x0004
#define GL_BLEND                0x0BE2
#define GL_DEPTH_TEST           0x0B71
#define GL_CULL_FACE            0x0B44
#define GL_SCISSOR_TEST         0x0C11
#define GL_UNPACK_ALIGNMENT     0x0CF5
#define GL_PACK_ALIGNMENT       0x0D05
#define GL_MAX_TEXTURE_SIZE     0x0D33
#define GL_TEXTURE_2D           0x0DE1
#define GL_UNSIGNED_BYTE        0x1401
#define GL_UNSIGNED_SHORT       0x1403
#define GL_FLOAT                0x1406
#define GL_COLOR_BUFFER_BIT     0x4000
#define GL_VENDOR               0x1F00
#define GL_RENDERER             0x1F01
#define GL_VERSION              0x1F02
#define GL_LINEAR               0x2601
#define GL_NEAREST              0x2600
#define GL_TEXTURE_MAG_FILTER   0x2800
#define GL_TEXTURE_MIN_FILTER   0x2801
#define GL_TEXTURE_WRAP_S       0x2802
#define GL_TEXTURE_WRAP_T       0x2803
#define GL_SRC_COLOR            0x0300
#define GL_ONE_MINUS_SRC_COLOR  0x0301
#define GL_SRC_ALPHA            0x0302
#define GL_ONE_MINUS_SRC_ALPHA  0x0303
#define GL_DST_ALPHA            0x0304
#define GL_ONE_MINUS_DST_ALPHA  0x0305
#define GL_DST_COLOR            0x0306
#define GL_ONE_MINUS_DST_COLOR  0x0307
#define GL_RGBA                 0x1908
#define GL_FUNC_ADD             0x8006
#define GL_CLAMP_TO_EDGE        0x812F
#define GL_TEXTURE0             0x84C0
#define GL_ARRAY_BUFFER         0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STREAM_DRAW          0x88E0
#define GL_DYNAMIC_DRAW         0x88E8
#define GL_FRAGMENT_SHADER      0x8B30
#define GL_VERTEX_SHADER        0x8B31
#define GL_COMPILE_STATUS       0x8B81
#define GL_LINK_STATUS          0x8B82
#define GL_INFO_LOG_LENGTH      0x8B84
#define GL_FRAMEBUFFER          0x8D40
#define GL_RENDERBUFFER         0x8D41
#define GL_COLOR_ATTACHMENT0    0x8CE0
#define GL_DEPTH_ATTACHMENT     0x8D00
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5

namespace ggl {

typedef void* (*ProcGetter)(const char*);

struct Api {
    // raster state
    void      (*ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
    void      (*Clear)(GLbitfield);
    void      (*Viewport)(GLint, GLint, GLsizei, GLsizei);
    void      (*Scissor)(GLint, GLint, GLsizei, GLsizei);
    void      (*Enable)(GLenum);
    void      (*Disable)(GLenum);
    void      (*BlendFunc)(GLenum, GLenum);
    void      (*BlendEquation)(GLenum);
    void      (*PixelStorei)(GLenum, GLint);
    void      (*ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
    const GLubyte* (*GetString)(GLenum);
    void      (*GetIntegerv)(GLenum, GLint*);
    GLenum    (*GetError)(void);

    // textures
    void      (*GenTextures)(GLsizei, GLuint*);
    void      (*DeleteTextures)(GLsizei, const GLuint*);
    void      (*BindTexture)(GLenum, GLuint);
    void      (*TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint,
                            GLenum, GLenum, const void*);
    void      (*TexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei,
                               GLenum, GLenum, const void*);
    void      (*TexParameteri)(GLenum, GLenum, GLint);
    void      (*ActiveTexture)(GLenum);

    // buffers / attributes
    void      (*GenBuffers)(GLsizei, GLuint*);
    void      (*DeleteBuffers)(GLsizei, const GLuint*);
    void      (*BindBuffer)(GLenum, GLuint);
    void      (*BufferData)(GLenum, GLsizeiptr, const void*, GLenum);
    void      (*BufferSubData)(GLenum, GLintptr, GLsizeiptr, const void*);
    void      (*EnableVertexAttribArray)(GLuint);
    void      (*DisableVertexAttribArray)(GLuint);
    void      (*VertexAttribPointer)(GLuint, GLint, GLenum, GLboolean,
                                     GLsizei, const void*);
    void      (*DrawElements)(GLenum, GLsizei, GLenum, const void*);

    // shaders
    GLuint    (*CreateShader)(GLenum);
    void      (*ShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*);
    void      (*CompileShader)(GLuint);
    void      (*GetShaderiv)(GLuint, GLenum, GLint*);
    void      (*GetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
    void      (*DeleteShader)(GLuint);
    GLuint    (*CreateProgram)(void);
    void      (*AttachShader)(GLuint, GLuint);
    void      (*LinkProgram)(GLuint);
    void      (*GetProgramiv)(GLuint, GLenum, GLint*);
    void      (*GetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*);
    void      (*UseProgram)(GLuint);
    void      (*DeleteProgram)(GLuint);
    GLint     (*GetAttribLocation)(GLuint, const GLchar*);
    GLint     (*GetUniformLocation)(GLuint, const GLchar*);
    void      (*Uniform1i)(GLint, GLint);
    void      (*Uniform4f)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
    void      (*UniformMatrix4fv)(GLint, GLsizei, GLboolean, const GLfloat*);

    // framebuffer objects (render targets, used from M2 on)
    void      (*GenFramebuffers)(GLsizei, GLuint*);
    void      (*BindFramebuffer)(GLenum, GLuint);
    void      (*DeleteFramebuffers)(GLsizei, const GLuint*);
    void      (*FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
    GLenum    (*CheckFramebufferStatus)(GLenum);
};

extern Api gl;

// Resolves every entry point. Returns false (and fills missing) if something
// non-optional is unavailable.
bool Load(ProcGetter getProc, char* missing, int missingSize);

} // namespace ggl

#endif // GL_API_H
