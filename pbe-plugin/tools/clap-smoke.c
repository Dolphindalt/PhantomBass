// Host-less smoke test: loads the built .clap the way a host would, checks
// the parameter metadata, pushes audio through it, and round-trips state.
// Exercises the whole stack below the GUI (CPLUG wrapper, plugin glue,
// engine, pbe-dsp) without needing a DAW.
//
//   pbe-clap-smoke path/to/PhantomBass.clap
#include <clap/clap.h>
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SAMPLE_RATE 48000.0
#define BLOCK 512
#define SECONDS 1.5
#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
      return 1;                                                                \
    }                                                                          \
  } while (0)

// ---- Minimal host -------------------------------------------------------------

static const void *host_get_extension(const clap_host_t *h, const char *id) {
  (void)h; (void)id;
  return NULL;
}
static void host_noop(const clap_host_t *h) { (void)h; }

static const clap_host_t HOST = {
    .clap_version = CLAP_VERSION_INIT,
    .host_data = NULL,
    .name = "pbe-clap-smoke",
    .vendor = "",
    .url = "",
    .version = "0",
    .get_extension = host_get_extension,
    .request_restart = host_noop,
    .request_process = host_noop,
    .request_callback = host_noop,
};

typedef struct {
  clap_input_events_t base;
  const clap_event_header_t *events[8];
  uint32_t count;
} InEvents;

static uint32_t in_size(const clap_input_events_t *l) { return ((const InEvents *)l)->count; }
static const clap_event_header_t *in_get(const clap_input_events_t *l, uint32_t i) {
  return ((const InEvents *)l)->events[i];
}

typedef struct {
  clap_output_events_t base;
  uint32_t count;
} OutEvents;

static bool out_try_push(const clap_output_events_t *l, const clap_event_header_t *e) {
  (void)e;
  ((OutEvents *)l)->count++;
  return true;
}

typedef struct {
  clap_ostream_t base;
  uint8_t data[1024];
  size_t size;
} MemOStream;

static int64_t mem_write(const clap_ostream_t *s, const void *buf, uint64_t n) {
  MemOStream *m = (MemOStream *)s;
  if (m->size + n > sizeof(m->data)) return -1;
  memcpy(m->data + m->size, buf, n);
  m->size += n;
  return (int64_t)n;
}

typedef struct {
  clap_istream_t base;
  const uint8_t *data;
  size_t size, pos;
} MemIStream;

static int64_t mem_read(const clap_istream_t *s, void *buf, uint64_t n) {
  MemIStream *m = (MemIStream *)s;
  size_t left = m->size - m->pos;
  if (n > left) n = left;
  memcpy(buf, m->data + m->pos, n);
  m->pos += n;
  return (int64_t)n;
}

// ---- Test helpers ---------------------------------------------------------------

typedef struct {
  const clap_plugin_t *plugin;
  float in[2][BLOCK], out[2][BLOCK];
  float *ins[2], *outs[2];
  clap_audio_buffer_t in_buf, out_buf;
  InEvents in_events;
  OutEvents out_events;
  clap_process_t process;
  double phase;
} Runner;

static void runner_init(Runner *r, const clap_plugin_t *plugin) {
  memset(r, 0, sizeof(*r));
  r->plugin = plugin;
  r->ins[0] = r->in[0];
  r->ins[1] = r->in[1];
  r->outs[0] = r->out[0];
  r->outs[1] = r->out[1];
  r->in_buf.data32 = r->ins;
  r->in_buf.channel_count = 2;
  r->out_buf.data32 = r->outs;
  r->out_buf.channel_count = 2;
  r->in_events.base.ctx = &r->in_events;
  r->in_events.base.size = in_size;
  r->in_events.base.get = in_get;
  r->out_events.base.ctx = &r->out_events;
  r->out_events.base.try_push = out_try_push;
  r->process.steady_time = -1;
  r->process.frames_count = BLOCK;
  r->process.audio_inputs = &r->in_buf;
  r->process.audio_outputs = &r->out_buf;
  r->process.audio_inputs_count = 1;
  r->process.audio_outputs_count = 1;
  r->process.in_events = &r->in_events.base;
  r->process.out_events = &r->out_events.base;
}

// Runs a sine for SECONDS and returns the output RMS over the last half,
// or -1 if the output ever went non-finite.
static double run_sine(Runner *r, double freq_hz, double amplitude) {
  int blocks = (int)(SECONDS * SAMPLE_RATE / BLOCK);
  double sum = 0.0;
  size_t n = 0;
  for (int b = 0; b < blocks; b++) {
    for (int i = 0; i < BLOCK; i++) {
      float s = (float)(amplitude * sin(r->phase));
      r->phase += 2.0 * M_PI * freq_hz / SAMPLE_RATE;
      r->in[0][i] = r->in[1][i] = s;
    }
    if (r->plugin->process(r->plugin, &r->process) == CLAP_PROCESS_ERROR) return -1.0;
    r->in_events.count = 0; // events apply to the first block only
    for (int i = 0; i < BLOCK; i++) {
      for (int ch = 0; ch < 2; ch++) {
        float v = r->out[ch][i];
        if (!isfinite(v)) return -1.0;
        if (b >= blocks / 2) {
          sum += (double)v * v;
          n++;
        }
      }
    }
  }
  return sqrt(sum / (double)n);
}

static void queue_param(Runner *r, clap_event_param_value_t *ev, clap_id id, double value) {
  memset(ev, 0, sizeof(*ev));
  ev->header.size = sizeof(*ev);
  ev->header.space_id = CLAP_CORE_EVENT_SPACE_ID;
  ev->header.type = CLAP_EVENT_PARAM_VALUE;
  ev->param_id = id;
  ev->note_id = -1;
  ev->port_index = -1;
  ev->channel = -1;
  ev->key = -1;
  ev->value = value;
  r->in_events.events[r->in_events.count++] = &ev->header;
}

static double to_db(double rms, double ref) { return 20.0 * log10(rms / ref); }

// ---- Main ---------------------------------------------------------------------

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <plugin.clap>\n", argv[0]);
    return 2;
  }
  void *lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!lib) {
    fprintf(stderr, "dlopen: %s\n", dlerror());
    return 1;
  }
  const clap_plugin_entry_t *entry = dlsym(lib, "clap_entry");
  CHECK(entry != NULL);
  CHECK(entry->init(argv[1]));

  const clap_plugin_factory_t *factory = entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
  CHECK(factory != NULL);
  CHECK(factory->get_plugin_count(factory) == 1);
  const clap_plugin_descriptor_t *desc = factory->get_plugin_descriptor(factory, 0);
  CHECK(desc != NULL);
  printf("plugin: %s (%s) v%s\n", desc->name, desc->id, desc->version);

  const clap_plugin_t *plugin = factory->create_plugin(factory, &HOST, desc->id);
  CHECK(plugin != NULL);
  CHECK(plugin->init(plugin));

  // Parameters.
  const clap_plugin_params_t *params = plugin->get_extension(plugin, CLAP_EXT_PARAMS);
  CHECK(params != NULL);
  uint32_t n_params = params->count(plugin);
  CHECK(n_params == 3);
  clap_id gain_id = 0, cutoff_id = 0;
  double gain_min = 0.0; // the gain knob's minimum is "Off"
  for (uint32_t i = 0; i < n_params; i++) {
    clap_param_info_t info;
    CHECK(params->get_info(plugin, i, &info));
    double value = 0.0;
    char text[64] = "";
    CHECK(params->get_value(plugin, info.id, &value));
    CHECK(params->value_to_text(plugin, info.id, value, text, sizeof(text)));
    printf("param %u: %-24s [%g .. %g] default %g -> %s\n", info.id, info.name, info.min_value,
           info.max_value, info.default_value, text);
    CHECK(value == info.default_value);
    CHECK(info.flags & CLAP_PARAM_IS_AUTOMATABLE);
    if (strstr(info.name, "Gain")) {
      gain_id = info.id;
      gain_min = info.min_value;
    }
    if (strstr(info.name, "Speaker")) cutoff_id = info.id;
  }
  CHECK(gain_id != 0 && cutoff_id != 0);

  // Audio.
  CHECK(plugin->activate(plugin, SAMPLE_RATE, 1, BLOCK));
  CHECK(plugin->start_processing(plugin));
  Runner *r = calloc(1, sizeof(*r));
  runner_init(r, plugin);

  // A tone below the speaker cutoff: with enhancement on, harmonics above
  // the cutoff must appear in the output.
  const double bass_hz = 50.0, amp = 0.25;
  double on_rms = run_sine(r, bass_hz, amp);
  CHECK(on_rms > 0.0);

  clap_event_param_value_t ev;
  queue_param(r, &ev, gain_id, gain_min); // Off
  double off_rms = run_sine(r, bass_hz, amp);
  CHECK(off_rms > 0.0);
  printf("50 Hz tone: enhancement on %.1f dB, off %.1f dB (re input)\n", to_db(on_rms, amp),
         to_db(off_rms, amp));
  CHECK(off_rms < amp * 0.5);       // the crossover removed most of the bass
  CHECK(on_rms > off_rms * 1.5);    // ...and the enhancer put harmonics back

  // A tone well above the cutoff passes through essentially unchanged.
  queue_param(r, &ev, gain_id, 0.0);
  double mid_rms = run_sine(r, 1000.0, amp);
  printf("1 kHz tone: %.2f dB (re input)\n", to_db(mid_rms, amp));
  CHECK(fabs(to_db(mid_rms, amp / sqrt(2.0))) < 1.0);

  // Retuning the speaker cutoff while streaming (down to its 30 Hz minimum,
  // then up to its 400 Hz maximum) must be glitch-free: the filters keep
  // their state, so a tone far above either cutoff passes at the same level
  // and nothing goes non-finite. (5 kHz: the 12 dB/oct crossover is within
  // 0.03 dB of unity even at the 400 Hz setting.)
  clap_event_param_value_t ev_cutoff;
  const double ref = amp / sqrt(2.0);
  queue_param(r, &ev_cutoff, cutoff_id, 30.0);
  double low_cutoff_rms = run_sine(r, 5000.0, amp);
  queue_param(r, &ev_cutoff, cutoff_id, 400.0);
  double high_cutoff_rms = run_sine(r, 5000.0, amp);
  printf("5 kHz tone, cutoff 30 Hz: %.2f dB, 400 Hz: %.2f dB\n", to_db(low_cutoff_rms, ref),
         to_db(high_cutoff_rms, ref));
  CHECK(low_cutoff_rms > 0.0 && high_cutoff_rms > 0.0);
  CHECK(fabs(to_db(low_cutoff_rms, ref)) < 0.2);
  CHECK(fabs(to_db(high_cutoff_rms, ref)) < 0.2);

  // Parameter changes from the host must not be echoed back as output events.
  CHECK(r->out_events.count == 0);

  // State round-trip into a fresh instance.
  const clap_plugin_state_t *state = plugin->get_extension(plugin, CLAP_EXT_STATE);
  CHECK(state != NULL);
  queue_param(r, &ev, gain_id, 6.0);
  run_sine(r, 1000.0, amp);
  MemOStream out = {.base = {.ctx = NULL, .write = mem_write}};
  CHECK(state->save(plugin, &out.base));
  CHECK(out.size > 0);

  const clap_plugin_t *plugin2 = factory->create_plugin(factory, &HOST, desc->id);
  CHECK(plugin2 != NULL && plugin2->init(plugin2));
  MemIStream in = {.base = {.ctx = NULL, .read = mem_read}, .data = out.data, .size = out.size};
  const clap_plugin_state_t *state2 = plugin2->get_extension(plugin2, CLAP_EXT_STATE);
  CHECK(state2->load(plugin2, &in.base));
  const clap_plugin_params_t *params2 = plugin2->get_extension(plugin2, CLAP_EXT_PARAMS);
  double gain2 = 0.0;
  CHECK(params2->get_value(plugin2, gain_id, &gain2));
  printf("state round-trip: gain %.1f dB\n", gain2);
  CHECK(fabs(gain2 - 6.0) < 1e-6);
  plugin2->destroy(plugin2);

  plugin->stop_processing(plugin);
  plugin->deactivate(plugin);
  plugin->destroy(plugin);
  entry->deinit();
  free(r);
  dlclose(lib);
  printf("OK\n");
  return 0;
}
