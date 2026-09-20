#include <math.h>
#include <unity.h>

#include "band-pass.h"

#define FS 48000.0f
#define F_LOW 60.0f
#define F_HIGH 400.0f
#define AMPLITUDE 0.5f
#define N_SAMPLES 96000

static BandPassFilter_t filter;

void setUp(void) { BandPassFilter_Init(&filter, F_LOW, F_HIGH, FS); }
void tearDown(void) {}

static fixed_t sine(float freq_hz, int n) {
  return float_to_fixed(AMPLITUDE * sinf(2.0f * (float)M_PI * freq_hz * n / FS));
}

// Steady-state peak output for a sine, in dB relative to the input amplitude.
static float gain_db(float freq_hz) {
  float peak = 0.0f;
  for (int n = 0; n < N_SAMPLES; n++) {
    fixed_t y = BandPassFilter_Process(&filter, sine(freq_hz, n));
    if (n >= N_SAMPLES / 2) peak = fmaxf(peak, fabsf(fixed_to_float(y)));
  }
  return 20.0f * log10f(peak / AMPLITUDE);
}

void test_init_zeroes_state(void) {
  TEST_ASSERT_EQUAL_INT64(0, filter.hp.s1);
  TEST_ASSERT_EQUAL_INT64(0, filter.hp.s2);
  TEST_ASSERT_EQUAL_INT64(0, filter.lp.s1);
  TEST_ASSERT_EQUAL_INT64(0, filter.lp.s2);
  TEST_ASSERT_EQUAL_FLOAT(FS, filter.sample_rate_hz);
}

void test_silence_in_silence_out(void) {
  for (int n = 0; n < 100; n++)
    TEST_ASSERT_EQUAL_INT32(0, BandPassFilter_Process(&filter, 0));
}

void test_dc_is_blocked(void) {
  fixed_t y = 0;
  for (int n = 0; n < N_SAMPLES; n++)
    y = BandPassFilter_Process(&filter, float_to_fixed(AMPLITUDE));
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, fixed_to_float(y));
}

void test_passband_is_flat(void) {
  // Geometric center of the band; cutoffs are far enough apart that the
  // two -3 dB skirts contribute negligibly here.
  TEST_ASSERT_FLOAT_WITHIN(0.3f, 0.0f, gain_db(sqrtf(F_LOW * F_HIGH)));
}

void test_minus_3db_at_cutoffs(void) {
  TEST_ASSERT_FLOAT_WITHIN(0.5f, -3.0f, gain_db(F_LOW));
  setUp();
  TEST_ASSERT_FLOAT_WITHIN(0.5f, -3.0f, gain_db(F_HIGH));
}

void test_low_stopband_slope(void) {
  // 2nd-order highpass skirt: ~12 dB/octave below the low cutoff.
  float g1 = gain_db(F_LOW / 4.0f);
  setUp();
  float g2 = gain_db(F_LOW / 8.0f);
  TEST_ASSERT_TRUE(g1 < -20.0f);
  TEST_ASSERT_FLOAT_WITHIN(1.5f, -12.0f, g2 - g1);
}

void test_high_stopband_slope(void) {
  // 2nd-order lowpass skirt: ~12 dB/octave above the high cutoff.
  float g1 = gain_db(F_HIGH * 4.0f);
  setUp();
  float g2 = gain_db(F_HIGH * 8.0f);
  TEST_ASSERT_TRUE(g1 < -20.0f);
  TEST_ASSERT_FLOAT_WITHIN(1.5f, -12.0f, g2 - g1);
}

void test_set_high_cutoff_moves_upper_edge(void) {
  // 800 Hz is well inside the stopband of a 400 Hz LP...
  float before = gain_db(800.0f);
  // ...and inside the passband once the high cutoff moves to 3.2 kHz.
  setUp();
  BandPassFilter_SetHighCutoff(&filter, 3200.0f);
  float after = gain_db(800.0f);
  TEST_ASSERT_TRUE(before < -10.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.0f, after);
  // The new cutoff is -3 dB.
  setUp();
  BandPassFilter_SetHighCutoff(&filter, 3200.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, -3.0f, gain_db(3200.0f));
}

void test_set_high_cutoff_leaves_low_edge_alone(void) {
  BandPassFilter_SetHighCutoff(&filter, 2000.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, -3.0f, gain_db(F_LOW));
}

void test_set_high_cutoff_preserves_state(void) {
  // Run an in-band tone, retune the LP upward mid-stream, and check the
  // output does not jump: the sample-to-sample step stays bounded by the
  // largest step the same tone produces in steady state (with some slack).
  const float f = 150.0f;
  fixed_t prev = 0;
  float max_step = 0.0f;
  for (int n = 0; n < 4800; n++) {
    fixed_t y = BandPassFilter_Process(&filter, sine(f, n));
    if (n > 2400) max_step = fmaxf(max_step, fabsf(fixed_to_float(y - prev)));
    prev = y;
  }
  TEST_ASSERT_TRUE(filter.lp.s1 != 0 || filter.lp.s2 != 0);
  BandPassFilter_SetHighCutoff(&filter, 1000.0f);
  for (int n = 4800; n < 4900; n++) {
    fixed_t y = BandPassFilter_Process(&filter, sine(f, n));
    TEST_ASSERT_TRUE(fabsf(fixed_to_float(y - prev)) < 2.0f * max_step);
    prev = y;
  }
}

void test_set_low_cutoff_moves_lower_edge(void) {
  // 30 Hz is well below a 60 Hz HP; move the low cutoff down to 15 Hz and
  // it lands inside the passband. The high edge is untouched.
  float before = gain_db(30.0f);
  setUp();
  BandPassFilter_SetLowCutoff(&filter, 15.0f);
  float after = gain_db(30.0f);
  TEST_ASSERT_TRUE(before < -10.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.0f, after);
  setUp();
  BandPassFilter_SetLowCutoff(&filter, 15.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, -3.0f, gain_db(F_HIGH));
  BandPassFilter_t ref;
  BandPassFilter_Init(&ref, 15.0f, F_HIGH, FS);
  TEST_ASSERT_EQUAL_INT32(ref.hp.b0, filter.hp.b0);
  TEST_ASSERT_EQUAL_INT32(ref.hp.a1, filter.hp.a1);
}

void test_set_gain_scales_output(void) {
  float g0 = gain_db(sqrtf(F_LOW * F_HIGH));
  setUp();
  BandPassFilter_SetGain(&filter, 0.5f);
  float g1 = gain_db(sqrtf(F_LOW * F_HIGH));
  TEST_ASSERT_FLOAT_WITHIN(0.1f, -6.02f, g1 - g0);
  // Gain survives a cutoff change.
  BandPassFilter_SetHighCutoff(&filter, 2.0f * F_HIGH);
  TEST_ASSERT_EQUAL_FLOAT(0.5f, filter.gain);
  setUp();
  BandPassFilter_SetGain(&filter, 0.5f);
  BandPassFilter_SetHighCutoff(&filter, 2.0f * F_HIGH);
  TEST_ASSERT_FLOAT_WITHIN(0.2f, -6.02f, gain_db(sqrtf(F_LOW * F_HIGH)));
}

void test_impulse_response_decays(void) {
  fixed_t y = BandPassFilter_Process(&filter, float_to_fixed(1.0f));
  for (int n = 0; n < N_SAMPLES; n++) y = BandPassFilter_Process(&filter, 0);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, fixed_to_float(y));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_init_zeroes_state);
  RUN_TEST(test_silence_in_silence_out);
  RUN_TEST(test_dc_is_blocked);
  RUN_TEST(test_passband_is_flat);
  RUN_TEST(test_minus_3db_at_cutoffs);
  RUN_TEST(test_low_stopband_slope);
  RUN_TEST(test_high_stopband_slope);
  RUN_TEST(test_set_high_cutoff_moves_upper_edge);
  RUN_TEST(test_set_high_cutoff_leaves_low_edge_alone);
  RUN_TEST(test_set_high_cutoff_preserves_state);
  RUN_TEST(test_set_low_cutoff_moves_lower_edge);
  RUN_TEST(test_set_gain_scales_output);
  RUN_TEST(test_impulse_response_decays);
  return UNITY_END();
}
