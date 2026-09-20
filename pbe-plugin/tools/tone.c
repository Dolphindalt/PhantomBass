// Audition tool (Linux): plays a test tone through the plugin and out of the
// sound card with the editor open, so what the knobs do can be heard live.
// Acts as a tiny host: cplug_process() is driven from this program's own
// audio loop, exactly as in gui-preview, but the output goes to ALSA.
//
//   pbe-tone [--freq HZ] [--level DBFS] [--device ALSA_DEVICE] [--seconds N]
//
// Keys (in the terminal):  + / -  tone up/down a semitone   < / >  level -/+ 3 dB
//                          s  sine tone    m  bass line    b  bypass on/off    q  quit
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <alsa/asoundlib.h>
#include <cplug.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "config.h"

#define SAMPLE_RATE 48000
#define BLOCK 256

// ---- Fake process context: one stereo block per call ----------------------------

typedef struct {
  CplugProcessContext base;
  float *ins[2], *outs[2];
  bool audio_done;
} Process;

static bool proc_enqueue(CplugProcessContext *ctx, const CplugEvent *ev, uint32_t frame) {
  (void)ctx; (void)ev; (void)frame;
  return true;
}
static bool proc_dequeue(CplugProcessContext *ctx, CplugEvent *ev, uint32_t frame) {
  Process *p = (Process *)ctx;
  (void)frame;
  if (p->audio_done) return false;
  p->audio_done = true;
  ev->processAudio.type = CPLUG_EVENT_PROCESS_AUDIO;
  ev->processAudio.endFrame = BLOCK;
  return true;
}
static float **proc_input(const CplugProcessContext *ctx, uint32_t bus) { (void)bus; return ((Process *)ctx)->ins; }
static float **proc_output(const CplugProcessContext *ctx, uint32_t bus) { (void)bus; return ((Process *)ctx)->outs; }

// ---- Tone generator ---------------------------------------------------------------

typedef enum { TONE_SINE, TONE_BASSLINE } ToneMode;

typedef struct {
  ToneMode mode;
  double freq_hz;   // sine mode
  double level_db;  // dBFS
  double phase;
  double t;         // seconds, bass line clock
} Tone;

// E1 A1 D2 G1 C2 F1 A#1 D#2: a bass line that lives below a 100 Hz cutoff.
static const double BASSLINE_HZ[] = {41.2, 55.0, 73.4, 49.0, 65.4, 43.7, 58.3, 77.8};
#define BASSLINE_NOTE_SECONDS 0.5

static void tone_fill(Tone *tone, float *left, float *right, int n) {
  double amp = pow(10.0, tone->level_db / 20.0);
  for (int i = 0; i < n; i++) {
    double f = tone->freq_hz, env = 1.0;
    if (tone->mode == TONE_BASSLINE) {
      int note = (int)(tone->t / BASSLINE_NOTE_SECONDS);
      double into = tone->t - note * BASSLINE_NOTE_SECONDS;
      f = BASSLINE_HZ[note % (int)(sizeof(BASSLINE_HZ) / sizeof(BASSLINE_HZ[0]))];
      env = exp(-into * 3.0) * (into < 0.005 ? into / 0.005 : 1.0); // pluck
      if (tone->t >= 8 * BASSLINE_NOTE_SECONDS) tone->t -= 8 * BASSLINE_NOTE_SECONDS;
    }
    float s = (float)(amp * env * sin(tone->phase));
    tone->phase += 2.0 * M_PI * f / SAMPLE_RATE;
    if (tone->phase > 2.0 * M_PI) tone->phase -= 2.0 * M_PI;
    tone->t += 1.0 / SAMPLE_RATE;
    left[i] = right[i] = s;
  }
}

static const char *note_name(double freq_hz, char *buf, size_t len) {
  static const char *NAMES[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  double midi = 69.0 + 12.0 * log2(freq_hz / 440.0);
  int n = (int)lrint(midi);
  double cents = (midi - n) * 100.0;
  snprintf(buf, len, "%s%d%+.0fc", NAMES[((n % 12) + 12) % 12], n / 12 - 1, cents);
  return buf;
}

// ---- Terminal ---------------------------------------------------------------------

static struct termios saved_termios;
static bool termios_saved = false;

static void terminal_restore(void) {
  if (termios_saved) tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios);
}

static void terminal_raw(void) {
  if (!isatty(STDIN_FILENO) || tcgetattr(STDIN_FILENO, &saved_termios) != 0) return;
  termios_saved = true;
  atexit(terminal_restore);
  struct termios raw = saved_termios;
  raw.c_lflag &= ~(unsigned)(ICANON | ECHO);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}

static int terminal_key(void) {
  struct pollfd pfd = {.fd = STDIN_FILENO, .events = POLLIN};
  if (!termios_saved || poll(&pfd, 1, 0) <= 0) return -1;
  unsigned char c;
  return read(STDIN_FILENO, &c, 1) == 1 ? c : -1;
}

static void print_status(const Tone *tone, bool bypass) {
  char name[16];
  printf(isatty(STDOUT_FILENO) ? "\r\033[K" : "\n"); // rewrite the line on a terminal
  if (tone->mode == TONE_SINE) {
    printf("  sine %.1f Hz (%s)   level %.0f dBFS   %s", tone->freq_hz,
           note_name(tone->freq_hz, name, sizeof(name)), tone->level_db,
           bypass ? "BYPASSED" : "through plugin");
  } else {
    printf("  bass line E1 A1 D2 G1 ...   level %.0f dBFS   %s", tone->level_db,
           bypass ? "BYPASSED" : "through plugin");
  }
  fflush(stdout);
}

// ---- ALSA -------------------------------------------------------------------------

static snd_pcm_t *alsa_open(const char *device, bool *use_float) {
  snd_pcm_t *pcm = NULL;
  int err = snd_pcm_open(&pcm, device, SND_PCM_STREAM_PLAYBACK, 0);
  if (err < 0) {
    fprintf(stderr, "cannot open ALSA device '%s': %s\n", device, snd_strerror(err));
    return NULL;
  }
  *use_float = true;
  err = snd_pcm_set_params(pcm, SND_PCM_FORMAT_FLOAT_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 2,
                           SAMPLE_RATE, 1, 60000);
  if (err < 0) {
    *use_float = false;
    err = snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 2,
                             SAMPLE_RATE, 1, 60000);
  }
  if (err < 0) {
    fprintf(stderr, "cannot configure ALSA device '%s': %s\n", device, snd_strerror(err));
    snd_pcm_close(pcm);
    return NULL;
  }
  return pcm;
}

static void alsa_write(snd_pcm_t *pcm, bool use_float, const float *left, const float *right, int n) {
  float f32[2 * BLOCK];
  int16_t s16[2 * BLOCK];
  for (int i = 0; i < n; i++) {
    float l = fmaxf(-1.0f, fminf(1.0f, left[i])), r = fmaxf(-1.0f, fminf(1.0f, right[i]));
    f32[2 * i] = l;
    f32[2 * i + 1] = r;
    s16[2 * i] = (int16_t)lrintf(l * 32767.0f);
    s16[2 * i + 1] = (int16_t)lrintf(r * 32767.0f);
  }
  const void *buf = use_float ? (const void *)f32 : (const void *)s16;
  snd_pcm_sframes_t written = snd_pcm_writei(pcm, buf, (snd_pcm_uframes_t)n);
  if (written < 0) snd_pcm_recover(pcm, (int)written, 1); // underrun etc.
}

// ---- Main -------------------------------------------------------------------------

int main(int argc, char **argv) {
  Tone tone = {.mode = TONE_SINE, .freq_hz = 50.0, .level_db = -18.0};
  const char *device = "default";
  double seconds = 0.0;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--freq") && i + 1 < argc) tone.freq_hz = atof(argv[++i]);
    else if (!strcmp(argv[i], "--level") && i + 1 < argc) tone.level_db = atof(argv[++i]);
    else if (!strcmp(argv[i], "--device") && i + 1 < argc) device = argv[++i];
    else if (!strcmp(argv[i], "--seconds") && i + 1 < argc) seconds = atof(argv[++i]);
    else if (!strcmp(argv[i], "--bassline")) tone.mode = TONE_BASSLINE;
    else {
      fprintf(stderr, "usage: %s [--freq HZ] [--level DBFS] [--device ALSA_DEVICE] [--seconds N] [--bassline]\n", argv[0]);
      return 2;
    }
  }

  bool use_float;
  snd_pcm_t *pcm = alsa_open(device, &use_float);
  if (!pcm) return 1;

  Display *display = XOpenDisplay(NULL);
  if (!display) {
    fprintf(stderr, "cannot open X display\n");
    return 1;
  }

  CplugHostContext host = {.type = CPLUG_PLUGIN_IS_STANDALONE};
  void *plugin = cplug_createPlugin(&host);
  cplug_setSampleRateAndBlockSize(plugin, SAMPLE_RATE, BLOCK);
  void *gui = cplug_createGUI(plugin);
  uint32_t width, height;
  cplug_getSize(gui, &width, &height);

  int screen = DefaultScreen(display);
  Window window = XCreateSimpleWindow(display, RootWindow(display, screen), 0, 0, width, height, 0, 0,
                                      BlackPixel(display, screen));
  XStoreName(display, window, PBE_PLUGIN_TITLE " (tone)");
  XSizeHints hints = {.flags = PMinSize | PMaxSize, .min_width = (int)width, .min_height = (int)height,
                      .max_width = (int)width, .max_height = (int)height};
  XSetWMNormalHints(display, window, &hints);
  Atom wm_delete = XInternAtom(display, "WM_DELETE_WINDOW", False);
  XSetWMProtocols(display, window, &wm_delete, 1);
  XMapWindow(display, window);
  XFlush(display);
  cplug_setParent(gui, (void *)(uintptr_t)window);
  cplug_setVisible(gui, true);

  float in[2][BLOCK], out[2][BLOCK];
  Process proc;
  memset(&proc, 0, sizeof(proc));
  proc.ins[0] = in[0]; proc.ins[1] = in[1];
  proc.outs[0] = out[0]; proc.outs[1] = out[1];
  proc.base.numFrames = BLOCK;
  proc.base.numInputs = proc.base.numOutputs = 1;
  proc.base.enqueueEvent = proc_enqueue;
  proc.base.dequeueEvent = proc_dequeue;
  proc.base.getAudioInput = proc_input;
  proc.base.getAudioOutput = proc_output;

  terminal_raw();
  printf("%s audition: ALSA '%s' (%s), %d Hz\n", PBE_PLUGIN_TITLE, device,
         use_float ? "float" : "16-bit", SAMPLE_RATE);
  printf("  + / -  semitone up/down    < / >  level -/+ 3 dB    s sine    m bass line    b bypass    q quit\n");
  bool bypass = false;
  print_status(&tone, bypass);

  struct timespec start;
  clock_gettime(CLOCK_MONOTONIC, &start);
  for (bool running = true; running;) {
    tone_fill(&tone, in[0], in[1], BLOCK);
    if (bypass) {
      memcpy(out[0], in[0], sizeof(in[0]));
      memcpy(out[1], in[1], sizeof(in[1]));
    } else {
      proc.audio_done = false;
      cplug_process(plugin, &proc.base);
    }
    alsa_write(pcm, use_float, out[0], out[1], BLOCK);

    int key = terminal_key();
    bool changed = true;
    switch (key) {
    case '+': case '=': tone.freq_hz *= pow(2.0, 1.0 / 12.0); tone.mode = TONE_SINE; break;
    case '-': case '_': tone.freq_hz /= pow(2.0, 1.0 / 12.0); tone.mode = TONE_SINE; break;
    case '>': case '.': tone.level_db = fmin(0.0, tone.level_db + 3.0); break;
    case '<': case ',': tone.level_db = fmax(-60.0, tone.level_db - 3.0); break;
    case 's': tone.mode = TONE_SINE; break;
    case 'm': tone.mode = TONE_BASSLINE; tone.t = 0.0; break;
    case 'b': bypass = !bypass; break;
    case 'q': case 3: case 27: running = false; break;
    default: changed = false; break;
    }
    if (changed) print_status(&tone, bypass);

    while (XPending(display)) {
      XEvent ev;
      XNextEvent(display, &ev);
      if (ev.type == ClientMessage && (Atom)ev.xclient.data.l[0] == wm_delete) running = false;
    }
    if (seconds > 0.0) {
      struct timespec now;
      clock_gettime(CLOCK_MONOTONIC, &now);
      if ((double)(now.tv_sec - start.tv_sec) + (double)(now.tv_nsec - start.tv_nsec) * 1e-9 >= seconds) running = false;
    }
  }
  printf("\n");

  snd_pcm_drain(pcm);
  snd_pcm_close(pcm);
  cplug_setVisible(gui, false);
  cplug_setParent(gui, NULL);
  cplug_destroyGUI(gui);
  cplug_destroyPlugin(plugin);
  XDestroyWindow(display, window);
  XCloseDisplay(display);
  return 0;
}
