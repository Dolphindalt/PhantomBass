#pragma once

#include <stdint.h>

#include "fixed-math.h"

// Second-order section (biquad), transposed direct form II.
// Coefficients are Q4.28 (not fixed_t): coefficient magnitudes reach 2, and
// with Q2.30 samples the 64-bit state sums must not overflow at full scale.
// State is kept at full 64-bit precision between samples.
#define BIQUAD_COEF_FRAC_BITS 28

typedef struct {
  int32_t b0, b1, b2, a1, a2; // Q4.28, a0 normalized to 1
  int64_t s1, s2;             // Q(30+28)
} Biquad_t;

// Bandpass = 2nd-order Butterworth highpass (low cutoff) cascaded with a
// 2nd-order Butterworth lowpass (high cutoff). 12 dB/octave skirts, -3 dB at
// each cutoff when they are well separated. A linear output gain is folded
// into the lowpass feed-forward coefficients, so it costs nothing per sample.
typedef struct {
  Biquad_t hp;
  Biquad_t lp;
  float sample_rate_hz;
  float high_cutoff_hz;
  float gain;
} BandPassFilter_t;

void BandPassFilter_Init(BandPassFilter_t *filter, float low_cutoff_hz,
                         float high_cutoff_hz, float sample_rate_hz);

// Retune either cutoff / the output gain without disturbing filter state;
// safe to call between samples while streaming. Init sets gain = 1.
void BandPassFilter_SetLowCutoff(BandPassFilter_t *filter, float low_cutoff_hz);
void BandPassFilter_SetHighCutoff(BandPassFilter_t *filter,
                                  float high_cutoff_hz);
void BandPassFilter_SetGain(BandPassFilter_t *filter, float gain);

fixed_t BandPassFilter_Process(BandPassFilter_t *filter, fixed_t input);
