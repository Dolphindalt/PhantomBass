#include "params.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "pbe-dsp.h" // PBE_GAIN_OFF_DB

static const ParamInfo PARAMS[PARAM_COUNT] = {
    // Linear in dB (an audio taper). 0 dB sits at 12 o'clock and the useful
    // region, roughly +3..+15 dB, fills the right half; the leftmost position
    // mutes the enhancement (see Param_GainDb).
    [PARAM_GAIN] = {.id = 1,
                    .name = "Enhancement Gain",
                    .label = "GAIN",
                    .unit = "dB",
                    .min = -24.0f,
                    .max = 24.0f,
                    .def = 6.0f,
                    .scale = PARAM_SCALE_LINEAR,
                    .min_text = "Off"},
    // Where the speaker stops reproducing bass: ~60 Hz for a small bookshelf
    // speaker, 150-300 Hz for a laptop, up to ~500 Hz for a phone.
    [PARAM_SPEAKER_CUTOFF] = {.id = 2,
                              .name = "Speaker Cutoff",
                              .label = "SPEAKER CUTOFF",
                              .unit = "Hz",
                              .min = 20.0f,
                              .max = 500.0f,
                              .def = 100.0f,
                              .scale = PARAM_SCALE_LOG,
                              .min_text = NULL},
    [PARAM_BANDPASS_HIGH] = {.id = 3,
                             .name = "Harmonics High Cutoff",
                             .label = "HARMONICS",
                             .unit = "Hz",
                             .min = 100.0f,
                             .max = 4000.0f,
                             .def = 500.0f,
                             .scale = PARAM_SCALE_LOG,
                             .min_text = NULL},
};

const ParamInfo *Param_Info(ParamIndex idx) { return &PARAMS[idx]; }

double Param_GainDb(double plain) {
  return plain <= PARAMS[PARAM_GAIN].min ? PBE_GAIN_OFF_DB : plain;
}

int Param_IndexFromId(uint32_t id) {
  for (int i = 0; i < PARAM_COUNT; i++) {
    if (PARAMS[i].id == id) return i;
  }
  return -1;
}

double Param_Clamp(ParamIndex idx, double plain) {
  const ParamInfo *p = &PARAMS[idx];
  if (plain < p->min) return p->min;
  if (plain > p->max) return p->max;
  return plain;
}

static double clamp01(double x) { return x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x); }

double Param_Normalise(ParamIndex idx, double plain) {
  const ParamInfo *p = &PARAMS[idx];
  plain = Param_Clamp(idx, plain);
  if (p->scale == PARAM_SCALE_LOG) {
    return clamp01(log(plain / p->min) / log(p->max / p->min));
  }
  return clamp01((plain - p->min) / (p->max - p->min));
}

double Param_Denormalise(ParamIndex idx, double normalised) {
  const ParamInfo *p = &PARAMS[idx];
  normalised = clamp01(normalised);
  if (p->scale == PARAM_SCALE_LOG) {
    return Param_Clamp(idx, p->min * pow(p->max / p->min, normalised));
  }
  return Param_Clamp(idx, p->min + normalised * (p->max - p->min));
}

void Param_Format(ParamIndex idx, double plain, char *buf, size_t buflen) {
  const ParamInfo *p = &PARAMS[idx];
  plain = Param_Clamp(idx, plain);
  if (p->min_text && plain <= p->min) {
    snprintf(buf, buflen, "%s", p->min_text);
  } else if (idx == PARAM_GAIN) {
    snprintf(buf, buflen, "%+.1f %s", plain, p->unit);
  } else if (plain >= 1000.0) {
    snprintf(buf, buflen, "%.2f k%s", plain / 1000.0, p->unit);
  } else {
    snprintf(buf, buflen, "%.0f %s", plain, p->unit);
  }
}

// Case-insensitive "does text start with prefix"; portable (no strings.h).
static int starts_with_nocase(const char *text, const char *prefix) {
  for (; *prefix; text++, prefix++) {
    if (tolower((unsigned char)*text) != tolower((unsigned char)*prefix)) return 0;
  }
  return 1;
}

double Param_Parse(ParamIndex idx, const char *text) {
  const ParamInfo *p = &PARAMS[idx];
  while (*text == ' ') text++;
  if (p->min_text && starts_with_nocase(text, p->min_text)) return p->min;
  char *end = NULL;
  double v = strtod(text, &end);
  if (end == text) return p->def;
  while (*end == ' ') end++;
  if (*end == 'k' || *end == 'K') v *= 1000.0;
  return Param_Clamp(idx, v);
}
