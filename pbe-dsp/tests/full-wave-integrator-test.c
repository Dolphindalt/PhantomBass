#include <math.h>
#include <unity.h>

#include "full-wave-integrator.h"

// Q2.30 only spans +/-2, so use exactly representable fractions as the
// "integer" test values: Q(n) == n/16.
#define Q(n) ((fixed_t)((n) * (FIXED_SCALE / 16)))

static FullWaveIntegrator_t fwi;

void setUp(void) { FullWaveIntegrator_Init(&fwi); }
void tearDown(void) {}

// Feed a sequence and return the last output.
static fixed_t run(const fixed_t *u, int n) {
  fixed_t y = 0;
  for (int i = 0; i < n; i++) y = FullWaveIntegrator_Process(&fwi, u[i]);
  return y;
}

void test_init_zeroes_state(void) {
  TEST_ASSERT_EQUAL_INT32(0, fwi.u_prev);
  TEST_ASSERT_EQUAL_INT32(0, fwi.y_prev);
}

void test_silence_in_silence_out(void) {
  for (int i = 0; i < 100; i++)
    TEST_ASSERT_EQUAL_INT32(0, FullWaveIntegrator_Process(&fwi, 0));
}

void test_first_positive_sample_resets(void) {
  // u[-1] = 0 (initial state) <= 0 and u[0] > 0 -> y[0] = 0
  TEST_ASSERT_EQUAL_INT32(0, FullWaveIntegrator_Process(&fwi, Q(5)));
}

void test_output_is_delayed_accumulation(void) {
  // Constant positive input after an initial reset:
  // y[0] = 0 (reset), y[1] = y[0] + u[0] = c, y[2] = 2c, ...
  const fixed_t c = Q(3);
  TEST_ASSERT_EQUAL_INT32(0, FullWaveIntegrator_Process(&fwi, c));
  TEST_ASSERT_EQUAL_INT32(1 * c, FullWaveIntegrator_Process(&fwi, c));
  TEST_ASSERT_EQUAL_INT32(2 * c, FullWaveIntegrator_Process(&fwi, c));
  TEST_ASSERT_EQUAL_INT32(3 * c, FullWaveIntegrator_Process(&fwi, c));
}

void test_negative_input_accumulates_rectified_without_reset(void) {
  // Going from 0 to negative is not a positive-going crossing, and the
  // magnitude is what accumulates.
  const fixed_t c = Q(-2);
  TEST_ASSERT_EQUAL_INT32(0, FullWaveIntegrator_Process(&fwi, c));      // 0 + |u[-1]|=0
  TEST_ASSERT_EQUAL_INT32(Q(2), FullWaveIntegrator_Process(&fwi, c));
  TEST_ASSERT_EQUAL_INT32(Q(4), FullWaveIntegrator_Process(&fwi, c));
}

void test_negative_going_crossing_does_not_reset(void) {
  const fixed_t u[] = {Q(4), Q(4), Q(-4)};
  // y: 0 (reset), 4, 4+4=8 -- the drop to -4 does not reset
  TEST_ASSERT_EQUAL_INT32(Q(8), run(u, 3));
  // continues accumulating the delayed magnitude: 8 + |-4| = 12
  TEST_ASSERT_EQUAL_INT32(Q(12), FullWaveIntegrator_Process(&fwi, Q(-4)));
}

void test_positive_going_crossing_resets(void) {
  const fixed_t u[] = {Q(4), Q(4), Q(-4),
                       Q(-4)};
  TEST_ASSERT_EQUAL_INT32(Q(12), run(u, 4)); // 0, 4, 8, 12
  // -4 -> +1 is a positive-going crossing -> reset
  TEST_ASSERT_EQUAL_INT32(0, FullWaveIntegrator_Process(&fwi, Q(1)));
  // and integration resumes from zero using the delayed input (+1)
  TEST_ASSERT_EQUAL_INT32(Q(1), FullWaveIntegrator_Process(&fwi, Q(1)));
}

void test_crossing_from_exactly_zero_resets(void) {
  // u[n-1] <= 0 is inclusive: 0 -> positive counts as a crossing.
  const fixed_t u[] = {Q(2), Q(2), Q(0)};
  TEST_ASSERT_EQUAL_INT32(Q(4), run(u, 3)); // 0, 2, 4
  TEST_ASSERT_EQUAL_INT32(0, FullWaveIntegrator_Process(&fwi, Q(1)));
}

void test_positive_to_positive_never_resets(void) {
  FullWaveIntegrator_Process(&fwi, Q(1)); // initial reset
  for (int i = 1; i <= 50; i++) {
    fixed_t u = Q(1 + (i % 7));
    fixed_t y = FullWaveIntegrator_Process(&fwi, u);
    TEST_ASSERT_TRUE(y > 0);
  }
}

void test_sine_resets_at_each_positive_zero_crossing(void) {
  // One full period of |sin| integrates to 4A/w; the sample following the
  // positive-going crossing is exactly 0.
  const int period = 480; // 100 Hz @ 48 kHz
  const float amp = 0.005f; // keep the integral (4A*N/2pi ~= 1.53) inside +/-2
  fixed_t y = 0;
  for (int n = 0; n < 5 * period; n++) {
    fixed_t u = float_to_fixed(amp * sinf(2.0f * (float)M_PI * (n % period) / period));
    y = FullWaveIntegrator_Process(&fwi, u);
    if (n % period == 1) {
      // u[n] > 0 and u[n-1] == 0 -> reset
      TEST_ASSERT_EQUAL_INT32(0, y);
    }
    if (n % period == 0 && n > 0) {
      // Just before the reset: the full period of |u| has summed to 4A/w
      TEST_ASSERT_FLOAT_WITHIN(0.02f, 4.0f * amp * period / (2.0f * (float)M_PI),
                               fixed_to_float(y));
    }
  }
  (void)y;
}

void test_sine_output_shape(void) {
  // Within a period the integral of |A*sin(wn)| rises monotonically: 2A/w
  // at the negative-going crossing (n = period/2), 4A/w just before the
  // reset. A ramp, not a hump: that is where the harmonics come from.
  const int period = 480;
  const float amp = 0.005f; // 4A*N/2pi ~= 1.53, inside +/-2
  const float half = 2.0f * amp * period / (2.0f * (float)M_PI);
  fixed_t peak = 0, prev = 0;
  int peak_n = -1;
  for (int n = 0; n < period; n++) {
    fixed_t u = float_to_fixed(amp * sinf(2.0f * (float)M_PI * (n % period) / period));
    fixed_t y = FullWaveIntegrator_Process(&fwi, u);
    if (n > 1) TEST_ASSERT_TRUE(y >= prev); // monotonic after the reset at n = 1
    if (n == period / 2) TEST_ASSERT_FLOAT_WITHIN(0.01f, half, fixed_to_float(y));
    if (y > peak) { peak = y; peak_n = n; }
    prev = y;
  }
  TEST_ASSERT_EQUAL_INT(period - 1, peak_n);
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 2.0f * half, fixed_to_float(peak));
}

void test_accumulator_saturates_instead_of_wrapping(void) {
  // Constant DC input never produces a positive zero crossing, so the
  // accumulator grows until it saturates and then stays there.
  const fixed_t u = Q(16); // 1.0
  fixed_t y = 0, y_prev = 0;
  for (int i = 0; i < 10; i++) {
    y = FullWaveIntegrator_Process(&fwi, u);
    TEST_ASSERT_TRUE(y >= y_prev); // monotonic: no wrap-around
    y_prev = y;
  }
  TEST_ASSERT_EQUAL_INT32(INT32_MAX, y);
  // A positive crossing still resets it.
  FullWaveIntegrator_Process(&fwi, Q(-1));
  TEST_ASSERT_EQUAL_INT32(0, FullWaveIntegrator_Process(&fwi, Q(1)));
}

void test_negative_dc_accumulates_rectified_and_saturates(void) {
  const fixed_t u = Q(-16); // -1.0
  fixed_t y = 0;
  for (int i = 0; i < 10; i++) y = FullWaveIntegrator_Process(&fwi, u);
  TEST_ASSERT_EQUAL_INT32(INT32_MAX, y);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_init_zeroes_state);
  RUN_TEST(test_silence_in_silence_out);
  RUN_TEST(test_first_positive_sample_resets);
  RUN_TEST(test_output_is_delayed_accumulation);
  RUN_TEST(test_negative_input_accumulates_rectified_without_reset);
  RUN_TEST(test_negative_going_crossing_does_not_reset);
  RUN_TEST(test_positive_going_crossing_resets);
  RUN_TEST(test_crossing_from_exactly_zero_resets);
  RUN_TEST(test_positive_to_positive_never_resets);
  RUN_TEST(test_sine_resets_at_each_positive_zero_crossing);
  RUN_TEST(test_sine_output_shape);
  RUN_TEST(test_accumulator_saturates_instead_of_wrapping);
  RUN_TEST(test_negative_dc_accumulates_rectified_and_saturates);
  return UNITY_END();
}
