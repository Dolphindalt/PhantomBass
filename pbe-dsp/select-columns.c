#include "select-columns.h"

void SelectColumns_Init(SelectColumns_t *sel, uint8_t n_channels,
                        uint8_t column) {
  if (n_channels == 0) n_channels = 1;
  if (column >= n_channels) column = n_channels - 1;
  sel->n_channels = n_channels;
  sel->column = column;
}

fixed_t SelectColumns_Process(const SelectColumns_t *sel,
                              const fixed_t *frame) {
  return frame[sel->column];
}

void SelectColumns_ProcessBlock(const SelectColumns_t *sel,
                                const fixed_t *input, fixed_t *output,
                                size_t n_frames) {
  // Walking forward is safe when output aliases input: output[n] sits at or
  // before input[n * n_channels + column], which has already been read.
  for (size_t n = 0; n < n_frames; n++) {
    output[n] = input[n * sel->n_channels + sel->column];
  }
}
