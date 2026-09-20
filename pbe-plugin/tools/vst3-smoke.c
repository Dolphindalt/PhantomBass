// Host-less VST3 smoke test: drives the module through the VST3 C API the
// way a DAW does (factory → component → bus arrangement → setupProcessing →
// activate → process with parameter changes) and checks the audio actually
// changes with the parameters. The CLAP test covers the same plugin code
// through the other wrapper; this one exists because DAWs on Linux mostly
// load the VST3.
//
//   pbe-vst3-smoke path/to/PhantomBass.vst3/Contents/<arch>-linux/PhantomBass.so
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vst3_c_api.h>

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

// ---- Minimal IParameterChanges / IParamValueQueue ------------------------------

static Steinberg_tresult SMTG_STDMETHODCALLTYPE no_query(void *self, const Steinberg_TUID iid, void **obj) {
  (void)self; (void)iid;
  *obj = NULL;
  return Steinberg_kNoInterface;
}
static Steinberg_uint32 SMTG_STDMETHODCALLTYPE no_ref(void *self) { (void)self; return 1; }

typedef struct {
  Steinberg_Vst_IParamValueQueueVtbl *lpVtbl;
  Steinberg_Vst_ParamID id;
  double value; // normalised
  int points;
} Queue;

static Steinberg_Vst_ParamID SMTG_STDMETHODCALLTYPE queue_id(void *self) { return ((Queue *)self)->id; }
static Steinberg_int32 SMTG_STDMETHODCALLTYPE queue_count(void *self) { return ((Queue *)self)->points; }
static Steinberg_tresult SMTG_STDMETHODCALLTYPE queue_point(void *self, Steinberg_int32 i, Steinberg_int32 *off,
                                                            Steinberg_Vst_ParamValue *value) {
  (void)i;
  *off = 0;
  *value = ((Queue *)self)->value;
  return Steinberg_kResultOk;
}
static Steinberg_tresult SMTG_STDMETHODCALLTYPE queue_add(void *self, Steinberg_int32 off,
                                                          Steinberg_Vst_ParamValue value, Steinberg_int32 *index) {
  (void)self; (void)off; (void)value; (void)index;
  return Steinberg_kNotImplemented;
}
static Steinberg_Vst_IParamValueQueueVtbl QUEUE_VTBL = {no_query, no_ref, no_ref, queue_id, queue_count,
                                                        queue_point, queue_add};

typedef struct {
  Steinberg_Vst_IParameterChangesVtbl *lpVtbl;
  Queue queue;
  int count; // 0 or 1
} Changes;

static Steinberg_int32 SMTG_STDMETHODCALLTYPE changes_count(void *self) { return ((Changes *)self)->count; }
static struct Steinberg_Vst_IParamValueQueue *SMTG_STDMETHODCALLTYPE changes_data(void *self, Steinberg_int32 i) {
  (void)i;
  return (struct Steinberg_Vst_IParamValueQueue *)&((Changes *)self)->queue;
}
static struct Steinberg_Vst_IParamValueQueue *SMTG_STDMETHODCALLTYPE
changes_add(void *self, const Steinberg_Vst_ParamID *id, Steinberg_int32 *index) {
  (void)self; (void)id; (void)index;
  return NULL;
}
static Steinberg_Vst_IParameterChangesVtbl CHANGES_VTBL = {no_query, no_ref, no_ref, changes_count, changes_data,
                                                           changes_add};

// ---- Test harness -----------------------------------------------------------------

typedef struct {
  Steinberg_Vst_IAudioProcessor *processor;
  float in[2][BLOCK], out[2][BLOCK];
  float *ins[2], *outs[2];
  struct Steinberg_Vst_AudioBusBuffers in_bus, out_bus;
  Changes in_changes;
  struct Steinberg_Vst_ProcessData data;
  double phase;
} Runner;

static void runner_init(Runner *r, Steinberg_Vst_IAudioProcessor *processor) {
  memset(r, 0, sizeof(*r));
  r->processor = processor;
  r->ins[0] = r->in[0];
  r->ins[1] = r->in[1];
  r->outs[0] = r->out[0];
  r->outs[1] = r->out[1];
  r->in_bus.numChannels = 2;
  r->in_bus.Steinberg_Vst_AudioBusBuffers_channelBuffers32 = r->ins;
  r->out_bus.numChannels = 2;
  r->out_bus.Steinberg_Vst_AudioBusBuffers_channelBuffers32 = r->outs;
  r->in_changes.lpVtbl = &CHANGES_VTBL;
  r->in_changes.queue.lpVtbl = &QUEUE_VTBL;
  r->data.processMode = Steinberg_Vst_ProcessModes_kRealtime;
  r->data.symbolicSampleSize = Steinberg_Vst_SymbolicSampleSizes_kSample32;
  r->data.numSamples = BLOCK;
  r->data.numInputs = 1;
  r->data.numOutputs = 1;
  r->data.inputs = &r->in_bus;
  r->data.outputs = &r->out_bus;
  r->data.inputParameterChanges = (struct Steinberg_Vst_IParameterChanges *)&r->in_changes;
}

static void queue_param(Runner *r, Steinberg_Vst_ParamID id, double normalised) {
  r->in_changes.queue.id = id;
  r->in_changes.queue.value = normalised;
  r->in_changes.queue.points = 1;
  r->in_changes.count = 1;
}

// Runs a sine for SECONDS and returns the output RMS over the last half, or
// -1 if process() failed or the output went non-finite.
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
    if (r->processor->lpVtbl->process(r->processor, &r->data) != Steinberg_kResultOk) return -1.0;
    r->in_changes.count = 0; // parameter changes apply to the first block only
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

static double to_db(double rms, double ref) { return 20.0 * log10(rms / ref); }

static void utf16_to_ascii(const Steinberg_char16 *in, char *out, size_t len) {
  size_t i = 0;
  for (; i + 1 < len && in[i]; i++) out[i] = in[i] < 128 ? (char)in[i] : '?';
  out[i] = 0;
}

// ---- Main -------------------------------------------------------------------------

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s <PhantomBass.so inside the .vst3 bundle>\n", argv[0]);
    return 2;
  }
  void *lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!lib) {
    fprintf(stderr, "dlopen: %s\n", dlerror());
    return 1;
  }
  Steinberg_TBool (*module_entry)(void *) = (Steinberg_TBool(*)(void *))dlsym(lib, "ModuleEntry");
  Steinberg_TBool (*module_exit)(void) = (Steinberg_TBool(*)(void))dlsym(lib, "ModuleExit");
  Steinberg_IPluginFactory *(*get_factory)(void) =
      (Steinberg_IPluginFactory * (*)(void)) dlsym(lib, "GetPluginFactory");
  CHECK(get_factory != NULL);
  if (module_entry) CHECK(module_entry(lib));

  Steinberg_IPluginFactory *factory = get_factory();
  CHECK(factory != NULL);

  // Find the audio effect class.
  struct Steinberg_PClassInfo info;
  int found = 0;
  for (Steinberg_int32 i = 0; i < factory->lpVtbl->countClasses(factory); i++) {
    CHECK(factory->lpVtbl->getClassInfo(factory, i, &info) == Steinberg_kResultOk);
    if (strcmp(info.category, "Audio Module Class") == 0) {
      found = 1;
      break;
    }
  }
  CHECK(found);
  printf("plugin: %s (%s)\n", info.name, info.category);

  Steinberg_Vst_IComponent *component = NULL;
  CHECK(factory->lpVtbl->createInstance(factory, info.cid, Steinberg_Vst_IComponent_iid, (void **)&component) ==
        Steinberg_kResultOk);
  CHECK(component != NULL);
  CHECK(component->lpVtbl->initialize(component, NULL) == Steinberg_kResultOk);

  Steinberg_Vst_IAudioProcessor *processor = NULL;
  Steinberg_Vst_IEditController *controller = NULL;
  CHECK(component->lpVtbl->queryInterface(component, Steinberg_Vst_IAudioProcessor_iid, (void **)&processor) ==
        Steinberg_kResultOk);
  CHECK(component->lpVtbl->queryInterface(component, Steinberg_Vst_IEditController_iid, (void **)&controller) ==
        Steinberg_kResultOk);

  // Parameters.
  CHECK(controller->lpVtbl->getParameterCount(controller) == 3);
  Steinberg_Vst_ParamID gain_id = 0;
  for (Steinberg_int32 i = 0; i < 3; i++) {
    struct Steinberg_Vst_ParameterInfo pi;
    CHECK(controller->lpVtbl->getParameterInfo(controller, i, &pi) == Steinberg_kResultOk);
    char title[64], text[64];
    Steinberg_Vst_String128 str;
    utf16_to_ascii(pi.title, title, sizeof(title));
    double norm = controller->lpVtbl->getParamNormalized(controller, pi.id);
    controller->lpVtbl->getParamStringByValue(controller, pi.id, norm, str);
    utf16_to_ascii(str, text, sizeof(text));
    printf("param %u: %-24s normalised %.3f (default %.3f) -> %s\n", pi.id, title, norm,
           pi.defaultNormalizedValue, text);
    CHECK(fabs(norm - pi.defaultNormalizedValue) < 1e-9);
    if (strstr(title, "Gain")) gain_id = pi.id;
  }
  CHECK(gain_id != 0);
  double gain_off = 0.0; // normalised minimum: the gain knob's "Off" position
  double gain_0db = controller->lpVtbl->plainParamToNormalized(controller, gain_id, 0.0);

  // Bus and processing setup, as a host does it.
  Steinberg_Vst_SpeakerArrangement stereo = Steinberg_Vst_SpeakerArr_kStereo;
  CHECK(processor->lpVtbl->setBusArrangements(processor, &stereo, 1, &stereo, 1) == Steinberg_kResultTrue);
  CHECK(processor->lpVtbl->canProcessSampleSize(processor, Steinberg_Vst_SymbolicSampleSizes_kSample32) ==
        Steinberg_kResultTrue);
  struct Steinberg_Vst_ProcessSetup setup = {Steinberg_Vst_ProcessModes_kRealtime,
                                             Steinberg_Vst_SymbolicSampleSizes_kSample32, BLOCK, SAMPLE_RATE};
  CHECK(processor->lpVtbl->setupProcessing(processor, &setup) == Steinberg_kResultOk);
  CHECK(component->lpVtbl->activateBus(component, Steinberg_Vst_MediaTypes_kAudio,
                                       Steinberg_Vst_BusDirections_kInput, 0, 1) == Steinberg_kResultOk);
  CHECK(component->lpVtbl->activateBus(component, Steinberg_Vst_MediaTypes_kAudio,
                                       Steinberg_Vst_BusDirections_kOutput, 0, 1) == Steinberg_kResultOk);
  CHECK(component->lpVtbl->setActive(component, 1) == Steinberg_kResultOk);
  CHECK(processor->lpVtbl->setProcessing(processor, 1) == Steinberg_kResultOk);

  Runner *r = calloc(1, sizeof(*r));
  runner_init(r, processor);

  const double bass_hz = 50.0, amp = 0.25;
  double on_rms = run_sine(r, bass_hz, amp);
  CHECK(on_rms > 0.0);
  queue_param(r, gain_id, gain_off);
  double off_rms = run_sine(r, bass_hz, amp);
  CHECK(off_rms > 0.0);
  printf("50 Hz tone: enhancement on %.1f dB, off %.1f dB (re input)\n", to_db(on_rms, amp),
         to_db(off_rms, amp));
  CHECK(off_rms < amp * 0.5);
  CHECK(on_rms > off_rms * 1.5);

  // The controller must now report the automated value.
  CHECK(fabs(controller->lpVtbl->getParamNormalized(controller, gain_id) - gain_off) < 1e-6);

  queue_param(r, gain_id, gain_0db);
  double mid_rms = run_sine(r, 1000.0, amp);
  printf("1 kHz tone: %.2f dB (re input)\n", to_db(mid_rms, amp));
  CHECK(fabs(to_db(mid_rms, amp / sqrt(2.0))) < 1.0);

  CHECK(processor->lpVtbl->setProcessing(processor, 0) == Steinberg_kResultOk);
  CHECK(component->lpVtbl->setActive(component, 0) == Steinberg_kResultOk);
  CHECK(component->lpVtbl->terminate(component) == Steinberg_kResultOk);
  controller->lpVtbl->release(controller);
  processor->lpVtbl->release(processor);
  component->lpVtbl->release(component);
  factory->lpVtbl->release(factory);
  if (module_exit) module_exit();
  free(r);
  dlclose(lib);
  printf("OK\n");
  return 0;
}
