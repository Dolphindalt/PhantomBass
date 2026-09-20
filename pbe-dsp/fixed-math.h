#pragma once

#include <assert.h>
#include <math.h>
#include <stdint.h>

// Signal sample format: Q2.30 in an int32_t, matching the picoamp firmware's
// internal DSP format (fpformat2 = 30): full scale (1.0) is 1 << 30 and the
// remaining bit is 6 dB of headroom. Filter coefficients that need more
// integer range use their own Q formats (see band-pass.h).
typedef int32_t fixed_t;

#ifndef FIXED_FRAC_BITS
#define FIXED_FRAC_BITS 30
#endif

#ifndef FIXED_SCALE
#define FIXED_SCALE (1 << FIXED_FRAC_BITS)
#endif

#define FIXED_INT_MAX ((1 << (32 - FIXED_FRAC_BITS - 1)) - 1)
#define FIXED_INT_MIN (-(1 << (32 - FIXED_FRAC_BITS - 1)))

// MACRO FOR LOCAL VARIABLES (allows inline static statements)
#define INIT_FIXED(int_part, frac_part)                                        \
  (__extension__({                                                             \
    static_assert(                                                             \
        (int_part) <= FIXED_INT_MAX && (int_part) >= FIXED_INT_MIN,            \
        "Fixed-point initialization overflow: Integer part out of range!");    \
    static_assert((frac_part) >= 0 && (frac_part) < FIXED_SCALE,               \
                  "Fixed-point initialization overflow: Fractional part must " \
                  "be between 0 and (FIXED_SCALE-1)!");                        \
    (fixed_t)(((int_part) << FIXED_FRAC_BITS) + (frac_part));                  \
  }))

// MACRO FOR GLOBAL / STATIC CONSTANTS (no code blocks allowed)
// This relies on the compiler catching out-of-range constant expressions
// natively.
#define INIT_FIXED_GLOBAL(int_part, frac_part)                                 \
  ((fixed_t)(((int_part) << FIXED_FRAC_BITS) + (frac_part)))

static inline fixed_t int_to_fixed(int32_t i) {
  return (fixed_t)(i << FIXED_FRAC_BITS);
}

static inline fixed_t fixed_add(fixed_t a, fixed_t b) { return a + b; }

static inline fixed_t fixed_sub(fixed_t a, fixed_t b) { return a - b; }

// Saturating add: clamps to the fixed_t range instead of wrapping.
static inline fixed_t fixed_add_sat(fixed_t a, fixed_t b) {
  fixed_t r;
  if (__builtin_add_overflow(a, b, &r)) return b < 0 ? INT32_MIN : INT32_MAX;
  return r;
}

static inline fixed_t fixed_clamp(fixed_t x, fixed_t lo, fixed_t hi) {
  return x < lo ? lo : (x > hi ? hi : x);
}

// Narrow a 64-bit intermediate to fixed_t with saturation.
static inline fixed_t fixed_sat64(int64_t x) {
  if (x > INT32_MAX) return INT32_MAX;
  if (x < INT32_MIN) return INT32_MIN;
  return (fixed_t)x;
}

// Round-to-nearest arithmetic shift of a 64-bit accumulator.
static inline int64_t fixed_round_shift64(int64_t x, unsigned bits) {
  return (x + ((int64_t)1 << (bits - 1))) >> bits;
}

static inline fixed_t fixed_mul(fixed_t a, fixed_t b) {
  // Round to nearest: plain truncation biases toward -inf, and in a feedback
  // path (IIR filters) that bias accumulates into a DC offset.
  return (fixed_t)fixed_round_shift64((int64_t)a * b, FIXED_FRAC_BITS);
}

static inline fixed_t fixed_div(fixed_t a, fixed_t b) {
  return (fixed_t)(((int64_t)a << FIXED_FRAC_BITS) / b);
}

// Conversion helpers for init-time / control-rate use only (soft float on
// the target). float_to_fixed saturates to the representable range.
static inline fixed_t float_to_fixed(float f) {
  float scaled = f * (float)FIXED_SCALE;
  if (scaled >= 2147483647.0f) return INT32_MAX;
  if (scaled <= -2147483648.0f) return INT32_MIN;
  return (fixed_t)lrintf(scaled);
}

static inline float fixed_to_float(fixed_t x) {
  return (float)x / (float)FIXED_SCALE;
}
