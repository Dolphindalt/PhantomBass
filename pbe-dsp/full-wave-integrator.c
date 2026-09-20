#include "full-wave-integrator.h"

void FullWaveIntegrator_Init(FullWaveIntegrator_t *fwi) {
  fwi->u_prev = 0;
  fwi->y_prev = 0;
}

fixed_t FullWaveIntegrator_Process(FullWaveIntegrator_t *fwi, fixed_t input) {
  fixed_t y;
  if (input > 0 && fwi->u_prev <= 0) {
    y = 0; // positive-going zero crossing: reset
  } else {
    // Rectify, then saturate: a DC offset with no zero crossings would
    // otherwise wrap.
    fixed_t mag = fwi->u_prev < 0 ? (fwi->u_prev == INT32_MIN ? INT32_MAX : -fwi->u_prev)
                                  : fwi->u_prev;
    y = fixed_add_sat(fwi->y_prev, mag);
  }
  fwi->u_prev = input;
  fwi->y_prev = y;
  return y;
}
