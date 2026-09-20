#pragma once

#include <stddef.h>
#include <stdint.h>

#include "band-pass.h"
#include "concat-columns.h"
#include "cross-over.h"
#include "fixed-math.h"
#include "full-wave-integrator.h"
#include "select-columns.h"

// Psychoacoustic bass enhancement for band-limited (small-speaker) playback.
//
//   stereo in ──► crossover (fc = speaker cutoff) ──► hp_left  ─────────────┐
//                       │                          ──► hp_right ────────────┤
//                       └──► lp_stereo ──► (L+R)/2 ──► ↓4 ──► full-wave     │
//                            integrator ──► bandpass (fc .. high) · G ──►   │
//                            ↑4 (linear interp + smoothing) ──────────────►(+)
//                                                                           │
//   stereo out ◄── concat(clip(hp_left + y_G), clip(hp_right + y_G)) ◄──────┘
//
// The output carries no content below the speaker cutoff, but the bass pitch
// is preserved by the harmonics the integrator generates from the lowpass
// band. The lowpass band is < ~60 Hz and the enhancement < ~1 kHz, so that
// whole path runs at fs / PBE_DECIMATION, cutting its cost by that factor and
// moving the filter poles away from z = 1 (better fixed-point behaviour).
//
// Samples are Q2.30 (see fixed-math.h): full scale is 1 << 30.

#define PBE_DECIMATION 4

// The integrator input is pre-scaled by 2^-PBE_INTEGRATOR_SHIFT so that the
// accumulator (which ramps to 4A/w per period) stays within int32 for
// full-scale bass down to a few Hz; the factor is compensated in the
// bandpass gain.
#define PBE_INTEGRATOR_SHIFT 9

// Output full scale; the mixed output is clipped to +/- this value.
#define PBE_FULL_SCALE INIT_FIXED_GLOBAL(1, 0)

// Gains at or below this are treated as off (enhancement muted).
#define PBE_GAIN_OFF_DB (-60.0f)

// One-pole smoother on the up-sampled enhancement, suppressing interpolation
// images around fs / PBE_DECIMATION. Well above any useful bandpass high
// cutoff (-0.3 dB at 1 kHz, -1 dB at 2 kHz).
#define PBE_SMOOTHER_CUTOFF_HZ 4000.0f

typedef struct {
  SelectColumns_t select_left;
  SelectColumns_t select_right;
  CrossOverFilter_t crossover_left;
  CrossOverFilter_t crossover_right;
  ConcatColumns_t concat;

  // Decimated bass path, running at sample_rate_hz / PBE_DECIMATION.
  uint8_t phase;          // 0 .. PBE_DECIMATION-1
  fixed_t decimate_acc;   // running sum of (lp_mono / PBE_DECIMATION)
  FullWaveIntegrator_t integrator;
  BandPassFilter_t bandpass; // carries G and the integrator normalization
  fixed_t enhance_prev;   // previous decimated bandpass output
  fixed_t enhance_cur;    // latest decimated bandpass output
  fixed_t smooth_alpha;   // one-pole smoother coefficient
  fixed_t smooth_state;

  float speaker_cutoff_hz;
  float sample_rate_hz;
  float gain_db;
} PbeDsp_t;

// speaker_cutoff_hz sets both the crossover frequency and the bandpass lower
// cutoff. bandpass_high_hz and gain_db are the two tuning parameters.
//
// gain_db is referenced so that 0 dB makes a bass tone at the speaker cutoff
// generate harmonics at about the tone's own level; sensible values are
// roughly -12 .. +12 dB. Lower bass tones come out proportionally louder
// (the integrator's gain is 1/f), which is the intended pitch preservation.
void PbeDsp_Init(PbeDsp_t *pbe, float speaker_cutoff_hz, float bandpass_high_hz,
                 float gain_db, float sample_rate_hz);

// Runtime tuning; all are safe to call between frames while streaming (no
// filter state is reset). None is cheap on a soft-float core (powf / trig),
// so call them at control rate, not per sample.
void PbeDsp_SetGain(PbeDsp_t *pbe, float gain_db);
void PbeDsp_SetBandPassHighCutoff(PbeDsp_t *pbe, float bandpass_high_hz);
// Moves the crossover and the bandpass lower cutoff together and re-derives
// the gain reference for the new cutoff.
void PbeDsp_SetSpeakerCutoff(PbeDsp_t *pbe, float speaker_cutoff_hz);

// Process one interleaved stereo frame: frame_in[0] = left, frame_in[1] = right.
// frame_out may alias frame_in.
void PbeDsp_ProcessFrame(PbeDsp_t *pbe, const fixed_t frame_in[2],
                         fixed_t frame_out[2]);

// Process n_frames interleaved stereo frames (2 * n_frames samples each way).
// output may alias input.
void PbeDsp_ProcessBlock(PbeDsp_t *pbe, const fixed_t *input, fixed_t *output,
                         size_t n_frames);
