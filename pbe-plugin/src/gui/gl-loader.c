#include "gl-loader.h"

#if defined(_WIN32)
#include <windows.h>

#define PBE_GL_DEFINE(type, name) type pbe_##name = NULL;
PBE_GL_FUNCS(PBE_GL_DEFINE)
#undef PBE_GL_DEFINE

static void *load_gl_function(const char *name) {
  // wglGetProcAddress only knows extensions and post-1.1 core functions;
  // opengl32.dll is the fallback for the rest.
  void *p = (void *)puglGetProcAddress(name);
  if (p == NULL || p == (void *)1 || p == (void *)2 || p == (void *)3 || p == (void *)-1) {
    HMODULE module = GetModuleHandleA("opengl32.dll");
    p = module ? (void *)GetProcAddress(module, name) : NULL;
  }
  return p;
}

bool GlLoader_Load(void) {
  bool ok = true;
#define PBE_GL_LOAD(type, name)                                                \
  pbe_##name = (type)load_gl_function(#name);                                  \
  if (!pbe_##name) ok = false;
  PBE_GL_FUNCS(PBE_GL_LOAD)
#undef PBE_GL_LOAD
  return ok;
}
#else
bool GlLoader_Load(void) { return true; }
#endif
