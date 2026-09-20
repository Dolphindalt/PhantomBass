#include "knob.h"

#include <math.h>
#include <string.h>

#include "theme.h"

// The arc runs clockwise from 7:30 to 4:30 (270 degrees), with 0 at the
// bottom-left end. Angles are in the canvas convention (0 = right, y down).
#define KNOB_PI 3.14159265358979f
#define KNOB_ARC_START (0.75f * KNOB_PI)
#define KNOB_ARC_SWEEP (1.5f * KNOB_PI)

#define KNOB_DRAG_PIXELS 200.0f // full range per this many pixels
#define KNOB_FINE_FACTOR 8.0f
#define KNOB_STEP_LINES 40.0f // full range per this many wheel clicks

static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

void Knob_Init(Knob *k, ParamIndex param, float cx, float cy, float radius) {
  memset(k, 0, sizeof(*k));
  k->param = param;
  k->cx = cx;
  k->cy = cy;
  k->radius = radius;
  Knob_SetValue(k, (float)Param_Normalise(param, Param_Info(param)->def));
}

bool Knob_Hit(const Knob *k, float x, float y) {
  float dx = x - k->cx, dy = y - k->cy;
  float r = k->radius + THEME_KNOB_TRACK * 2.0f;
  return dx * dx + dy * dy <= r * r;
}

bool Knob_SetValue(Knob *k, float normalised) {
  normalised = clamp01(normalised);
  bool changed = normalised != k->value;
  k->value = normalised;
  Param_Format(k->param, Param_Denormalise(k->param, normalised), k->value_text,
               sizeof(k->value_text));
  return changed;
}

void Knob_DragBegin(Knob *k, float y) {
  k->dragging = true;
  k->drag_start_y = y;
  k->drag_start_value = k->value;
}

bool Knob_DragTo(Knob *k, float y, bool fine) {
  if (!k->dragging) return false;
  float pixels = KNOB_DRAG_PIXELS * (fine ? KNOB_FINE_FACTOR : 1.0f);
  return Knob_SetValue(k, k->drag_start_value + (k->drag_start_y - y) / pixels);
}

void Knob_DragEnd(Knob *k) { k->dragging = false; }

bool Knob_Step(Knob *k, float lines, bool fine) {
  float per_line = 1.0f / (KNOB_STEP_LINES * (fine ? KNOB_FINE_FACTOR : 1.0f));
  return Knob_SetValue(k, k->value + lines * per_line);
}

void Knob_Paint(const Knob *k, Canvas *c) {
  const ParamInfo *info = Param_Info(k->param);
  float a0 = KNOB_ARC_START;
  float a_val = a0 + KNOB_ARC_SWEEP * k->value;
  float a1 = a0 + KNOB_ARC_SWEEP;
  float track_r = k->radius + THEME_KNOB_TRACK * 1.5f;

  // Track and value arc.
  Canvas_StrokeArc(c, k->cx, k->cy, track_r, a0, a1, THEME_KNOB_TRACK, THEME_TRACK);
  if (k->value > 0.0f) {
    Canvas_StrokeArc(c, k->cx, k->cy, track_r, a0, a_val, THEME_KNOB_TRACK, THEME_ACCENT);
  }

  // Body.
  Canvas_FillCircle(c, k->cx, k->cy, k->radius, THEME_KNOB);
  Canvas_StrokeCircle(c, k->cx, k->cy, k->radius, 1.5f,
                      (k->hovered || k->dragging) ? THEME_KNOB_HOVER : THEME_KNOB_EDGE);

  // Pointer.
  float px = cosf(a_val), py = sinf(a_val);
  Canvas_StrokeLine(c, k->cx + px * k->radius * 0.45f, k->cy + py * k->radius * 0.45f,
                    k->cx + px * k->radius * 0.85f, k->cy + py * k->radius * 0.85f, 3.0f,
                    THEME_POINTER);

  // Label above, value below.
  Canvas_Text(c, k->cx, k->cy - track_r - 16.0f, info->label, THEME_KNOB_LABEL_SIZE, 1.5f,
              THEME_TEXT_DIM, CANVAS_ALIGN_CENTER);
  Canvas_Text(c, k->cx, k->cy + track_r + 16.0f, k->value_text, THEME_KNOB_VALUE_SIZE, 0.0f,
              THEME_TEXT, CANVAS_ALIGN_CENTER);
}
