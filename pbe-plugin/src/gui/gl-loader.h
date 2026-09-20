#pragma once

#include <stdbool.h>

// OpenGL headers for the NanoVG backend, plus a loader for Windows.
//
// Linux (libGL) and macOS (OpenGL.framework) export every GL 2.x function we
// need. Windows' opengl32.dll only exports GL 1.1: anything newer must be
// fetched at runtime with a context current. The names are redirected below
// so that nanovg_gl.h, included after this header, uses the loaded pointers.

#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION 1
#include <pugl/gl.h>
#include <OpenGL/glext.h>
#else
#define GL_GLEXT_PROTOTYPES 1
#include <pugl/gl.h>
#include <GL/glext.h>
#endif

#if defined(_WIN32)
#define PBE_GL_FUNCS(X)                                                        \
  X(PFNGLACTIVETEXTUREPROC, glActiveTexture)                                   \
  X(PFNGLATTACHSHADERPROC, glAttachShader)                                     \
  X(PFNGLBINDATTRIBLOCATIONPROC, glBindAttribLocation)                         \
  X(PFNGLBINDBUFFERPROC, glBindBuffer)                                         \
  X(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate)                           \
  X(PFNGLBUFFERDATAPROC, glBufferData)                                         \
  X(PFNGLCOMPILESHADERPROC, glCompileShader)                                   \
  X(PFNGLCREATEPROGRAMPROC, glCreateProgram)                                   \
  X(PFNGLCREATESHADERPROC, glCreateShader)                                     \
  X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers)                                   \
  X(PFNGLDELETEPROGRAMPROC, glDeleteProgram)                                   \
  X(PFNGLDELETESHADERPROC, glDeleteShader)                                     \
  X(PFNGLDISABLEVERTEXATTRIBARRAYPROC, glDisableVertexAttribArray)             \
  X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray)               \
  X(PFNGLGENBUFFERSPROC, glGenBuffers)                                         \
  X(PFNGLGENERATEMIPMAPPROC, glGenerateMipmap)                                 \
  X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog)                           \
  X(PFNGLGETPROGRAMIVPROC, glGetProgramiv)                                     \
  X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)                             \
  X(PFNGLGETSHADERIVPROC, glGetShaderiv)                                       \
  X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation)                         \
  X(PFNGLLINKPROGRAMPROC, glLinkProgram)                                       \
  X(PFNGLSHADERSOURCEPROC, glShaderSource)                                     \
  X(PFNGLSTENCILOPSEPARATEPROC, glStencilOpSeparate)                           \
  X(PFNGLUNIFORM1IPROC, glUniform1i)                                           \
  X(PFNGLUNIFORM2FVPROC, glUniform2fv)                                         \
  X(PFNGLUNIFORM4FVPROC, glUniform4fv)                                         \
  X(PFNGLUSEPROGRAMPROC, glUseProgram)                                         \
  X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer)

#define PBE_GL_DECLARE(type, name) extern type pbe_##name;
PBE_GL_FUNCS(PBE_GL_DECLARE)
#undef PBE_GL_DECLARE

#define glActiveTexture pbe_glActiveTexture
#define glAttachShader pbe_glAttachShader
#define glBindAttribLocation pbe_glBindAttribLocation
#define glBindBuffer pbe_glBindBuffer
#define glBlendFuncSeparate pbe_glBlendFuncSeparate
#define glBufferData pbe_glBufferData
#define glCompileShader pbe_glCompileShader
#define glCreateProgram pbe_glCreateProgram
#define glCreateShader pbe_glCreateShader
#define glDeleteBuffers pbe_glDeleteBuffers
#define glDeleteProgram pbe_glDeleteProgram
#define glDeleteShader pbe_glDeleteShader
#define glDisableVertexAttribArray pbe_glDisableVertexAttribArray
#define glEnableVertexAttribArray pbe_glEnableVertexAttribArray
#define glGenBuffers pbe_glGenBuffers
#define glGenerateMipmap pbe_glGenerateMipmap
#define glGetProgramInfoLog pbe_glGetProgramInfoLog
#define glGetProgramiv pbe_glGetProgramiv
#define glGetShaderInfoLog pbe_glGetShaderInfoLog
#define glGetShaderiv pbe_glGetShaderiv
#define glGetUniformLocation pbe_glGetUniformLocation
#define glLinkProgram pbe_glLinkProgram
#define glShaderSource pbe_glShaderSource
#define glStencilOpSeparate pbe_glStencilOpSeparate
#define glUniform1i pbe_glUniform1i
#define glUniform2fv pbe_glUniform2fv
#define glUniform4fv pbe_glUniform4fv
#define glUseProgram pbe_glUseProgram
#define glVertexAttribPointer pbe_glVertexAttribPointer
#endif // _WIN32

// Resolves the function pointers above. Must be called with a GL context
// current. A no-op that returns true on platforms that need no loading.
bool GlLoader_Load(void);
