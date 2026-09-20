#pragma once

#include <stddef.h>
#include <stdint.h>

// The plugin's parameter table. Everything the host or the GUI needs to know
// about a parameter lives here: identity, range, default, scaling, and text
// conversion. No CPLUG, DSP-state, or GUI dependencies.
//
// "Plain" values are in the parameter's own unit (dB, Hz). "Normalised"
// values are in [0, 1] and are what knobs and VST3 exchange.

typedef enum {
  PARAM_GAIN = 0,
  PARAM_SPEAKER_CUTOFF,
  PARAM_BANDPASS_HIGH,
  PARAM_COUNT
} ParamIndex;

typedef enum {
  PARAM_SCALE_LINEAR,
  PARAM_SCALE_LOG, // normalised = log(v / min) / log(max / min)
} ParamScale;

typedef struct {
  uint32_t id; // host-facing id, part of the saved state: never renumber
  const char *name;       // full name shown by the host
  const char *label;      // short name shown on the knob
  const char *unit;
  float min, max, def;    // plain values
  ParamScale scale;
  const char *min_text;   // optional text shown instead of the value at min
} ParamInfo;

const ParamInfo *Param_Info(ParamIndex idx);

// Gain to hand to the DSP: the knob's minimum means "off" (muted
// enhancement), which the DSP expresses as a gain at or below its own
// PBE_GAIN_OFF_DB threshold.
double Param_GainDb(double plain);

// Returns -1 for unknown ids.
int Param_IndexFromId(uint32_t id);

double Param_Clamp(ParamIndex idx, double plain);
double Param_Normalise(ParamIndex idx, double plain);
double Param_Denormalise(ParamIndex idx, double normalised);

// Human readable value with unit, e.g. "120 Hz", "+3.0 dB", "Off".
void Param_Format(ParamIndex idx, double plain, char *buf, size_t buflen);

// Inverse of Param_Format; unparseable text yields the default.
double Param_Parse(ParamIndex idx, const char *text);
