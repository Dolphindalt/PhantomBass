#include <math.h>
#include <string.h>
#include <unity.h>

#include "pbe-dsp.h"

#define FS 48000.0f
#define SPEAKER_CUTOFF 60.0f
#define BP_HIGH 400.0f
#define GAIN_DB 0.0f
#define AMPLITUDE 0.25f
#define N_FRAMES 48000

static PbeDsp_t pbe;

void setUp(void) { PbeDsp_Init(&pbe, SPEAKER_CUTOFF, BP_HIGH, GAIN_DB, FS); }
void tearDown(void) {}

static fixed_t tone(float freq_hz, int n, float amp) {
  return float_to_fixed(amp * sinf(2.0f * (float)M_PI * freq_hz * n / FS));
}

typedef struct {
  float peak[2];
  double mean[2];
  double rms[2];
} Stats_t;

// Run a stereo tone (independent amplitudes per channel) through the
// processor and gather steady-state statistics of each output channel.
static Stats_t run_tone(float freq_hz, float amp_left, float amp_right) {
  Stats_t st;
  memset(&st, 0, sizeof(st));
  int count = 0;
  for (int n = 0; n < N_FRAMES; n++) {
    fixed_t in[2] = {tone(freq_hz, n, amp_left), tone(freq_hz, n, amp_right)};
    fixed_t out[2];
    PbeDsp_ProcessFrame(&pbe, in, out);
    if (n < N_FRAMES / 2) continue;
    for (int c = 0; c < 2; c++) {
      float v = fixed_to_float(out[c]);
      st.peak[c] = fmaxf(st.peak[c], fabsf(v));
      st.mean[c] += v;
      st.rms[c] += (double)v * v;
    }
    count++;
  }
  for (int c = 0; c < 2; c++) {
    st.mean[c] /= count;
    st.rms[c] = sqrt(st.rms[c] / count);
  }
  return st;
}

static float to_db(float peak, float ref) { return 20.0f * log10f(peak / ref); }

void test_init_configures_stages(void) {
  TEST_ASSERT_EQUAL_UINT8(0, pbe.select_left.column);
  TEST_ASSERT_EQUAL_UINT8(1, pbe.select_right.column);
  TEST_ASSERT_EQUAL_UINT8(2, pbe.concat.n_channels);
  // Bass path runs decimated; 0 dB gain = integrator normalization.
  TEST_ASSERT_EQUAL_FLOAT(FS / PBE_DECIMATION, pbe.bandpass.sample_rate_hz);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f,
                           (float)M_PI * SPEAKER_CUTOFF / (2.0f * FS / PBE_DECIMATION) *
                               (1 << PBE_INTEGRATOR_SHIFT),
                           pbe.bandpass.gain);
  TEST_ASSERT_EQUAL_UINT8(0, pbe.phase);
  // Both crossovers share the same tuning.
  TEST_ASSERT_EQUAL_INT32(pbe.crossover_left.a0, pbe.crossover_right.a0);
  TEST_ASSERT_EQUAL_INT32(pbe.crossover_left.b1, pbe.crossover_right.b1);
}

void test_silence_in_silence_out(void) {
  fixed_t in[2] = {0, 0}, out[2] = {1, 1};
  for (int n = 0; n < 100; n++) {
    PbeDsp_ProcessFrame(&pbe, in, out);
    TEST_ASSERT_EQUAL_INT32(0, out[0]);
    TEST_ASSERT_EQUAL_INT32(0, out[1]);
  }
}

void test_high_frequencies_pass_through_per_channel(void) {
  // Well above the crossover: output == input, channels stay independent.
  Stats_t st = run_tone(2000.0f, AMPLITUDE, 0.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, to_db(st.peak[0], AMPLITUDE));
  TEST_ASSERT_TRUE(st.peak[1] < 1e-3f);

  setUp();
  st = run_tone(2000.0f, 0.0f, AMPLITUDE);
  TEST_ASSERT_TRUE(st.peak[0] < 1e-3f);
  TEST_ASSERT_FLOAT_WITHIN(0.1f, 0.0f, to_db(st.peak[1], AMPLITUDE));
}

void test_bass_below_cutoff_is_removed(void) {
  // A 20 Hz tone is below the speaker cutoff. With enhancement off the
  // output is just the crossover highpass: ~ -18 dB, 1.5 octaves down at
  // 12 dB/oct from -6 dB at fc.
  PbeDsp_SetGain(&pbe, PBE_GAIN_OFF_DB);
  Stats_t st = run_tone(20.0f, AMPLITUDE, AMPLITUDE);
  TEST_ASSERT_TRUE(to_db(st.peak[0], AMPLITUDE) < -15.0f);
  TEST_ASSERT_TRUE(to_db(st.peak[1], AMPLITUDE) < -15.0f);
}

void test_zero_gain_output_equals_crossover_highpass(void) {
  PbeDsp_SetGain(&pbe, PBE_GAIN_OFF_DB);
  CrossOverFilter_t ref_l, ref_r;
  CrossOverFilter_Init(&ref_l, SPEAKER_CUTOFF, FS);
  CrossOverFilter_Init(&ref_r, SPEAKER_CUTOFF, FS);
  for (int n = 0; n < 4800; n++) {
    fixed_t in[2] = {tone(40.0f, n, AMPLITUDE), tone(3000.0f, n, AMPLITUDE)};
    fixed_t out[2], lo, hi_l, hi_r;
    PbeDsp_ProcessFrame(&pbe, in, out);
    CrossOverFilter_Process(&ref_l, in[0], &lo, &hi_l);
    CrossOverFilter_Process(&ref_r, in[1], &lo, &hi_r);
    TEST_ASSERT_EQUAL_INT32(hi_l, out[0]);
    TEST_ASSERT_EQUAL_INT32(hi_r, out[1]);
  }
}

void test_enhancement_adds_harmonics_equally_to_both_channels(void) {
  // Bass-only input: with gain on, the output carries generated content
  // that is absent with gain off, and it is identical in L and R because
  // it is derived from the mono lowpass sum.
  PbeDsp_SetGain(&pbe, PBE_GAIN_OFF_DB);
  Stats_t off = run_tone(40.0f, AMPLITUDE, AMPLITUDE);
  setUp();
  Stats_t on = run_tone(40.0f, AMPLITUDE, AMPLITUDE);

  // With the 0 dB reference (unity at fc) the 40 Hz enhancement sits ~4 dB
  // above the crossover's residual highpass leakage.
  TEST_ASSERT_TRUE(on.rms[0] > 1.4 * off.rms[0]);
  TEST_ASSERT_TRUE(on.rms[1] > 1.4 * off.rms[1]);

  // The added signal is the same in both channels: difference L-R is
  // unchanged by enabling the enhancement (input is symmetric so it's ~0).
  setUp();
  double diff = 0.0;
  for (int n = 0; n < N_FRAMES; n++) {
    fixed_t in[2] = {tone(40.0f, n, AMPLITUDE), tone(40.0f, n, AMPLITUDE)};
    fixed_t out[2];
    PbeDsp_ProcessFrame(&pbe, in, out);
    diff = fmax(diff, fabs(fixed_to_float(out[0] - out[1])));
  }
  TEST_ASSERT_TRUE(diff < 1e-3);
}

void test_enhancement_output_is_dc_free(void) {
  // The integrator output has a large DC component; the bandpass must
  // remove it before it reaches the speakers.
  Stats_t st = run_tone(40.0f, AMPLITUDE, AMPLITUDE);
  TEST_ASSERT_TRUE(fabs(st.mean[0]) < 1e-3);
  TEST_ASSERT_TRUE(fabs(st.mean[1]) < 1e-3);
}

// RMS of the enhancement alone: the sample-by-sample difference between an
// instance at gain_db and one with the enhancement off, fed the same tone.
// (The highpass path is identical in both, so only the added signal remains.)
static double enhancement_rms(float freq_hz, float gain_db) {
  PbeDsp_t on, off;
  PbeDsp_Init(&on, SPEAKER_CUTOFF, BP_HIGH, gain_db, FS);
  PbeDsp_Init(&off, SPEAKER_CUTOFF, BP_HIGH, PBE_GAIN_OFF_DB, FS);
  double sum = 0.0;
  int count = 0;
  for (int n = 0; n < N_FRAMES; n++) {
    fixed_t in[2] = {tone(freq_hz, n, AMPLITUDE), tone(freq_hz, n, AMPLITUDE)};
    fixed_t out_on[2], out_off[2];
    PbeDsp_ProcessFrame(&on, in, out_on);
    PbeDsp_ProcessFrame(&off, in, out_off);
    if (n < N_FRAMES / 2) continue;
    double d = fixed_to_float(out_on[0]) - fixed_to_float(out_off[0]);
    sum += d * d;
    count++;
  }
  return sqrt(sum / count);
}

void test_gain_scales_enhancement(void) {
  // The gain sits after the nonlinearity, so +6 dB doubles the added signal.
  double e1 = enhancement_rms(40.0f, GAIN_DB);
  double e2 = enhancement_rms(40.0f, GAIN_DB + 6.02f); // x2
  TEST_ASSERT_TRUE(e1 > 1e-3);
  TEST_ASSERT_FLOAT_WITHIN((float)(0.05 * e2), (float)(2.0 * e1), (float)e2);
}

void test_gain_reference_is_unity_at_speaker_cutoff(void) {
  // A tone at the speaker cutoff, 0 dB: the integrator output scaled by the
  // normalization (which lives in the bandpass gain) should peak at the mono
  // lowpass amplitude, i.e. the integrator's 1/f gain is normalized out.
  float peak_y = 0.0f, peak_lp = 0.0f;
  for (int n = 0; n < N_FRAMES; n++) {
    fixed_t in[2] = {tone(SPEAKER_CUTOFF, n, AMPLITUDE),
                     tone(SPEAKER_CUTOFF, n, AMPLITUDE)};
    fixed_t out[2];
    PbeDsp_ProcessFrame(&pbe, in, out);
    if (n < N_FRAMES / 2) continue;
    float y = fixed_to_float(pbe.integrator.y_prev) * pbe.bandpass.gain;
    float lp = 0.5f * (fixed_to_float(pbe.crossover_left.lp2) +
                       fixed_to_float(pbe.crossover_right.lp2));
    peak_y = fmaxf(peak_y, y);
    peak_lp = fmaxf(peak_lp, lp);
  }
  TEST_ASSERT_FLOAT_WITHIN(0.1f * peak_lp, peak_lp, peak_y);
}

void test_enhancement_is_continuous_across_decimation(void) {
  // The up-sampled enhancement must not step at decimation boundaries: with
  // a bass-only input the output sample-to-sample difference stays small.
  fixed_t prev = 0;
  float max_step = 0.0f;
  for (int n = 0; n < N_FRAMES; n++) {
    fixed_t in[2] = {tone(40.0f, n, AMPLITUDE), tone(40.0f, n, AMPLITUDE)};
    fixed_t out[2];
    PbeDsp_ProcessFrame(&pbe, in, out);
    if (n >= N_FRAMES / 2) max_step = fmaxf(max_step, fabsf(fixed_to_float(out[0] - prev)));
    prev = out[0];
  }
  // A 400 Hz component of amplitude a moves at most 2*pi*400/48000*a ~= 0.05a
  // per sample; anything much larger is a step.
  TEST_ASSERT_TRUE(max_step < 0.05f * AMPLITUDE);
}

void test_output_is_clipped_to_full_scale(void) {
  // Overdrive: full-scale bass in both channels at high gain.
  PbeDsp_SetGain(&pbe, 24.0f);
  fixed_t max = 0, min = 0;
  for (int n = 0; n < N_FRAMES; n++) {
    fixed_t in[2] = {tone(40.0f, n, 1.0f), tone(40.0f, n, 1.0f)};
    fixed_t out[2];
    PbeDsp_ProcessFrame(&pbe, in, out);
    for (int c = 0; c < 2; c++) {
      if (out[c] > max) max = out[c];
      if (out[c] < min) min = out[c];
    }
  }
  TEST_ASSERT_EQUAL_INT32(PBE_FULL_SCALE, max);  // actually clipped
  TEST_ASSERT_EQUAL_INT32(-PBE_FULL_SCALE, min);
}

void test_set_bandpass_high_cutoff_retunes(void) {
  int32_t before = pbe.bandpass.lp.b0;
  PbeDsp_SetBandPassHighCutoff(&pbe, 2.0f * BP_HIGH);
  TEST_ASSERT_NOT_EQUAL(before, pbe.bandpass.lp.b0);
  // Lower cutoff (speaker cutoff) untouched.
  BandPassFilter_t ref;
  BandPassFilter_Init(&ref, SPEAKER_CUTOFF, 2.0f * BP_HIGH, FS / PBE_DECIMATION);
  BandPassFilter_SetGain(&ref, pbe.bandpass.gain);
  TEST_ASSERT_EQUAL_INT32(ref.hp.b0, pbe.bandpass.hp.b0);
  TEST_ASSERT_EQUAL_INT32(ref.lp.b0, pbe.bandpass.lp.b0);
}

void test_set_speaker_cutoff_matches_fresh_init(void) {
  PbeDsp_t ref;
  PbeDsp_Init(&ref, 2.0f * SPEAKER_CUTOFF, BP_HIGH, GAIN_DB, FS);
  PbeDsp_SetSpeakerCutoff(&pbe, 2.0f * SPEAKER_CUTOFF);
  TEST_ASSERT_EQUAL_FLOAT(2.0f * SPEAKER_CUTOFF, pbe.speaker_cutoff_hz);
  TEST_ASSERT_EQUAL_INT32(ref.crossover_left.a0, pbe.crossover_left.a0);
  TEST_ASSERT_EQUAL_INT32(ref.crossover_right.a0, pbe.crossover_right.a0);
  TEST_ASSERT_EQUAL_INT32(ref.bandpass.hp.b0, pbe.bandpass.hp.b0);
  TEST_ASSERT_EQUAL_INT32(ref.bandpass.hp.a1, pbe.bandpass.hp.a1);
  // Gain reference re-derived for the new cutoff (bandpass gain folds it in).
  TEST_ASSERT_EQUAL_INT32(ref.bandpass.lp.b0, pbe.bandpass.lp.b0);
  TEST_ASSERT_EQUAL_FLOAT(ref.bandpass.gain, pbe.bandpass.gain);
  // Muted stays muted through a cutoff change.
  PbeDsp_SetGain(&pbe, PBE_GAIN_OFF_DB);
  PbeDsp_SetSpeakerCutoff(&pbe, SPEAKER_CUTOFF);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, pbe.bandpass.gain);
}

void test_set_speaker_cutoff_is_glitch_free(void) {
  // Retune mid-stream on a bass-only tone; the output must stay continuous
  // (no state reset) and bounded.
  fixed_t prev = 0;
  float max_step = 0.0f;
  for (int n = 0; n < N_FRAMES; n++) {
    if (n == N_FRAMES / 2) PbeDsp_SetSpeakerCutoff(&pbe, 2.0f * SPEAKER_CUTOFF);
    fixed_t in[2] = {tone(40.0f, n, AMPLITUDE), tone(40.0f, n, AMPLITUDE)};
    fixed_t out[2];
    PbeDsp_ProcessFrame(&pbe, in, out);
    if (n >= N_FRAMES / 4) max_step = fmaxf(max_step, fabsf(fixed_to_float(out[0] - prev)));
    prev = out[0];
  }
  TEST_ASSERT_TRUE(max_step < 0.05f * AMPLITUDE);
  // And the new cutoff is in effect: bass below the (now higher) cutoff is
  // attenuated harder than before.
  PbeDsp_SetGain(&pbe, PBE_GAIN_OFF_DB);
  Stats_t high_fc = run_tone(40.0f, AMPLITUDE, AMPLITUDE);
  setUp();
  PbeDsp_SetGain(&pbe, PBE_GAIN_OFF_DB);
  Stats_t low_fc = run_tone(40.0f, AMPLITUDE, AMPLITUDE);
  TEST_ASSERT_TRUE(high_fc.peak[0] < low_fc.peak[0]);
}

void test_block_matches_frame_by_frame(void) {
  enum { N = 1024 };
  static fixed_t in[2 * N], out_block[2 * N], out_frames[2 * N];
  for (int n = 0; n < N; n++) {
    in[2 * n] = tone(45.0f, n, AMPLITUDE) + tone(1500.0f, n, 0.1f);
    in[2 * n + 1] = tone(45.0f, n, 0.5f * AMPLITUDE) - tone(900.0f, n, 0.1f);
  }
  PbeDsp_ProcessBlock(&pbe, in, out_block, N);
  setUp();
  for (int n = 0; n < N; n++) PbeDsp_ProcessFrame(&pbe, &in[2 * n], &out_frames[2 * n]);
  TEST_ASSERT_EQUAL_INT32_ARRAY(out_frames, out_block, 2 * N);
}

void test_block_in_place(void) {
  enum { N = 512 };
  static fixed_t buf[2 * N], ref[2 * N];
  for (int n = 0; n < N; n++) {
    buf[2 * n] = tone(50.0f, n, AMPLITUDE);
    buf[2 * n + 1] = tone(700.0f, n, AMPLITUDE);
  }
  PbeDsp_ProcessBlock(&pbe, buf, ref, N);
  setUp();
  PbeDsp_ProcessBlock(&pbe, buf, buf, N);
  TEST_ASSERT_EQUAL_INT32_ARRAY(ref, buf, 2 * N);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_init_configures_stages);
  RUN_TEST(test_silence_in_silence_out);
  RUN_TEST(test_high_frequencies_pass_through_per_channel);
  RUN_TEST(test_bass_below_cutoff_is_removed);
  RUN_TEST(test_zero_gain_output_equals_crossover_highpass);
  RUN_TEST(test_enhancement_adds_harmonics_equally_to_both_channels);
  RUN_TEST(test_enhancement_output_is_dc_free);
  RUN_TEST(test_gain_scales_enhancement);
  RUN_TEST(test_gain_reference_is_unity_at_speaker_cutoff);
  RUN_TEST(test_enhancement_is_continuous_across_decimation);
  RUN_TEST(test_output_is_clipped_to_full_scale);
  RUN_TEST(test_set_bandpass_high_cutoff_retunes);
  RUN_TEST(test_set_speaker_cutoff_matches_fresh_init);
  RUN_TEST(test_set_speaker_cutoff_is_glitch_free);
  RUN_TEST(test_block_matches_frame_by_frame);
  RUN_TEST(test_block_in_place);
  return UNITY_END();
}
