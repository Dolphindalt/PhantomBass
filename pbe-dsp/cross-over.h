#pragma once

#include "fixed-math.h"

// Second-order Linkwitz-Riley (LR2) crossover: two cascaded bilinear one-pole
// sections. The high output is the *inverted* LR2 highpass so that
// low + high == first-order allpass (flat magnitude, no phase cancellation).
typedef struct {
  fixed_t a0, b1; // bilinear one-pole coefficients
  fixed_t x1;     // previous input
  fixed_t lp1;    // stage-1 output (also stage-2's previous input)
  fixed_t lp2;    // stage-2 output
} CrossOverFilter_t;

void CrossOverFilter_Init(CrossOverFilter_t *filter, float cutoff_hz,
                          float sample_rate_hz);

// Retune the crossover frequency without disturbing filter state; safe to
// call between samples while streaming.
void CrossOverFilter_SetCutoff(CrossOverFilter_t *filter, float cutoff_hz,
                               float sample_rate_hz);

void CrossOverFilter_Process(CrossOverFilter_t *filter, fixed_t input,
                             fixed_t *low_out, fixed_t *high_out);
