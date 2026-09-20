#include "concat-columns.h"

void ConcatColumns_Init(ConcatColumns_t *cat, uint8_t n_channels) {
  cat->n_channels = n_channels == 0 ? 1 : n_channels;
}

void ConcatColumns_Process(const ConcatColumns_t *cat, const fixed_t *samples,
                           fixed_t *frame) {
  for (uint8_t c = 0; c < cat->n_channels; c++) frame[c] = samples[c];
}

void ConcatColumns_ProcessBlock(const ConcatColumns_t *cat,
                                const fixed_t *const *inputs, fixed_t *output,
                                size_t n_frames) {
  for (size_t n = 0; n < n_frames; n++) {
    for (uint8_t c = 0; c < cat->n_channels; c++) {
      output[n * cat->n_channels + c] = inputs[c][n];
    }
  }
}
