#pragma once

#include <stdbool.h>

// Minimal 2D drawing interface used by the editor and its widgets. The
// editor never touches a rendering library directly, so the renderer (today
// NanoVG on OpenGL, see canvas-nanovg.c) can be swapped without touching
// widget code. Coordinates are logical pixels, origin top-left, y down.
typedef struct Canvas Canvas;

typedef struct {
  float r, g, b, a; // 0..1
} CanvasColor;

typedef enum {
  CANVAS_ALIGN_LEFT,
  CANVAS_ALIGN_CENTER,
  CANVAS_ALIGN_RIGHT,
} CanvasAlign;

void Canvas_FillRect(Canvas *c, float x, float y, float w, float h, CanvasColor color);
void Canvas_FillRoundedRect(Canvas *c, float x, float y, float w, float h, float radius,
                            CanvasColor color);
void Canvas_FillCircle(Canvas *c, float cx, float cy, float r, CanvasColor color);
void Canvas_StrokeCircle(Canvas *c, float cx, float cy, float r, float width, CanvasColor color);

// Arc of a circle from angle a0 to a1, clockwise, in radians; 0 points right
// (3 o'clock), pi/2 points down. Round line caps.
void Canvas_StrokeArc(Canvas *c, float cx, float cy, float r, float a0, float a1, float width,
                      CanvasColor color);
void Canvas_StrokeLine(Canvas *c, float x0, float y0, float x1, float y1, float width,
                       CanvasColor color);

// Single line of text, vertically centred on y. letter_spacing is extra
// space between glyphs in pixels (0 for normal text).
void Canvas_Text(Canvas *c, float x, float y, const char *text, float size, float letter_spacing,
                 CanvasColor color, CanvasAlign align);

// Horizontal advance of `text` as Canvas_Text would draw it.
float Canvas_TextWidth(Canvas *c, const char *text, float size, float letter_spacing);
