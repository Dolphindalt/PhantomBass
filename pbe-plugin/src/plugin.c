// CPLUG entry points for everything except the GUI (see gui/window-pugl.c).
#include <cplug.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine.h"
#include "param-queue.h"
#include "params.h"
#include "platform.h"
#include "plugin.h"

struct PbePlugin {
  CplugHostContext *host;
  Engine engine;

  // Plain parameter values, the authoritative copy. Written by the audio
  // thread (host automation, GUI queue) and by the host's main thread (state
  // load, controller edits); read by the GUI thread. Aligned 4-byte float
  // stores are atomic on every platform we target, so a reader sees either
  // the old or the new value, never a torn one.
  float values[PARAM_COUNT];

  // Bit i: values[i] changed outside the GUI; the GUI re-reads it on its tick.
  cplug_atomic_i32 gui_dirty_mask;

  ParamQueue gui_queue;
};

static void set_value(PbePlugin *p, ParamIndex idx, double plain, bool notify_gui) {
  p->values[idx] = (float)Param_Clamp(idx, plain);
  if (notify_gui) pbe_atomic_fetch_or_i32(&p->gui_dirty_mask, 1 << idx);
}

// ---- Lifecycle --------------------------------------------------------------

void cplug_libraryLoad(void) {}
void cplug_libraryUnload(void) {}

void *cplug_createPlugin(CplugHostContext *ctx) {
  PbePlugin *p = calloc(1, sizeof(*p));
  if (!p) return NULL;
  p->host = ctx;
  for (int i = 0; i < PARAM_COUNT; i++) p->values[i] = Param_Info(i)->def;
  ParamQueue_Init(&p->gui_queue);
  Engine_Init(&p->engine, (float)Param_GainDb(p->values[PARAM_GAIN]), p->values[PARAM_SPEAKER_CUTOFF],
              p->values[PARAM_BANDPASS_HIGH]);
  return p;
}

void cplug_destroyPlugin(void *ptr) {
  PbePlugin *p = ptr;
  if (!p) return;
  Engine_Release(&p->engine);
  free(p);
}

// ---- Busses -----------------------------------------------------------------

uint32_t cplug_getNumInputBusses(void *ptr) { (void)ptr; return 1; }
uint32_t cplug_getNumOutputBusses(void *ptr) { (void)ptr; return 1; }
uint32_t cplug_getInputBusChannelCount(void *ptr, uint32_t idx) { (void)ptr; (void)idx; return ENGINE_CHANNELS; }
uint32_t cplug_getOutputBusChannelCount(void *ptr, uint32_t idx) { (void)ptr; (void)idx; return ENGINE_CHANNELS; }

void cplug_getInputBusName(void *ptr, uint32_t idx, char *buf, size_t buflen) {
  (void)ptr; (void)idx;
  snprintf(buf, buflen, "Stereo In");
}
void cplug_getOutputBusName(void *ptr, uint32_t idx, char *buf, size_t buflen) {
  (void)ptr; (void)idx;
  snprintf(buf, buflen, "Stereo Out");
}

uint32_t cplug_getLatencyInSamples(void *ptr) { (void)ptr; return 0; }
uint32_t cplug_getTailInSamples(void *ptr) { (void)ptr; return 0; }

void cplug_setSampleRateAndBlockSize(void *ptr, double sampleRate, uint32_t maxBlockSize) {
  PbePlugin *p = ptr;
  Engine_Prepare(&p->engine, sampleRate, maxBlockSize);
}

// ---- Parameters -------------------------------------------------------------

uint32_t cplug_getNumParameters(void *ptr) { (void)ptr; return PARAM_COUNT; }
uint32_t cplug_getParameterID(void *ptr, uint32_t index) { (void)ptr; return Param_Info(index)->id; }

uint32_t cplug_getParameterFlags(void *ptr, uint32_t id) {
  (void)ptr; (void)id;
  return CPLUG_FLAG_PARAMETER_IS_AUTOMATABLE;
}

void cplug_getParameterRange(void *ptr, uint32_t id, double *min, double *max) {
  (void)ptr;
  int idx = Param_IndexFromId(id);
  if (idx < 0) { *min = 0; *max = 1; return; }
  *min = Param_Info(idx)->min;
  *max = Param_Info(idx)->max;
}

void cplug_getParameterName(void *ptr, uint32_t id, char *buf, size_t buflen) {
  (void)ptr;
  int idx = Param_IndexFromId(id);
  snprintf(buf, buflen, "%s", idx < 0 ? "" : Param_Info(idx)->name);
}

double cplug_getParameterValue(void *ptr, uint32_t id) {
  const PbePlugin *p = ptr;
  int idx = Param_IndexFromId(id);
  return idx < 0 ? 0.0 : p->values[idx];
}

double cplug_getDefaultParameterValue(void *ptr, uint32_t id) {
  (void)ptr;
  int idx = Param_IndexFromId(id);
  return idx < 0 ? 0.0 : Param_Info(idx)->def;
}

// Host-originated change (automation on the audio thread, or a controller
// edit on the main thread). The GUI is told to refresh.
void cplug_setParameterValue(void *ptr, uint32_t id, double value) {
  PbePlugin *p = ptr;
  int idx = Param_IndexFromId(id);
  if (idx >= 0) set_value(p, idx, value, true);
}

double cplug_denormaliseParameterValue(void *ptr, uint32_t id, double normalised) {
  (void)ptr;
  int idx = Param_IndexFromId(id);
  return idx < 0 ? normalised : Param_Denormalise(idx, normalised);
}

double cplug_normaliseParameterValue(void *ptr, uint32_t id, double plain) {
  (void)ptr;
  int idx = Param_IndexFromId(id);
  return idx < 0 ? plain : Param_Normalise(idx, plain);
}

double cplug_parameterStringToValue(void *ptr, uint32_t id, const char *str) {
  (void)ptr;
  int idx = Param_IndexFromId(id);
  return idx < 0 ? 0.0 : Param_Parse(idx, str);
}

void cplug_parameterValueToString(void *ptr, uint32_t id, char *buf, size_t bufsize, double value) {
  (void)ptr;
  int idx = Param_IndexFromId(id);
  if (idx < 0) { snprintf(buf, bufsize, "%g", value); return; }
  Param_Format(idx, value, buf, bufsize);
}

// ---- Audio ------------------------------------------------------------------

static void drain_gui_queue(PbePlugin *p, CplugProcessContext *ctx) {
  ParamEvent ev;
  while (ParamQueue_Pop(&p->gui_queue, &ev)) {
    int idx = Param_IndexFromId(ev.param_id);
    if (idx < 0) continue;
    if (ev.type == CPLUG_EVENT_PARAM_CHANGE_UPDATE) set_value(p, idx, ev.value, false);
    if (ev.forward_to_host) {
      CplugEvent out;
      memset(&out, 0, sizeof(out));
      out.parameter.type = ev.type;
      out.parameter.id = ev.param_id;
      out.parameter.value = ev.value;
      ctx->enqueueEvent(ctx, &out, 0);
    }
  }
}

static void apply_params(PbePlugin *p) {
  Engine_SetGain(&p->engine, (float)Param_GainDb(p->values[PARAM_GAIN]));
  Engine_SetSpeakerCutoff(&p->engine, p->values[PARAM_SPEAKER_CUTOFF]);
  Engine_SetBandpassHigh(&p->engine, p->values[PARAM_BANDPASS_HIGH]);
}

void cplug_process(void *ptr, CplugProcessContext *ctx) {
  PbePlugin *p = ptr;

  drain_gui_queue(p, ctx);

  float **in = ctx->numInputs > 0 ? ctx->getAudioInput(ctx, 0) : NULL;
  float **out = ctx->numOutputs > 0 ? ctx->getAudioOutput(ctx, 0) : NULL;

  // Sample-accurate loop: parameter events land between audio sub-blocks.
  CplugEvent event;
  uint32_t frame = 0;
  while (ctx->dequeueEvent(ctx, &event, frame)) {
    switch (event.type) {
    case CPLUG_EVENT_PARAM_CHANGE_UPDATE:
      cplug_setParameterValue(p, event.parameter.id, event.parameter.value);
      break;
    case CPLUG_EVENT_PROCESS_AUDIO: {
      uint32_t end = event.processAudio.endFrame;
      if (end > frame) {
        apply_params(p);
        const float *ins[ENGINE_CHANNELS];
        float *outs[ENGINE_CHANNELS];
        for (int ch = 0; ch < ENGINE_CHANNELS; ch++) {
          ins[ch] = (in && in[ch]) ? in[ch] + frame : NULL;
          outs[ch] = (out && out[ch]) ? out[ch] + frame : NULL;
        }
        Engine_Process(&p->engine, ins, outs, end - frame);
      }
      frame = end;
      break;
    }
    default:
      break;
    }
  }
}

// ---- State ------------------------------------------------------------------
// Chunk layout (little-endian): "PBE1", uint32 count, then count x
// { uint32 id, float value }. Unknown ids are ignored and missing ones keep
// their current value, so parameters can be added or removed later.

#define STATE_MAGIC "PBE1"
#define STATE_MAX_ENTRIES 64

static void put_u32(uint8_t *b, uint32_t v) {
  b[0] = (uint8_t)v; b[1] = (uint8_t)(v >> 8); b[2] = (uint8_t)(v >> 16); b[3] = (uint8_t)(v >> 24);
}
static uint32_t get_u32(const uint8_t *b) {
  return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

void cplug_saveState(void *ptr, const void *stateCtx, cplug_writeProc writeProc) {
  PbePlugin *p = ptr;
  uint8_t buf[8 + PARAM_COUNT * 8];
  memcpy(buf, STATE_MAGIC, 4);
  put_u32(buf + 4, PARAM_COUNT);
  for (int i = 0; i < PARAM_COUNT; i++) {
    uint32_t bits;
    float v = p->values[i];
    memcpy(&bits, &v, 4);
    put_u32(buf + 8 + i * 8, Param_Info(i)->id);
    put_u32(buf + 12 + i * 8, bits);
  }
  writeProc(stateCtx, buf, sizeof(buf));
}

void cplug_loadState(void *ptr, const void *stateCtx, cplug_readProc readProc) {
  PbePlugin *p = ptr;
  uint8_t buf[8 + STATE_MAX_ENTRIES * 8];
  // Streams may hand out fewer bytes per call than asked for; read to EOF.
  int64_t n = 0;
  while ((size_t)n < sizeof(buf)) {
    int64_t got = readProc(stateCtx, buf + n, sizeof(buf) - (size_t)n);
    if (got <= 0) break;
    n += got;
  }
  if (n < 8 || memcmp(buf, STATE_MAGIC, 4) != 0) return;
  uint32_t count = get_u32(buf + 4);
  if (count > STATE_MAX_ENTRIES) count = STATE_MAX_ENTRIES;
  for (uint32_t i = 0; i < count && (int64_t)(8 + i * 8 + 8) <= n; i++) {
    int idx = Param_IndexFromId(get_u32(buf + 8 + i * 8));
    if (idx < 0) continue;
    uint32_t bits = get_u32(buf + 12 + i * 8);
    float v;
    memcpy(&v, &bits, 4);
    if (v == v) set_value(p, idx, v, true); // skip NaN
  }
}

// ---- GUI-facing API ---------------------------------------------------------

bool Plugin_GuiRunsOnHostThread(void) { return PBE_GUI_ON_HOST_THREAD; }

double Plugin_GetParamPlain(const PbePlugin *p, ParamIndex idx) { return p->values[idx]; }

double Plugin_GetParamNormalised(const PbePlugin *p, ParamIndex idx) {
  return Param_Normalise(idx, p->values[idx]);
}

uint32_t Plugin_TakeGuiDirtyMask(PbePlugin *p) {
  return (uint32_t)cplug_atomic_exchange_i32(&p->gui_dirty_mask, 0);
}

// Gesture and value events from the GUI. When the GUI lives on the host's UI
// thread and the host is VST3, the host is told directly (IComponentHandler
// begin/perform/endEdit, which some hosts require for automation to record);
// the value is still queued so the audio thread applies it immediately.
// Otherwise everything goes through the audio thread, which forwards it to
// the host as output parameter changes (CLAP gets proper gesture events).
static void send_gui_event(PbePlugin *p, uint32_t type, ParamIndex idx, double plain) {
  ParamEvent ev;
  ev.type = type;
  ev.param_id = Param_Info(idx)->id;
  ev.value = plain;
  ev.forward_to_host = true;

  if (PBE_GUI_ON_HOST_THREAD && p->host && p->host->type == CPLUG_PLUGIN_IS_VST3 &&
      p->host->sendParamEvent) {
    CplugEvent ce;
    memset(&ce, 0, sizeof(ce));
    ce.parameter.type = type;
    ce.parameter.id = ev.param_id;
    ce.parameter.value = plain;
    p->host->sendParamEvent(p->host, &ce);
    if (type != CPLUG_EVENT_PARAM_CHANGE_UPDATE) return;
    ev.forward_to_host = false;
  }
  ParamQueue_Push(&p->gui_queue, &ev);
}

void Plugin_GuiBeginGesture(PbePlugin *p, ParamIndex idx) {
  send_gui_event(p, CPLUG_EVENT_PARAM_CHANGE_BEGIN, idx, p->values[idx]);
}

void Plugin_GuiSetParamNormalised(PbePlugin *p, ParamIndex idx, double normalised) {
  send_gui_event(p, CPLUG_EVENT_PARAM_CHANGE_UPDATE, idx, Param_Denormalise(idx, normalised));
}

void Plugin_GuiEndGesture(PbePlugin *p, ParamIndex idx) {
  send_gui_event(p, CPLUG_EVENT_PARAM_CHANGE_END, idx, p->values[idx]);
}
