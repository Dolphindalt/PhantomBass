#include <string.h>
#include <unity.h>

#include "concat-columns.h"
#include "select-columns.h"

#define N_FRAMES 8

// Payload values: the selectors move samples without interpreting them.
#define V(n) ((fixed_t)((n) * 1000))

static ConcatColumns_t cat;
static fixed_t left[N_FRAMES], right[N_FRAMES];

void setUp(void) {
  // Left = 1, 2, 3, ...; right = -1, -2, -3, ...
  for (int n = 0; n < N_FRAMES; n++) {
    left[n] = V(n + 1);
    right[n] = V(-(n + 1));
  }
}
void tearDown(void) {}

void test_init_stores_channels(void) {
  ConcatColumns_Init(&cat, 2);
  TEST_ASSERT_EQUAL_UINT8(2, cat.n_channels);
}

void test_init_treats_zero_channels_as_mono(void) {
  ConcatColumns_Init(&cat, 0);
  TEST_ASSERT_EQUAL_UINT8(1, cat.n_channels);
}

void test_process_builds_one_stereo_frame(void) {
  fixed_t samples[2] = {left[3], right[3]}, frame[2];
  ConcatColumns_Init(&cat, 2);
  ConcatColumns_Process(&cat, samples, frame);
  TEST_ASSERT_EQUAL_INT32(left[3], frame[0]);
  TEST_ASSERT_EQUAL_INT32(right[3], frame[1]);
}

void test_process_block_mono_pair_to_stereo(void) {
  const fixed_t *inputs[2] = {left, right};
  fixed_t stereo[N_FRAMES * 2];
  ConcatColumns_Init(&cat, 2);
  ConcatColumns_ProcessBlock(&cat, inputs, stereo, N_FRAMES);
  for (int n = 0; n < N_FRAMES; n++) {
    TEST_ASSERT_EQUAL_INT32(left[n], stereo[2 * n]);
    TEST_ASSERT_EQUAL_INT32(right[n], stereo[2 * n + 1]);
  }
}

void test_process_block_zero_frames_is_noop(void) {
  const fixed_t *inputs[2] = {left, right};
  fixed_t stereo[N_FRAMES * 2];
  memset(stereo, 0x55, sizeof(stereo));
  ConcatColumns_Init(&cat, 2);
  ConcatColumns_ProcessBlock(&cat, inputs, stereo, 0);
  for (int n = 0; n < N_FRAMES * 2; n++) TEST_ASSERT_EQUAL_INT32(0x55555555, stereo[n]);
}

void test_select_columns_inverts_concat(void) {
  // concat(L, R) -> select column 0 == L, select column 1 == R
  const fixed_t *inputs[2] = {left, right};
  fixed_t stereo[N_FRAMES * 2], mono[N_FRAMES];
  SelectColumns_t sel;
  ConcatColumns_Init(&cat, 2);
  ConcatColumns_ProcessBlock(&cat, inputs, stereo, N_FRAMES);

  SelectColumns_Init(&sel, 2, 0);
  SelectColumns_ProcessBlock(&sel, stereo, mono, N_FRAMES);
  TEST_ASSERT_EQUAL_INT32_ARRAY(left, mono, N_FRAMES);

  SelectColumns_Init(&sel, 2, 1);
  SelectColumns_ProcessBlock(&sel, stereo, mono, N_FRAMES);
  TEST_ASSERT_EQUAL_INT32_ARRAY(right, mono, N_FRAMES);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_init_stores_channels);
  RUN_TEST(test_init_treats_zero_channels_as_mono);
  RUN_TEST(test_process_builds_one_stereo_frame);
  RUN_TEST(test_process_block_mono_pair_to_stereo);
  RUN_TEST(test_process_block_zero_frames_is_noop);
  RUN_TEST(test_select_columns_inverts_concat);
  return UNITY_END();
}
