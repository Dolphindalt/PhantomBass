#include "cross-over.h"

#include <math.h>

#include "fixed-math.h"

void CrossOverFilter_SetCutoff(CrossOverFilter_t *filter, float cutoff_hz,
                               float sample_rate_hz) {
  float k = tanf((float)M_PI * cutoff_hz / sample_rate_hz);
  filter->a0 = float_to_fixed(k / (1.0f + k));
  // Force 2*a0 + b1 == 1 exactly so that low + high is an exact allpass
  // regardless of coefficient quantization.
  filter->b1 = INIT_FIXED(1, 0) - 2 * filter->a0;
}

void CrossOverFilter_Init(CrossOverFilter_t *filter, float cutoff_hz,
                          float sample_rate_hz) {
  CrossOverFilter_SetCutoff(filter, cutoff_hz, sample_rate_hz);
  filter->x1 = 0;
  filter->lp1 = 0;
  filter->lp2 = 0;
}

// One bilinear one-pole section: y = a0*(x + x1) + b1*y1, evaluated in a
// 64-bit accumulator with a single rounding. a0, b1 > 0 and 2*a0 + b1 == 1,
// so |y| <= max|x| and the result always fits.
static inline fixed_t one_pole(fixed_t a0, fixed_t b1, fixed_t x, fixed_t x1,
                               fixed_t y1) {
  int64_t acc = (int64_t)a0 * ((int64_t)x + x1) + (int64_t)b1 * y1;
  return (fixed_t)fixed_round_shift64(acc, FIXED_FRAC_BITS);
}

void CrossOverFilter_Process(CrossOverFilter_t *filter, fixed_t input,
                             fixed_t *low_out, fixed_t *high_out) {
  // Stage 1: one-pole lowpass on the input
  fixed_t lp1 = one_pole(filter->a0, filter->b1, input, filter->x1, filter->lp1);
  // Stage 2: same one-pole lowpass on stage-1 output -> LR2 lowpass
  fixed_t lp2 = one_pole(filter->a0, filter->b1, lp1, filter->lp1, filter->lp2);
  // First-order allpass = 2*LP1 - input. high = ap - low == -HP_LR2,
  // so low + high == ap: flat magnitude when the bands are summed.
  // The highpass of a full-scale step transiently reaches ~2x full scale,
  // which can exceed the 6 dB headroom, so saturate.
  int64_t high = 2 * (int64_t)lp1 - input - lp2;

  filter->x1 = input;
  filter->lp1 = lp1;
  filter->lp2 = lp2;

  *low_out = lp2;
  *high_out = fixed_sat64(high);
}
