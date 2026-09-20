#include "engine.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

// The harmonics band is [speaker cutoff, bandpass high]; never let it invert.
static float clamp_bandpass_high(const Engine *e, float hz) {
  return hz < e->speaker_cutoff_hz ? e->speaker_cutoff_hz : hz;
}

void Engine_Init(Engine *e, float gain_db, float speaker_cutoff_hz,
                 float bandpass_high_hz) {
  memset(e, 0, sizeof(*e));
  e->gain_db = gain_db;
  e->speaker_cutoff_hz = speaker_cutoff_hz;
  e->bandpass_high_hz = bandpass_high_hz;
}

bool Engine_Prepare(Engine *e, double sample_rate_hz, uint32_t max_block) {
  if (max_block == 0) max_block = 1;
  fixed_t *scratch = realloc(e->scratch, sizeof(fixed_t) * ENGINE_CHANNELS * max_block);
  if (!scratch) {
    e->prepared = false;
    return false;
  }
  e->scratch = scratch;
  e->max_block = max_block;
  e->sample_rate_hz = (float)sample_rate_hz;
  PbeDsp_Init(&e->dsp, e->speaker_cutoff_hz, clamp_bandpass_high(e, e->bandpass_high_hz),
              e->gain_db, e->sample_rate_hz);
  e->prepared = true;
  return true;
}

void Engine_Release(Engine *e) {
  free(e->scratch);
  e->scratch = NULL;
  e->max_block = 0;
  e->prepared = false;
}

void Engine_SetGain(Engine *e, float gain_db) {
  if (gain_db == e->gain_db) return;
  e->gain_db = gain_db;
  if (e->prepared) PbeDsp_SetGain(&e->dsp, gain_db);
}

void Engine_SetSpeakerCutoff(Engine *e, float hz) {
  if (hz == e->speaker_cutoff_hz) return;
  e->speaker_cutoff_hz = hz;
  if (!e->prepared) return;
  PbeDsp_SetSpeakerCutoff(&e->dsp, hz);
  // The band's lower edge moved; keep its upper edge from crossing it.
  PbeDsp_SetBandPassHighCutoff(&e->dsp, clamp_bandpass_high(e, e->bandpass_high_hz));
}

void Engine_SetBandpassHigh(Engine *e, float hz) {
  if (hz == e->bandpass_high_hz) return;
  e->bandpass_high_hz = hz;
  if (e->prepared) PbeDsp_SetBandPassHighCutoff(&e->dsp, clamp_bandpass_high(e, hz));
}

static inline fixed_t sample_to_fixed(float x) {
  // Saturate at the fixed_t limits (about +/-2.0 in Q2.30) so that a hot or
  // non-finite input from the host can't wrap and poison the filters.
  if (x != x) return 0; // NaN
  float v = x * (float)FIXED_SCALE;
  if (v >= (float)INT32_MAX) return INT32_MAX;
  if (v <= (float)INT32_MIN) return INT32_MIN;
  return (fixed_t)lrintf(v);
}

static inline float fixed_to_sample(fixed_t x) {
  return (float)x * (1.0f / (float)FIXED_SCALE);
}

static void passthrough(const float *const *input, float *const *output,
                        uint32_t n_frames) {
  for (int ch = 0; ch < ENGINE_CHANNELS; ch++) {
    if (!output[ch]) continue;
    if (input[ch]) {
      if (output[ch] != input[ch]) memmove(output[ch], input[ch], sizeof(float) * n_frames);
    } else {
      memset(output[ch], 0, sizeof(float) * n_frames);
    }
  }
}

void Engine_Process(Engine *e, const float *const *input, float *const *output,
                    uint32_t n_frames) {
  if (!e->prepared) {
    passthrough(input, output, n_frames);
    return;
  }
  for (uint32_t start = 0; start < n_frames; start += e->max_block) {
    uint32_t n = n_frames - start;
    if (n > e->max_block) n = e->max_block;

    for (uint32_t i = 0; i < n; i++) {
      e->scratch[2 * i] = input[0] ? sample_to_fixed(input[0][start + i]) : 0;
      e->scratch[2 * i + 1] = input[1] ? sample_to_fixed(input[1][start + i]) : 0;
    }

    PbeDsp_ProcessBlock(&e->dsp, e->scratch, e->scratch, n);

    for (uint32_t i = 0; i < n; i++) {
      if (output[0]) output[0][start + i] = fixed_to_sample(e->scratch[2 * i]);
      if (output[1]) output[1][start + i] = fixed_to_sample(e->scratch[2 * i + 1]);
    }
  }
}
