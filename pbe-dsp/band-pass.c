#include "band-pass.h"

#include <math.h>

#define COEF_FRAC_BITS BIQUAD_COEF_FRAC_BITS
#define BUTTERWORTH_Q 0.70710678f // 1/sqrt(2)

static int32_t float_to_coef(float f) {
  return (int32_t)lrintf(f * (float)(1 << COEF_FRAC_BITS));
}

static void biquad_reset(Biquad_t *bq) {
  bq->s1 = 0;
  bq->s2 = 0;
}

// RBJ cookbook lowpass / highpass, normalized so a0 == 1. `gain` scales the
// feed-forward path (output gain).
static void biquad_set_lowpass(Biquad_t *bq, float cutoff_hz,
                               float sample_rate_hz, float gain) {
  float w0 = 2.0f * (float)M_PI * cutoff_hz / sample_rate_hz;
  float c = cosf(w0);
  float alpha = sinf(w0) / (2.0f * BUTTERWORTH_Q);
  float a0 = 1.0f + alpha;
  bq->b0 = float_to_coef(gain * ((1.0f - c) / 2.0f) / a0);
  bq->b1 = float_to_coef(gain * (1.0f - c) / a0);
  bq->b2 = bq->b0;
  bq->a1 = float_to_coef((-2.0f * c) / a0);
  bq->a2 = float_to_coef((1.0f - alpha) / a0);
}

static void biquad_set_highpass(Biquad_t *bq, float cutoff_hz,
                                float sample_rate_hz) {
  float w0 = 2.0f * (float)M_PI * cutoff_hz / sample_rate_hz;
  float c = cosf(w0);
  float alpha = sinf(w0) / (2.0f * BUTTERWORTH_Q);
  float a0 = 1.0f + alpha;
  bq->b0 = float_to_coef(((1.0f + c) / 2.0f) / a0);
  bq->b1 = float_to_coef(-(1.0f + c) / a0);
  bq->b2 = bq->b0;
  bq->a1 = float_to_coef((-2.0f * c) / a0);
  bq->a2 = float_to_coef((1.0f - alpha) / a0);
}

// Transposed direct form II. Coefficient changes between calls do not
// produce the state-scaling glitches that direct form I/II suffer from.
//
// The output is quantized to fixed_t but the feedback uses the *exact* value:
// the rounding error `err` (|err| < 2^27) is fed back through a1/a2 as well.
// Without this, poles near z = 1 (bass-range cutoffs) produce a dead zone
// where the decay per sample is under half an LSB and the state locks up.
static inline fixed_t biquad_process(Biquad_t *bq, fixed_t x) {
  int64_t acc = (int64_t)bq->b0 * x + bq->s1; // Q(30+28)
  fixed_t y = fixed_sat64(fixed_round_shift64(acc, COEF_FRAC_BITS));
  int64_t err = acc - ((int64_t)y << COEF_FRAC_BITS);
  int64_t fb1 = (int64_t)bq->a1 * y + (((int64_t)bq->a1 * err) >> COEF_FRAC_BITS);
  int64_t fb2 = (int64_t)bq->a2 * y + (((int64_t)bq->a2 * err) >> COEF_FRAC_BITS);
  bq->s1 = (int64_t)bq->b1 * x - fb1 + bq->s2;
  bq->s2 = (int64_t)bq->b2 * x - fb2;
  return y;
}

void BandPassFilter_Init(BandPassFilter_t *filter, float low_cutoff_hz,
                         float high_cutoff_hz, float sample_rate_hz) {
  filter->sample_rate_hz = sample_rate_hz;
  filter->high_cutoff_hz = high_cutoff_hz;
  filter->gain = 1.0f;
  biquad_set_highpass(&filter->hp, low_cutoff_hz, sample_rate_hz);
  biquad_set_lowpass(&filter->lp, high_cutoff_hz, sample_rate_hz, 1.0f);
  biquad_reset(&filter->hp);
  biquad_reset(&filter->lp);
}

void BandPassFilter_SetLowCutoff(BandPassFilter_t *filter, float low_cutoff_hz) {
  biquad_set_highpass(&filter->hp, low_cutoff_hz, filter->sample_rate_hz);
}

void BandPassFilter_SetHighCutoff(BandPassFilter_t *filter,
                                  float high_cutoff_hz) {
  filter->high_cutoff_hz = high_cutoff_hz;
  biquad_set_lowpass(&filter->lp, high_cutoff_hz, filter->sample_rate_hz,
                     filter->gain);
}

void BandPassFilter_SetGain(BandPassFilter_t *filter, float gain) {
  filter->gain = gain;
  biquad_set_lowpass(&filter->lp, filter->high_cutoff_hz,
                     filter->sample_rate_hz, gain);
}

fixed_t BandPassFilter_Process(BandPassFilter_t *filter, fixed_t input) {
  return biquad_process(&filter->lp, biquad_process(&filter->hp, input));
}
