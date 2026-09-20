// Opens the plugin editor inside a plain top-level X11 window, standing in
// for a host: the plugin is created, its GUI attached to our window, silent
// audio is pumped through cplug_process() so GUI parameter changes travel
// the same path they would in a DAW, and the window is kept open until
// closed or until `seconds` (argv[1]) elapse. On exit the parameter values
// and the number of events the GUI sent to the "host" are printed.
//
//   pbe-gui-preview [seconds]
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <cplug.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "config.h"

// ---- Minimal process context: one silent stereo block per call ----------------

#define BLOCK 64

typedef struct {
  CplugProcessContext base;
  float in[2][BLOCK], out[2][BLOCK];
  float *ins[2], *outs[2];
  bool audio_done;
  unsigned host_events;
} FakeProcess;

static bool fake_enqueue(CplugProcessContext *ctx, const CplugEvent *ev, uint32_t frame) {
  (void)ev; (void)frame;
  ((FakeProcess *)ctx)->host_events++;
  return true;
}

static bool fake_dequeue(CplugProcessContext *ctx, CplugEvent *ev, uint32_t frame) {
  FakeProcess *fp = (FakeProcess *)ctx;
  (void)frame;
  if (fp->audio_done) return false;
  fp->audio_done = true;
  ev->processAudio.type = CPLUG_EVENT_PROCESS_AUDIO;
  ev->processAudio.endFrame = BLOCK;
  return true;
}

static float **fake_input(const CplugProcessContext *ctx, uint32_t bus) {
  (void)bus;
  return ((FakeProcess *)ctx)->ins;
}
static float **fake_output(const CplugProcessContext *ctx, uint32_t bus) {
  (void)bus;
  return ((FakeProcess *)ctx)->outs;
}

static void fake_process_init(FakeProcess *fp) {
  memset(fp, 0, sizeof(*fp));
  fp->ins[0] = fp->in[0];
  fp->ins[1] = fp->in[1];
  fp->outs[0] = fp->out[0];
  fp->outs[1] = fp->out[1];
  fp->base.numFrames = BLOCK;
  fp->base.numInputs = 1;
  fp->base.numOutputs = 1;
  fp->base.enqueueEvent = fake_enqueue;
  fp->base.dequeueEvent = fake_dequeue;
  fp->base.getAudioInput = fake_input;
  fp->base.getAudioOutput = fake_output;
}

int main(int argc, char **argv) {
  double seconds = argc > 1 ? atof(argv[1]) : 0.0;

  Display *display = XOpenDisplay(NULL);
  if (!display) {
    fprintf(stderr, "cannot open X display\n");
    return 1;
  }

  CplugHostContext host = {.type = CPLUG_PLUGIN_IS_STANDALONE};
  void *plugin = cplug_createPlugin(&host);
  cplug_setSampleRateAndBlockSize(plugin, 48000.0, 512);
  void *gui = cplug_createGUI(plugin);
  uint32_t width, height;
  cplug_getSize(gui, &width, &height);

  int screen = DefaultScreen(display);
  Window window = XCreateSimpleWindow(display, RootWindow(display, screen), 0, 0, width, height,
                                      0, 0, BlackPixel(display, screen));
  XStoreName(display, window, PBE_PLUGIN_TITLE " (preview)");
  XSizeHints hints = {.flags = PMinSize | PMaxSize,
                      .min_width = (int)width,
                      .min_height = (int)height,
                      .max_width = (int)width,
                      .max_height = (int)height};
  XSetWMNormalHints(display, window, &hints);
  Atom wm_delete = XInternAtom(display, "WM_DELETE_WINDOW", False);
  XSetWMProtocols(display, window, &wm_delete, 1);
  XSelectInput(display, window, StructureNotifyMask);
  XMapWindow(display, window);
  XFlush(display);

  cplug_setParent(gui, (void *)(uintptr_t)window);
  cplug_setVisible(gui, true);
  printf("editor %ux%u attached to X window 0x%lx\n", width, height, window);

  FakeProcess fake;
  fake_process_init(&fake);

  struct timespec start;
  clock_gettime(CLOCK_MONOTONIC, &start);
  for (int running = 1; running;) {
    while (XPending(display)) {
      XEvent ev;
      XNextEvent(display, &ev);
      if (ev.type == ClientMessage && (Atom)ev.xclient.data.l[0] == wm_delete) running = 0;
    }
    if (seconds > 0.0) {
      struct timespec now;
      clock_gettime(CLOCK_MONOTONIC, &now);
      double elapsed = (double)(now.tv_sec - start.tv_sec) + (double)(now.tv_nsec - start.tv_nsec) * 1e-9;
      if (elapsed >= seconds) running = 0;
    }
    fake.audio_done = false;
    cplug_process(plugin, &fake.base);
    usleep(20000);
  }

  cplug_setVisible(gui, false);
  cplug_setParent(gui, NULL);
  cplug_destroyGUI(gui);

  for (uint32_t i = 0; i < cplug_getNumParameters(plugin); i++) {
    uint32_t id = cplug_getParameterID(plugin, i);
    char name[64], text[64];
    cplug_getParameterName(plugin, id, name, sizeof(name));
    cplug_parameterValueToString(plugin, id, text, sizeof(text), cplug_getParameterValue(plugin, id));
    printf("param %u %-24s %s\n", id, name, text);
  }
  printf("events forwarded to host: %u\n", fake.host_events);
  cplug_destroyPlugin(plugin);
  XDestroyWindow(display, window);
  XCloseDisplay(display);
  return 0;
}
