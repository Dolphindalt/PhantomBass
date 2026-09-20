// Offline renderer: pushes a raw float32 interleaved-stereo file through the
// built .clap with the given parameters and writes the result in the same
// format. Used for measuring what the plugin does to real signals
// (see tools/analyze.py).
//
//   pbe-render <PhantomBass.clap> <in.f32> <out.f32> [gain_db] [speaker_hz] [harmonics_hz]
#include <clap/clap.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SAMPLE_RATE 48000.0
#define BLOCK 256

static const void *host_get_extension(const clap_host_t *h, const char *id) { (void)h; (void)id; return NULL; }
static void host_noop(const clap_host_t *h) { (void)h; }
static const clap_host_t HOST = {.clap_version = CLAP_VERSION_INIT, .name = "pbe-render", .vendor = "", .url = "",
                                 .version = "0", .get_extension = host_get_extension,
                                 .request_restart = host_noop, .request_process = host_noop,
                                 .request_callback = host_noop};

typedef struct {
  clap_input_events_t base;
  clap_event_param_value_t events[3];
  uint32_t count;
} InEvents;
static uint32_t in_size(const clap_input_events_t *l) { return ((const InEvents *)l)->count; }
static const clap_event_header_t *in_get(const clap_input_events_t *l, uint32_t i) {
  return &((const InEvents *)l)->events[i].header;
}
static bool out_try_push(const clap_output_events_t *l, const clap_event_header_t *e) { (void)l; (void)e; return true; }

int main(int argc, char **argv) {
  if (argc < 4) {
    fprintf(stderr, "usage: %s <PhantomBass.clap> <in.f32> <out.f32> [gain_db] [speaker_hz] [harmonics_hz]\n", argv[0]);
    return 2;
  }
  void *lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!lib) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
  const clap_plugin_entry_t *entry = dlsym(lib, "clap_entry");
  if (!entry || !entry->init(argv[1])) return 1;
  const clap_plugin_factory_t *factory = entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
  const clap_plugin_descriptor_t *desc = factory->get_plugin_descriptor(factory, 0);
  const clap_plugin_t *plugin = factory->create_plugin(factory, &HOST, desc->id);
  if (!plugin || !plugin->init(plugin)) return 1;
  const clap_plugin_params_t *params = plugin->get_extension(plugin, CLAP_EXT_PARAMS);

  FILE *in = fopen(argv[2], "rb"), *out = fopen(argv[3], "wb");
  if (!in || !out) { perror("fopen"); return 1; }

  // Parameters: the first block carries one event per value given.
  InEvents in_events = {.base = {.ctx = NULL, .size = in_size, .get = in_get}};
  for (int i = 0; i < 3 && 4 + i < argc; i++) {
    clap_param_info_t info;
    params->get_info(plugin, (uint32_t)i, &info);
    clap_event_param_value_t *ev = &in_events.events[in_events.count++];
    memset(ev, 0, sizeof(*ev));
    ev->header.size = sizeof(*ev);
    ev->header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    ev->header.type = CLAP_EVENT_PARAM_VALUE;
    ev->param_id = info.id;
    ev->note_id = ev->port_index = ev->channel = ev->key = -1;
    ev->value = atof(argv[4 + i]);
  }
  clap_output_events_t out_events = {.ctx = NULL, .try_push = out_try_push};

  float inbuf[2][BLOCK], outbuf[2][BLOCK], frames[2 * BLOCK];
  float *ins[2] = {inbuf[0], inbuf[1]}, *outs[2] = {outbuf[0], outbuf[1]};
  clap_audio_buffer_t in_bus = {.data32 = ins, .channel_count = 2};
  clap_audio_buffer_t out_bus = {.data32 = outs, .channel_count = 2};
  clap_process_t process = {.steady_time = -1, .frames_count = BLOCK, .audio_inputs = &in_bus,
                            .audio_outputs = &out_bus, .audio_inputs_count = 1, .audio_outputs_count = 1,
                            .in_events = &in_events.base, .out_events = &out_events};

  plugin->activate(plugin, SAMPLE_RATE, 1, BLOCK);
  plugin->start_processing(plugin);
  size_t n;
  while ((n = fread(frames, sizeof(float) * 2, BLOCK, in)) > 0) {
    for (size_t i = 0; i < n; i++) { inbuf[0][i] = frames[2 * i]; inbuf[1][i] = frames[2 * i + 1]; }
    process.frames_count = (uint32_t)n;
    plugin->process(plugin, &process);
    in_events.count = 0;
    for (size_t i = 0; i < n; i++) { frames[2 * i] = outbuf[0][i]; frames[2 * i + 1] = outbuf[1][i]; }
    fwrite(frames, sizeof(float) * 2, n, out);
  }
  plugin->stop_processing(plugin);
  plugin->deactivate(plugin);
  plugin->destroy(plugin);
  entry->deinit();
  fclose(in);
  fclose(out);
  return 0;
}
