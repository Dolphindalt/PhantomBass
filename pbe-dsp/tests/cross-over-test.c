#include <math.h>
#include <unity.h>

#include "cross-over.h"

#define FS 48000.0f
#define FC 120.0f
#define AMPLITUDE 0.5f
#define N_SAMPLES 48000

static CrossOverFilter_t filter;

void setUp(void) { CrossOverFilter_Init(&filter, FC, FS); }
void tearDown(void) {}

// Run a sine of the given frequency through the filter and return the peak
// magnitude of low, high, and low+high over the second half (steady state).
static void measure_sine(float freq_hz, float *low_peak, float *high_peak,
                         float *sum_peak) {
  *low_peak = *high_peak = *sum_peak = 0.0f;
  for (int n = 0; n < N_SAMPLES; n++) {
    fixed_t x =
        float_to_fixed(AMPLITUDE * sinf(2.0f * (float)M_PI * freq_hz * n / FS));
    fixed_t lo, hi;
    CrossOverFilter_Process(&filter, x, &lo, &hi);
    if (n < N_SAMPLES / 2)
      continue;
    float l = fixed_to_float(lo), h = fixed_to_float(hi);
    *low_peak = fmaxf(*low_peak, fabsf(l));
    *high_peak = fmaxf(*high_peak, fabsf(h));
    *sum_peak = fmaxf(*sum_peak, fabsf(l + h));
  }
}

static float to_db(float peak) { return 20.0f * log10f(peak / AMPLITUDE); }

void test_init_zeroes_state(void) {
  TEST_ASSERT_EQUAL_INT32(0, filter.x1);
  TEST_ASSERT_EQUAL_INT32(0, filter.lp1);
  TEST_ASSERT_EQUAL_INT32(0, filter.lp2);
}

void test_init_coefficients_are_sane(void) {
  // K = tan(pi*120/48000) ~= 0.007854; a0 = K/(1+K), b1 = (1-K)/(1+K)
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.007793f, fixed_to_float(filter.a0));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.984414f, fixed_to_float(filter.b1));
}

void test_silence_in_silence_out(void) {
  for (int n = 0; n < 100; n++) {
    fixed_t lo, hi;
    CrossOverFilter_Process(&filter, 0, &lo, &hi);
    TEST_ASSERT_EQUAL_INT32(0, lo);
    TEST_ASSERT_EQUAL_INT32(0, hi);
  }
}

void test_dc_goes_to_low_band(void) {
  fixed_t x = float_to_fixed(AMPLITUDE), lo = 0, hi = 0;
  for (int n = 0; n < N_SAMPLES; n++)
    CrossOverFilter_Process(&filter, x, &lo, &hi);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, AMPLITUDE, fixed_to_float(lo));
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, fixed_to_float(hi));
}

void test_low_frequency_passes_low_band(void) {
  float lo, hi, sum;
  measure_sine(20.0f, &lo, &hi, &sum);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.0f, to_db(lo));
  TEST_ASSERT_TRUE(to_db(hi) < -25.0f);
}

void test_high_frequency_passes_high_band(void) {
  float lo, hi, sum;
  measure_sine(5000.0f, &lo, &hi, &sum);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.0f, to_db(hi));
  TEST_ASSERT_TRUE(to_db(lo) < -40.0f);
}

void test_both_bands_minus_6db_at_crossover(void) {
  // Linkwitz-Riley signature: both bands at -6 dB at fc.
  float lo, hi, sum;
  measure_sine(FC, &lo, &hi, &sum);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, -6.0f, to_db(lo));
  TEST_ASSERT_FLOAT_WITHIN(0.5f, -6.0f, to_db(hi));
}

void test_second_order_slope(void) {
  // 12 dB/octave: one octave above fc the low band should be ~12 dB below
  // its level at fc (asymptotically; allow slack near the knee).
  float lo_fc, lo_2fc, lo_4fc, hi, sum;
  measure_sine(FC, &lo_fc, &hi, &sum);
  measure_sine(2.0f * FC, &lo_2fc, &hi, &sum);
  measure_sine(4.0f * FC, &lo_4fc, &hi, &sum);
  TEST_ASSERT_FLOAT_WITHIN(2.0f, -12.0f, to_db(lo_4fc) - to_db(lo_2fc));
  TEST_ASSERT_TRUE(to_db(lo_2fc) < to_db(lo_fc) - 6.0f);
}

void test_sum_reconstructs_flat(void) {
  // low + high must be allpass: unity magnitude at every frequency.
  const float freqs[] = {20.0f, 60.0f, FC, 240.0f, 1000.0f, 5000.0f, 15000.0f};
  for (unsigned i = 0; i < sizeof(freqs) / sizeof(freqs[0]); i++) {
    setUp();
    float lo, hi, sum;
    measure_sine(freqs[i], &lo, &hi, &sum);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.1f, 0.0f, to_db(sum),
                                     "low + high is not flat");
  }
}

void test_set_cutoff_matches_fresh_init(void) {
  CrossOverFilter_t ref;
  CrossOverFilter_Init(&ref, 2.0f * FC, FS);
  CrossOverFilter_SetCutoff(&filter, 2.0f * FC, FS);
  TEST_ASSERT_EQUAL_INT32(ref.a0, filter.a0);
  TEST_ASSERT_EQUAL_INT32(ref.b1, filter.b1);
  // Both bands -6 dB at the new crossover.
  float lo, hi, sum;
  measure_sine(2.0f * FC, &lo, &hi, &sum);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, -6.0f, to_db(lo));
  TEST_ASSERT_FLOAT_WITHIN(0.5f, -6.0f, to_db(hi));
}

void test_set_cutoff_preserves_state(void) {
  // Retune mid-stream: state is kept, so both outputs stay continuous. The
  // largest per-sample step of a 50 Hz tone is 2*pi*50/48000*A ~= 0.0065A;
  // a state reset would produce a jump of order A.
  fixed_t prev_lo = 0, prev_hi = 0;
  float max_step = 0.0f;
  for (int n = 0; n < 4800; n++) {
    fixed_t x = float_to_fixed(AMPLITUDE * sinf(2.0f * (float)M_PI * 50.0f * n / FS));
    if (n == 2400) CrossOverFilter_SetCutoff(&filter, 3.0f * FC, FS);
    fixed_t lo, hi;
    CrossOverFilter_Process(&filter, x, &lo, &hi);
    if (n > 2000) {
      max_step = fmaxf(max_step, fabsf(fixed_to_float(lo - prev_lo)));
      max_step = fmaxf(max_step, fabsf(fixed_to_float(hi - prev_hi)));
    }
    prev_lo = lo;
    prev_hi = hi;
  }
  TEST_ASSERT_TRUE(max_step < 0.02f * AMPLITUDE);
  TEST_ASSERT_TRUE(filter.lp1 != 0 || filter.lp2 != 0);
}

void test_impulse_response_decays(void) {
  fixed_t lo, hi;
  CrossOverFilter_Process(&filter, float_to_fixed(1.0f), &lo, &hi);
  for (int n = 0; n < N_SAMPLES; n++)
    CrossOverFilter_Process(&filter, 0, &lo, &hi);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, fixed_to_float(lo));
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.0f, fixed_to_float(hi));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_init_zeroes_state);
  RUN_TEST(test_init_coefficients_are_sane);
  RUN_TEST(test_silence_in_silence_out);
  RUN_TEST(test_dc_goes_to_low_band);
  RUN_TEST(test_low_frequency_passes_low_band);
  RUN_TEST(test_high_frequency_passes_high_band);
  RUN_TEST(test_both_bands_minus_6db_at_crossover);
  RUN_TEST(test_second_order_slope);
  RUN_TEST(test_sum_reconstructs_flat);
  RUN_TEST(test_set_cutoff_matches_fresh_init);
  RUN_TEST(test_set_cutoff_preserves_state);
  RUN_TEST(test_impulse_response_decays);
  return UNITY_END();
}
