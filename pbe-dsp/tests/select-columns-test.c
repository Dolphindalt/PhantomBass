#include <string.h>
#include <unity.h>

#include "select-columns.h"

#define N_FRAMES 8

// Payload values: the selectors move samples without interpreting them.
#define V(n) ((fixed_t)((n) * 1000))

static SelectColumns_t sel;
static fixed_t stereo[N_FRAMES * 2];

void setUp(void) {
  // Left channel = 1, 2, 3, ...; right channel = -1, -2, -3, ...
  for (int n = 0; n < N_FRAMES; n++) {
    stereo[2 * n] = V(n + 1);
    stereo[2 * n + 1] = V(-(n + 1));
  }
}
void tearDown(void) {}

void test_init_stores_parameters(void) {
  SelectColumns_Init(&sel, 2, 1);
  TEST_ASSERT_EQUAL_UINT8(2, sel.n_channels);
  TEST_ASSERT_EQUAL_UINT8(1, sel.column);
}

void test_init_clamps_column_to_last_channel(void) {
  SelectColumns_Init(&sel, 2, 7);
  TEST_ASSERT_EQUAL_UINT8(1, sel.column);
}

void test_init_treats_zero_channels_as_mono(void) {
  SelectColumns_Init(&sel, 0, 3);
  TEST_ASSERT_EQUAL_UINT8(1, sel.n_channels);
  TEST_ASSERT_EQUAL_UINT8(0, sel.column);
}

void test_process_selects_left(void) {
  SelectColumns_Init(&sel, 2, 0);
  for (int n = 0; n < N_FRAMES; n++) {
    TEST_ASSERT_EQUAL_INT32(V(n + 1),
                            SelectColumns_Process(&sel, &stereo[2 * n]));
  }
}

void test_process_selects_right(void) {
  SelectColumns_Init(&sel, 2, 1);
  for (int n = 0; n < N_FRAMES; n++) {
    TEST_ASSERT_EQUAL_INT32(V(-(n + 1)),
                            SelectColumns_Process(&sel, &stereo[2 * n]));
  }
}

void test_process_block_stereo_to_mono(void) {
  fixed_t mono[N_FRAMES];
  SelectColumns_Init(&sel, 2, 1);
  SelectColumns_ProcessBlock(&sel, stereo, mono, N_FRAMES);
  for (int n = 0; n < N_FRAMES; n++) {
    TEST_ASSERT_EQUAL_INT32(V(-(n + 1)), mono[n]);
  }
}

void test_process_block_in_place(void) {
  SelectColumns_Init(&sel, 2, 0);
  SelectColumns_ProcessBlock(&sel, stereo, stereo, N_FRAMES);
  for (int n = 0; n < N_FRAMES; n++) {
    TEST_ASSERT_EQUAL_INT32(V(n + 1), stereo[n]);
  }
}

void test_process_block_zero_frames_is_noop(void) {
  fixed_t mono[N_FRAMES];
  memset(mono, 0x55, sizeof(mono));
  SelectColumns_Init(&sel, 2, 0);
  SelectColumns_ProcessBlock(&sel, stereo, mono, 0);
  for (int n = 0; n < N_FRAMES; n++) {
    TEST_ASSERT_EQUAL_INT32(0x55555555, mono[n]);
  }
}

void test_process_block_multichannel(void) {
  // 4 channels, pick column 2.
  fixed_t quad[N_FRAMES * 4], out[N_FRAMES];
  for (int n = 0; n < N_FRAMES; n++) {
    for (int c = 0; c < 4; c++) quad[4 * n + c] = V(10 * n + c);
  }
  SelectColumns_Init(&sel, 4, 2);
  SelectColumns_ProcessBlock(&sel, quad, out, N_FRAMES);
  for (int n = 0; n < N_FRAMES; n++) {
    TEST_ASSERT_EQUAL_INT32(V(10 * n + 2), out[n]);
  }
}

void test_mono_passthrough(void) {
  fixed_t in[N_FRAMES], out[N_FRAMES];
  for (int n = 0; n < N_FRAMES; n++) in[n] = float_to_fixed(0.1f * n);
  SelectColumns_Init(&sel, 1, 0);
  SelectColumns_ProcessBlock(&sel, in, out, N_FRAMES);
  TEST_ASSERT_EQUAL_INT32_ARRAY(in, out, N_FRAMES);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_init_stores_parameters);
  RUN_TEST(test_init_clamps_column_to_last_channel);
  RUN_TEST(test_init_treats_zero_channels_as_mono);
  RUN_TEST(test_process_selects_left);
  RUN_TEST(test_process_selects_right);
  RUN_TEST(test_process_block_stereo_to_mono);
  RUN_TEST(test_process_block_in_place);
  RUN_TEST(test_process_block_zero_frames_is_noop);
  RUN_TEST(test_process_block_multichannel);
  RUN_TEST(test_mono_passthrough);
  return UNITY_END();
}
