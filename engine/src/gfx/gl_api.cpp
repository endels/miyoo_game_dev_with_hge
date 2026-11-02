#include "gfx/gl_api.h"
#include <string.h>
#include <stdio.h>

namespace {

ggl::ProcGetter g_get = nullptr;

void appendMissing(char* buf, int cap, const char* name)
{
    if (!buf || cap <= 0) return;
    size_t len = strlen(buf);
    if (len && len + 1 < (size_t)cap) { buf[len++] = ','; buf[len] = 0; }
    size_t room = (size_t)cap - len - 1;
    size_t need = strlen(name);
    if (need > room) need = room;
    strncat(buf, name, need);
}

template <typename T>
bool loadFn(T& fn, const char* name, char* missing, int cap, bool optional = false)
{
    fn = reinterpret_cast<T>(g_get ? g_get(name) : nullptr);
    if (!fn && !optional) appendMissing(missing, cap, name);
    return fn != nullptr;
}

} // namespace

namespace ggl {

Api gl;

bool Load(ProcGetter getProc, char* missing, int missingSize)
{
    g_get = getProc;
    if (missing && missingSize > 0) missing[0] = 0;
    if (!g_get) { appendMissing(missing, missingSize, "no-proc-getter"); return false; }

    memset(&gl, 0, sizeof(gl));
    bool ok = true;

    ok &= loadFn(gl.ClearColor,      "glClearColor",      missing, missingSize);
    ok &= loadFn(gl.Clear,           "glClear",           missing, missingSize);
    ok &= loadFn(gl.Viewport,        "glViewport",        missing, missingSize);
    ok &= loadFn(gl.Scissor,         "glScissor",         missing, missingSize);
    ok &= loadFn(gl.Enable,          "glEnable",          missing, missingSize);
    ok &= loadFn(gl.Disable,         "glDisable",         missing, missingSize);
    ok &= loadFn(gl.BlendFunc,       "glBlendFunc",       missing, missingSize);
    ok &= loadFn(gl.BlendEquation,   "glBlendEquation",   missing, missingSize);
    ok &= loadFn(gl.PixelStorei,     "glPixelStorei",     missing, missingSize);
    ok &= loadFn(gl.ReadPixels,      "glReadPixels",      missing, missingSize);
    ok &= loadFn(gl.GetString,       "glGetString",       missing, missingSize);
    ok &= loadFn(gl.GetIntegerv,     "glGetIntegerv",     missing, missingSize);
    ok &= loadFn(gl.GetError,        "glGetError",        missing, missingSize);

    ok &= loadFn(gl.GenTextures,     "glGenTextures",     missing, missingSize);
    ok &= loadFn(gl.DeleteTextures,  "glDeleteTextures",  missing, missingSize);
    ok &= loadFn(gl.BindTexture,     "glBindTexture",     missing, missingSize);
    ok &= loadFn(gl.TexImage2D,      "glTexImage2D",      missing, missingSize);
    ok &= loadFn(gl.TexSubImage2D,   "glTexSubImage2D",   missing, missingSize);
    ok &= loadFn(gl.TexParameteri,   "glTexParameteri",   missing, missingSize);
    ok &= loadFn(gl.ActiveTexture,   "glActiveTexture",   missing, missingSize);

    ok &= loadFn(gl.GenBuffers,      "glGenBuffers",      missing, missingSize);
    ok &= loadFn(gl.DeleteBuffers,   "glDeleteBuffers",   missing, missingSize);
    ok &= loadFn(gl.BindBuffer,      "glBindBuffer",      missing, missingSize);
    ok &= loadFn(gl.BufferData,      "glBufferData",      missing, missingSize);
    ok &= loadFn(gl.BufferSubData,   "glBufferSubData",   missing, missingSize);
    ok &= loadFn(gl.EnableVertexAttribArray,  "glEnableVertexAttribArray",  missing, missingSize);
    ok &= loadFn(gl.DisableVertexAttribArray, "glDisableVertexAttribArray", missing, missingSize);
    ok &= loadFn(gl.VertexAttribPointer,      "glVertexAttribPointer",      missing, missingSize);
    ok &= loadFn(gl.DrawElements,    "glDrawElements",    missing, missingSize);

    ok &= loadFn(gl.CreateShader,    "glCreateShader",    missing, missingSize);
    ok &= loadFn(gl.ShaderSource,    "glShaderSource",    missing, missingSize);
    ok &= loadFn(gl.CompileShader,   "glCompileShader",   missing, missingSize);
    ok &= loadFn(gl.GetShaderiv,     "glGetShaderiv",     missing, missingSize);
    ok &= loadFn(gl.GetShaderInfoLog,"glGetShaderInfoLog",missing, missingSize);
    ok &= loadFn(gl.DeleteShader,    "glDeleteShader",    missing, missingSize);
    ok &= loadFn(gl.CreateProgram,   "glCreateProgram",   missing, missingSize);
    ok &= loadFn(gl.AttachShader,    "glAttachShader",    missing, missingSize);
    ok &= loadFn(gl.LinkProgram,     "glLinkProgram",     missing, missingSize);
    ok &= loadFn(gl.GetProgramiv,    "glGetProgramiv",    missing, missingSize);
    ok &= loadFn(gl.GetProgramInfoLog,"glGetProgramInfoLog",missing, missingSize);
    ok &= loadFn(gl.UseProgram,      "glUseProgram",      missing, missingSize);
    ok &= loadFn(gl.DeleteProgram,   "glDeleteProgram",   missing, missingSize);
    ok &= loadFn(gl.GetAttribLocation,  "glGetAttribLocation",  missing, missingSize);
    ok &= loadFn(gl.GetUniformLocation, "glGetUniformLocation", missing, missingSize);
    ok &= loadFn(gl.Uniform1i,       "glUniform1i",       missing, missingSize);
    ok &= loadFn(gl.Uniform4f,       "glUniform4f",       missing, missingSize);
    ok &= loadFn(gl.UniformMatrix4fv,"glUniformMatrix4fv",missing, missingSize);

    // Render targets are only needed from M2 on, and the entry points differ
    // between GL 3.0 and the EXT/ARB variants on older desktop drivers.
    loadFn(gl.GenFramebuffers,          "glGenFramebuffers",          missing, missingSize, true);
    loadFn(gl.BindFramebuffer,          "glBindFramebuffer",          missing, missingSize, true);
    loadFn(gl.DeleteFramebuffers,       "glDeleteFramebuffers",       missing, missingSize, true);
    loadFn(gl.FramebufferTexture2D,     "glFramebufferTexture2D",     missing, missingSize, true);
    loadFn(gl.CheckFramebufferStatus,   "glCheckFramebufferStatus",   missing, missingSize, true);

    return ok;
}

} // namespace ggl
