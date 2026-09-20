#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "pbe-dsp.h"

// Plugin internals: owns the PbeDsp_t instance and adapts it to what a host
// provides (planar float channels, arbitrary block sizes, parameters that
// move while audio runs). No plugin-format or GUI dependencies; everything
// here runs on the audio thread except Engine_Init/Prepare/Release.
typedef struct {
  PbeDsp_t dsp;
  float sample_rate_hz;
  uint32_t max_block;
  fixed_t *scratch; // interleaved stereo, 2 * max_block samples

  // Values currently applied to the DSP.
  float gain_db;
  float speaker_cutoff_hz;
  float bandpass_high_hz;

  bool prepared;
} Engine;

#define ENGINE_CHANNELS 2

void Engine_Init(Engine *e, float gain_db, float speaker_cutoff_hz,
                 float bandpass_high_hz);

// Allocates scratch and (re)initialises the DSP for the given sample rate.
// Returns false on allocation failure, in which case Engine_Process is a
// pass-through. Not real-time safe.
bool Engine_Prepare(Engine *e, double sample_rate_hz, uint32_t max_block);
void Engine_Release(Engine *e);

// Audio-thread setters; cheap to call every block with unchanged values.
// All retune the DSP in place without resetting filter state.
void Engine_SetGain(Engine *e, float gain_db);
void Engine_SetSpeakerCutoff(Engine *e, float hz);
void Engine_SetBandpassHigh(Engine *e, float hz);

// Planar stereo in/out; output may alias input. A NULL input channel is
// treated as silence; a NULL output channel is skipped.
void Engine_Process(Engine *e, const float *const *input, float *const *output,
                    uint32_t n_frames);
