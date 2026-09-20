#include "pbe-dsp.h"

#include <math.h>

#define PBE_CHANNELS 2
#define PBE_DECIMATION_SHIFT 2
static_assert((1 << PBE_DECIMATION_SHIFT) == PBE_DECIMATION,
              "PBE_DECIMATION must be a power of two");

void PbeDsp_Init(PbeDsp_t *pbe, float speaker_cutoff_hz, float bandpass_high_hz,
                 float gain_db, float sample_rate_hz) {
  float decimated_rate_hz = sample_rate_hz / PBE_DECIMATION;

  SelectColumns_Init(&pbe->select_left, PBE_CHANNELS, 0);
  SelectColumns_Init(&pbe->select_right, PBE_CHANNELS, 1);
  CrossOverFilter_Init(&pbe->crossover_left, speaker_cutoff_hz, sample_rate_hz);
  CrossOverFilter_Init(&pbe->crossover_right, speaker_cutoff_hz, sample_rate_hz);
  ConcatColumns_Init(&pbe->concat, PBE_CHANNELS);

  pbe->phase = 0;
  pbe->decimate_acc = 0;
  FullWaveIntegrator_Init(&pbe->integrator);
  BandPassFilter_Init(&pbe->bandpass, speaker_cutoff_hz, bandpass_high_hz,
                      decimated_rate_hz);
  pbe->enhance_prev = 0;
  pbe->enhance_cur = 0;
  pbe->smooth_alpha = float_to_fixed(
      1.0f - expf(-2.0f * (float)M_PI * PBE_SMOOTHER_CUTOFF_HZ / sample_rate_hz));
  pbe->smooth_state = 0;

  pbe->speaker_cutoff_hz = speaker_cutoff_hz;
  pbe->sample_rate_hz = sample_rate_hz;
  PbeDsp_SetGain(pbe, gain_db);
}

void PbeDsp_SetGain(PbeDsp_t *pbe, float gain_db) {
  pbe->gain_db = gain_db;
  if (gain_db <= PBE_GAIN_OFF_DB) {
    BandPassFilter_SetGain(&pbe->bandpass, 0.0f);
    return;
  }
  // The integrator of a sine of amplitude A at angular frequency w ramps to
  // 4A/w per period, i.e. 4A*fs_d/(2*pi*f) per decimated sample. Scaling by
  // pi*fc/(2*fs_d) makes a tone at the speaker cutoff come out at unity, so
  // gain_db reads sensibly. 2^PBE_INTEGRATOR_SHIFT undoes the integrator
  // input pre-scale.
  float decimated_rate_hz = pbe->sample_rate_hz / PBE_DECIMATION;
  float normalize = (float)M_PI * pbe->speaker_cutoff_hz / (2.0f * decimated_rate_hz) *
                    (float)(1 << PBE_INTEGRATOR_SHIFT);
  BandPassFilter_SetGain(&pbe->bandpass,
                         powf(10.0f, gain_db / 20.0f) * normalize);
}

void PbeDsp_SetBandPassHighCutoff(PbeDsp_t *pbe, float bandpass_high_hz) {
  BandPassFilter_SetHighCutoff(&pbe->bandpass, bandpass_high_hz);
}

void PbeDsp_SetSpeakerCutoff(PbeDsp_t *pbe, float speaker_cutoff_hz) {
  pbe->speaker_cutoff_hz = speaker_cutoff_hz;
  CrossOverFilter_SetCutoff(&pbe->crossover_left, speaker_cutoff_hz,
                            pbe->sample_rate_hz);
  CrossOverFilter_SetCutoff(&pbe->crossover_right, speaker_cutoff_hz,
                            pbe->sample_rate_hz);
  BandPassFilter_SetLowCutoff(&pbe->bandpass, speaker_cutoff_hz);
  PbeDsp_SetGain(pbe, pbe->gain_db); // normalization depends on the cutoff
}

static inline fixed_t clip_full_scale(int64_t x) {
  if (x > PBE_FULL_SCALE) return PBE_FULL_SCALE;
  if (x < -PBE_FULL_SCALE) return -PBE_FULL_SCALE;
  return (fixed_t)x;
}

void PbeDsp_ProcessFrame(PbeDsp_t *pbe, const fixed_t frame_in[2],
                         fixed_t frame_out[2]) {
  // 1. Crossover, per channel.
  fixed_t lp_left, hp_left, lp_right, hp_right;
  CrossOverFilter_Process(&pbe->crossover_left,
                          SelectColumns_Process(&pbe->select_left, frame_in),
                          &lp_left, &hp_left);
  CrossOverFilter_Process(&pbe->crossover_right,
                          SelectColumns_Process(&pbe->select_right, frame_in),
                          &lp_right, &hp_right);

  // 3. Lowpass band to mono (average: L+R at full scale would overflow),
  //    box-car decimation: accumulate lp_mono / PBE_DECIMATION.
  fixed_t lp_mono = (fixed_t)(((int64_t)lp_left + lp_right) >> 1);
  pbe->decimate_acc += lp_mono >> PBE_DECIMATION_SHIFT;

  // Up-sample the enhancement: linear interpolation between the two most
  // recent decimated outputs, then a one-pole smoother for the images.
  fixed_t step = (pbe->enhance_cur - pbe->enhance_prev) >> PBE_DECIMATION_SHIFT;
  fixed_t interp = pbe->enhance_prev + step * (pbe->phase + 1);
  pbe->smooth_state += (fixed_t)fixed_round_shift64(
      (int64_t)pbe->smooth_alpha * ((int64_t)interp - pbe->smooth_state),
      FIXED_FRAC_BITS);
  fixed_t y_g = pbe->smooth_state;

  // 4-6. Once per PBE_DECIMATION frames: harmonic generation, band limiting
  //      and gain (G and the integrator normalization live in the bandpass).
  if (pbe->phase == PBE_DECIMATION - 1) {
    fixed_t u = (pbe->decimate_acc + (1 << (PBE_INTEGRATOR_SHIFT - 1))) >>
                PBE_INTEGRATOR_SHIFT;
    pbe->decimate_acc = 0;
    fixed_t y = FullWaveIntegrator_Process(&pbe->integrator, u);
    fixed_t y_bp = BandPassFilter_Process(&pbe->bandpass, y);
    pbe->enhance_prev = pbe->enhance_cur;
    // Keep within +/- full scale so the interpolation difference cannot
    // overflow; anything larger would be clipped at the output anyway.
    pbe->enhance_cur = fixed_clamp(y_bp, -PBE_FULL_SCALE, PBE_FULL_SCALE);
    pbe->phase = 0;
  } else {
    pbe->phase++;
  }

  // 7-8. Mix into both highpass channels, clip, and re-interleave.
  fixed_t samples[PBE_CHANNELS] = {clip_full_scale((int64_t)hp_left + y_g),
                                   clip_full_scale((int64_t)hp_right + y_g)};
  ConcatColumns_Process(&pbe->concat, samples, frame_out);
}

void PbeDsp_ProcessBlock(PbeDsp_t *pbe, const fixed_t *input, fixed_t *output,
                         size_t n_frames) {
  for (size_t n = 0; n < n_frames; n++) {
    PbeDsp_ProcessFrame(pbe, &input[n * PBE_CHANNELS],
                        &output[n * PBE_CHANNELS]);
  }
}
