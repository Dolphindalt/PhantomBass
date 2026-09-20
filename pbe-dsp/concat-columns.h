#pragma once

#include <stddef.h>
#include <stdint.h>

#include "fixed-math.h"

// Column concatenator, the inverse of select-columns: MATLAB/Simulink's
// "Matrix Concatenate" block in horizontal mode. Each input is a mono
// stream (one column); the output is an interleaved frame stream with one
// column per input. Two mono inputs (left, right) become stereo.
typedef struct {
  uint8_t n_channels; // columns per output frame (2 for stereo)
} ConcatColumns_t;

// n_channels of 0 is treated as 1.
void ConcatColumns_Init(ConcatColumns_t *cat, uint8_t n_channels);

// Build one interleaved frame from one sample per channel.
// samples holds cat->n_channels values, frame receives cat->n_channels values.
void ConcatColumns_Process(const ConcatColumns_t *cat, const fixed_t *samples,
                           fixed_t *frame);

// Interleave n_frames samples from each of cat->n_channels mono inputs.
// inputs[c] points to n_frames samples; output receives
// n_frames * cat->n_channels samples. output must not alias any input.
void ConcatColumns_ProcessBlock(const ConcatColumns_t *cat,
                                const fixed_t *const *inputs, fixed_t *output,
                                size_t n_frames);
