#pragma once

#include <stddef.h>
#include <stdint.h>

#include "fixed-math.h"

// Column selector. Interleaved multi-channel audio is treated as a matrix
// with one row per frame and one column per channel; the selector keeps a
// single column, which turns a stereo (2-column) stream into mono.
typedef struct {
  uint8_t n_channels; // columns per input frame (2 for stereo)
  uint8_t column;     // zero-based column to keep (0 = left, 1 = right)
} SelectColumns_t;

// column is clamped to n_channels - 1; n_channels of 0 is treated as 1.
void SelectColumns_Init(SelectColumns_t *sel, uint8_t n_channels,
                        uint8_t column);

// Select from one interleaved frame of sel->n_channels samples.
fixed_t SelectColumns_Process(const SelectColumns_t *sel, const fixed_t *frame);

// Select from n_frames interleaved frames. input holds
// n_frames * sel->n_channels samples, output receives n_frames samples.
// output may alias input (in-place mono conversion).
void SelectColumns_ProcessBlock(const SelectColumns_t *sel,
                                const fixed_t *input, fixed_t *output,
                                size_t n_frames);
