// CPLUG GUI entry points, implemented with pugl (windowing/embedding) and
// the NanoVG canvas. This is the only GUI file that knows about CPLUG,
// pugl, threads, or OpenGL; the editor and widgets are backend-agnostic.
//
// Threading: on Windows and macOS the host's UI thread drives the view via
// the message loop / NSView callbacks and a pugl timer supplies periodic
// ticks. On Linux (X11) the host provides no run loop for embedded views, so
// the GUI runs a thread of its own that pumps puglUpdate() at ~60 Hz and
// owns the view for its whole life.
#include <cplug.h>
#include <stdlib.h>
#include <string.h>

#include <pugl/pugl.h>

#include "canvas-nanovg.h"
#include "config.h"
#include "editor.h"
#include "font-data.h"
#include "gl-loader.h" // GL headers (glViewport / glClear)
#include "platform.h"
#include "plugin.h"
#include "theme.h"

#if !PBE_GUI_ON_HOST_THREAD
#include <pthread.h>
#endif

#define TICK_TIMER_ID 1
#define TICK_SECONDS (1.0 / 30.0)
#define LINUX_LOOP_SECONDS (1.0 / 60.0)

typedef struct {
  PbePlugin *plugin;
  Editor *editor;

  PuglWorld *world;
  PuglView *view;
  Canvas *canvas;

  void *parent; // native handle from the host (HWND / NSView* / X11 Window)
  float scale;  // host-reported content scale
  unsigned view_width, view_height; // current view size in device pixels

  cplug_atomic_i32 want_visible;
  bool is_visible;

#if !PBE_GUI_ON_HOST_THREAD
  pthread_t thread;
  bool thread_started;
  cplug_atomic_i32 stop;
#endif
} Gui;

// ---- Painting and events ----------------------------------------------------

static unsigned mods_from_pugl(PuglMods state) {
  unsigned mods = 0;
  if (state & PUGL_MOD_SHIFT) mods |= EDITOR_MOD_SHIFT;
  if (state & PUGL_MOD_CTRL) mods |= EDITOR_MOD_CTRL;
  return mods;
}

static void paint(Gui *g) {
  if (!g->canvas || g->view_width == 0 || g->view_height == 0) return;
  float scale = g->scale > 0.0f ? g->scale : 1.0f;
  float width = (float)g->view_width / scale;
  float height = (float)g->view_height / scale;

  glViewport(0, 0, (GLsizei)g->view_width, (GLsizei)g->view_height);
  CanvasColor bg = THEME_BG;
  glClearColor(bg.r, bg.g, bg.b, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

  CanvasNanoVG_BeginFrame(g->canvas, width, height, scale);
  Editor_Paint(g->editor, g->canvas, width, height);
  CanvasNanoVG_EndFrame(g->canvas);
}

static void tick(Gui *g) {
  if (Editor_Tick(g->editor)) puglObscureView(g->view);
}

static PuglStatus on_event(PuglView *view, const PuglEvent *event) {
  Gui *g = puglGetHandle(view);
  float scale = g->scale > 0.0f ? g->scale : 1.0f;

  switch (event->type) {
  case PUGL_REALIZE:
    // GL context is current here and in UNREALIZE/EXPOSE only.
    if (!GlLoader_Load()) cplug_log("PBE: failed to load OpenGL functions");
    g->canvas = CanvasNanoVG_Create(pbe_font_data, pbe_font_data_size);
    if (!g->canvas) cplug_log("PBE: failed to create NanoVG context");
    Editor_SyncFromPlugin(g->editor);
    break;
  case PUGL_UNREALIZE:
    CanvasNanoVG_Destroy(g->canvas);
    g->canvas = NULL;
    break;
  case PUGL_CONFIGURE:
    g->view_width = event->configure.width;
    g->view_height = event->configure.height;
    break;
  case PUGL_EXPOSE:
    paint(g);
    return PUGL_SUCCESS;
  case PUGL_BUTTON_PRESS:
    Editor_MouseDown(g->editor, (float)event->button.x / scale, (float)event->button.y / scale,
                     (int)event->button.button, mods_from_pugl(event->button.state),
                     event->button.time);
    break;
  case PUGL_BUTTON_RELEASE:
    Editor_MouseUp(g->editor, (float)event->button.x / scale, (float)event->button.y / scale,
                   (int)event->button.button);
    break;
  case PUGL_MOTION:
    Editor_MouseMove(g->editor, (float)event->motion.x / scale, (float)event->motion.y / scale,
                     mods_from_pugl(event->motion.state));
    break;
  case PUGL_POINTER_OUT:
    Editor_MouseLeave(g->editor);
    break;
  case PUGL_SCROLL:
    Editor_Scroll(g->editor, (float)event->scroll.x / scale, (float)event->scroll.y / scale,
                  (float)event->scroll.dy, mods_from_pugl(event->scroll.state));
    break;
  case PUGL_TIMER:
    if (event->timer.id == TICK_TIMER_ID) tick(g);
    break;
  default:
    break;
  }

  if (Editor_TakeDirty(g->editor)) puglObscureView(view);
  return PUGL_SUCCESS;
}

// ---- View lifetime (always on the thread that will pump it) -----------------

static bool create_view(Gui *g) {
  unsigned width, height;
  Editor_GetSize(&width, &height);
  float scale = g->scale > 0.0f ? g->scale : 1.0f;

  // No PUGL_WORLD_THREADS: it would call XInitThreads() long after the host
  // started using Xlib, which is documented as unsafe. Our X connection is
  // private to the GUI thread, so no locking is needed anyway.
  g->world = puglNewWorld(PUGL_MODULE, 0);
  if (!g->world) return false;
  puglSetWorldString(g->world, PUGL_CLASS_NAME, PBE_PLUGIN_FILE_NAME);

  g->view = puglNewView(g->world);
  if (!g->view) return false;
  puglSetHandle(g->view, g);
  puglSetBackend(g->view, puglGlBackend());
  puglSetEventFunc(g->view, on_event);
  puglSetViewString(g->view, PUGL_WINDOW_TITLE, PBE_PLUGIN_TITLE);
  puglSetViewHint(g->view, PUGL_CONTEXT_VERSION_MAJOR, 2);
  puglSetViewHint(g->view, PUGL_CONTEXT_VERSION_MINOR, 0);
  puglSetViewHint(g->view, PUGL_CONTEXT_PROFILE, PUGL_OPENGL_COMPATIBILITY_PROFILE);
  puglSetViewHint(g->view, PUGL_DOUBLE_BUFFER, PUGL_TRUE);
  puglSetViewHint(g->view, PUGL_STENCIL_BITS, 8); // NanoVG fills need stencil
  puglSetViewHint(g->view, PUGL_SAMPLES, 0);      // NanoVG does its own AA
  puglSetViewHint(g->view, PUGL_SWAP_INTERVAL, 0);
  puglSetViewHint(g->view, PUGL_RESIZABLE, PUGL_FALSE);
  puglSetViewHint(g->view, PUGL_IGNORE_KEY_REPEAT, PUGL_TRUE);
  puglSetSizeHint(g->view, PUGL_DEFAULT_SIZE, (PuglSpan)(width * scale),
                  (PuglSpan)(height * scale));
  puglSetParent(g->view, (PuglNativeView)(uintptr_t)g->parent);

  PuglStatus st = puglRealize(g->view);
  if (st != PUGL_SUCCESS) {
    cplug_log("PBE: puglRealize failed: %s", puglStrerror(st));
    return false;
  }
  return true;
}

static void destroy_view(Gui *g) {
  if (g->view) {
    puglUnrealize(g->view); // dispatches UNREALIZE with the GL context current
    puglFreeView(g->view);
    g->view = NULL;
  }
  if (g->world) {
    puglFreeWorld(g->world);
    g->world = NULL;
  }
  g->is_visible = false;
  g->view_width = g->view_height = 0;
}

static void apply_visibility(Gui *g) {
  if (!g->view) return;
  bool want = cplug_atomic_load_i32(&g->want_visible) != 0;
  if (want == g->is_visible) return;
  if (want) {
    puglShow(g->view, PUGL_SHOW_PASSIVE);
  } else {
    puglHide(g->view);
  }
  g->is_visible = want;
}

#if !PBE_GUI_ON_HOST_THREAD
static void *gui_thread_main(void *arg) {
  Gui *g = arg;
  if (create_view(g)) {
    while (!cplug_atomic_load_i32(&g->stop)) {
      apply_visibility(g);
      puglUpdate(g->world, LINUX_LOOP_SECONDS);
      tick(g);
    }
  }
  destroy_view(g);
  return NULL;
}

static void start_thread(Gui *g) {
  if (g->thread_started) return;
  cplug_atomic_exchange_i32(&g->stop, 0);
  if (pthread_create(&g->thread, NULL, gui_thread_main, g) == 0) g->thread_started = true;
}

static void stop_thread(Gui *g) {
  if (!g->thread_started) return;
  cplug_atomic_exchange_i32(&g->stop, 1);
  pthread_join(g->thread, NULL);
  g->thread_started = false;
}
#endif

static void attach(Gui *g) {
#if PBE_GUI_ON_HOST_THREAD
  if (create_view(g)) {
    apply_visibility(g);
    puglStartTimer(g->view, TICK_TIMER_ID, TICK_SECONDS);
  } else {
    destroy_view(g);
  }
#else
  start_thread(g);
#endif
}

static void detach(Gui *g) {
#if PBE_GUI_ON_HOST_THREAD
  if (g->view) puglStopTimer(g->view, TICK_TIMER_ID);
  destroy_view(g);
#else
  stop_thread(g);
#endif
}

// ---- CPLUG GUI entry points -------------------------------------------------

void *cplug_createGUI(void *userPlugin) {
  Gui *g = calloc(1, sizeof(*g));
  if (!g) return NULL;
  g->plugin = userPlugin;
  g->scale = 1.0f;
  g->editor = Editor_Create(g->plugin);
  if (!g->editor) {
    free(g);
    return NULL;
  }
  return g;
}

void cplug_destroyGUI(void *userGUI) {
  Gui *g = userGUI;
  if (!g) return;
  detach(g);
  Editor_Destroy(g->editor);
  free(g);
}

void cplug_setParent(void *userGUI, void *newParent) {
  Gui *g = userGUI;
  detach(g);
  g->parent = newParent;
  if (newParent) attach(g);
}

void cplug_setVisible(void *userGUI, bool visible) {
  Gui *g = userGUI;
  cplug_atomic_exchange_i32(&g->want_visible, visible ? 1 : 0);
#if PBE_GUI_ON_HOST_THREAD
  apply_visibility(g);
#endif
}

void cplug_setScaleFactor(void *userGUI, float scale) {
  Gui *g = userGUI;
  // Only honoured at view creation for now (the editor is fixed-size).
  if (scale > 0.0f) g->scale = scale;
}

void cplug_getSize(void *userGUI, uint32_t *width, uint32_t *height) {
  Gui *g = userGUI;
  unsigned w, h;
  Editor_GetSize(&w, &h);
  *width = (uint32_t)(w * g->scale);
  *height = (uint32_t)(h * g->scale);
}

void cplug_checkSize(void *userGUI, uint32_t *width, uint32_t *height) {
  cplug_getSize(userGUI, width, height);
}

bool cplug_setSize(void *userGUI, uint32_t width, uint32_t height) {
  // Fixed size: accept the host's call as long as it matches what we asked for.
  uint32_t w, h;
  cplug_getSize(userGUI, &w, &h);
  return width == w && height == h;
}
