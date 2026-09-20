#pragma once

#include <stdbool.h>

#include "canvas.h"
#include "params.h"

// A rotary knob bound to one parameter. Holds the GUI-side copy of the value
// (normalised) and the drag state; it doesn't talk to the plugin itself, the
// editor does that when the knob reports a change.
typedef struct {
  ParamIndex param;
  float cx, cy, radius;
  float value; // normalised 0..1
  char value_text[32];

  bool hovered;
  bool dragging;
  float drag_start_y;
  float drag_start_value;
} Knob;

void Knob_Init(Knob *k, ParamIndex param, float cx, float cy, float radius);

bool Knob_Hit(const Knob *k, float x, float y);

// Sets the value and refreshes value_text. Returns true if it changed.
bool Knob_SetValue(Knob *k, float normalised);

// Vertical drag: moving the pointer up increases the value. `fine` slows the
// drag down for precise adjustment.
void Knob_DragBegin(Knob *k, float y);
bool Knob_DragTo(Knob *k, float y, bool fine);
void Knob_DragEnd(Knob *k);

// Mouse wheel: `lines` > 0 increases the value.
bool Knob_Step(Knob *k, float lines, bool fine);

void Knob_Paint(const Knob *k, Canvas *c);
