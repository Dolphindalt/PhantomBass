#pragma once

#include "fixed-math.h"

// Full-wave integrator: accumulates the magnitude of the (one-sample
// delayed) input and resets to zero on every positive-going zero crossing.
//
//   y[n] = 0                     if u[n] > 0 and u[n-1] <= 0
//   y[n] = y[n-1] + |u[n-1]|     otherwise
//
// "Full-wave" is full-wave rectification: integrating |u| turns a sine into
// a ramp of the same period, rich in every harmonic (2f at -1.4 dB, 3f at
// -7.4 dB, ... relative to the fundamental), which is what carries the
// virtual pitch. Integrating the signed input instead (as the MathWorks
// bass-enhancement page writes the equation) sums to zero over any full
// period, so the reset never fires and the output is just DC plus the
// fundamental: no harmonics at all.
//
// The accumulation saturates at the fixed_t limits rather than wrapping.
typedef struct {
  fixed_t u_prev; // u[n-1]
  fixed_t y_prev; // y[n-1]
} FullWaveIntegrator_t;

void FullWaveIntegrator_Init(FullWaveIntegrator_t *fwi);

fixed_t FullWaveIntegrator_Process(FullWaveIntegrator_t *fwi, fixed_t input);
