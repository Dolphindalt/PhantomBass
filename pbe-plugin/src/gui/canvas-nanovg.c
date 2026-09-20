#include "canvas-nanovg.h"

#include <stdlib.h>

#include "gl-loader.h" // GL headers first: nanovg_gl.h needs them
#include <nanovg.h>
#define NANOVG_GL2_IMPLEMENTATION
#include <nanovg_gl.h>

struct Canvas {
  NVGcontext *vg;
  int font;
};

#define FONT_NAME "ui"

static NVGcolor to_nvg(CanvasColor c) { return nvgRGBAf(c.r, c.g, c.b, c.a); }

Canvas *CanvasNanoVG_Create(const unsigned char *font_data, size_t font_size) {
  Canvas *c = calloc(1, sizeof(*c));
  if (!c) return NULL;
  c->vg = nvgCreateGL2(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
  if (!c->vg) {
    free(c);
    return NULL;
  }
  // freeData = 0: the font bytes are a static array owned by the caller.
  c->font = nvgCreateFontMem(c->vg, FONT_NAME, (unsigned char *)font_data, (int)font_size, 0);
  return c;
}

void CanvasNanoVG_Destroy(Canvas *c) {
  if (!c) return;
  nvgDeleteGL2(c->vg);
  free(c);
}

void CanvasNanoVG_BeginFrame(Canvas *c, float width, float height, float pixel_ratio) {
  nvgBeginFrame(c->vg, width, height, pixel_ratio);
}

void CanvasNanoVG_EndFrame(Canvas *c) { nvgEndFrame(c->vg); }

void Canvas_FillRect(Canvas *c, float x, float y, float w, float h, CanvasColor color) {
  nvgBeginPath(c->vg);
  nvgRect(c->vg, x, y, w, h);
  nvgFillColor(c->vg, to_nvg(color));
  nvgFill(c->vg);
}

void Canvas_FillRoundedRect(Canvas *c, float x, float y, float w, float h, float radius,
                            CanvasColor color) {
  nvgBeginPath(c->vg);
  nvgRoundedRect(c->vg, x, y, w, h, radius);
  nvgFillColor(c->vg, to_nvg(color));
  nvgFill(c->vg);
}

void Canvas_FillCircle(Canvas *c, float cx, float cy, float r, CanvasColor color) {
  nvgBeginPath(c->vg);
  nvgCircle(c->vg, cx, cy, r);
  nvgFillColor(c->vg, to_nvg(color));
  nvgFill(c->vg);
}

void Canvas_StrokeCircle(Canvas *c, float cx, float cy, float r, float width, CanvasColor color) {
  nvgBeginPath(c->vg);
  nvgCircle(c->vg, cx, cy, r);
  nvgStrokeWidth(c->vg, width);
  nvgStrokeColor(c->vg, to_nvg(color));
  nvgStroke(c->vg);
}

void Canvas_StrokeArc(Canvas *c, float cx, float cy, float r, float a0, float a1, float width,
                      CanvasColor color) {
  nvgBeginPath(c->vg);
  nvgArc(c->vg, cx, cy, r, a0, a1, NVG_CW);
  nvgLineCap(c->vg, NVG_ROUND);
  nvgStrokeWidth(c->vg, width);
  nvgStrokeColor(c->vg, to_nvg(color));
  nvgStroke(c->vg);
}

void Canvas_StrokeLine(Canvas *c, float x0, float y0, float x1, float y1, float width,
                       CanvasColor color) {
  nvgBeginPath(c->vg);
  nvgMoveTo(c->vg, x0, y0);
  nvgLineTo(c->vg, x1, y1);
  nvgLineCap(c->vg, NVG_ROUND);
  nvgStrokeWidth(c->vg, width);
  nvgStrokeColor(c->vg, to_nvg(color));
  nvgStroke(c->vg);
}

float Canvas_TextWidth(Canvas *c, const char *text, float size, float letter_spacing) {
  nvgFontFaceId(c->vg, c->font);
  nvgFontSize(c->vg, size);
  nvgTextLetterSpacing(c->vg, letter_spacing);
  nvgTextAlign(c->vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
  return nvgTextBounds(c->vg, 0.0f, 0.0f, text, NULL, NULL);
}

void Canvas_Text(Canvas *c, float x, float y, const char *text, float size, float letter_spacing,
                 CanvasColor color, CanvasAlign align) {
  int h_align = align == CANVAS_ALIGN_LEFT     ? NVG_ALIGN_LEFT
                : align == CANVAS_ALIGN_CENTER ? NVG_ALIGN_CENTER
                                               : NVG_ALIGN_RIGHT;
  nvgFontFaceId(c->vg, c->font);
  nvgFontSize(c->vg, size);
  nvgTextLetterSpacing(c->vg, letter_spacing);
  nvgTextAlign(c->vg, h_align | NVG_ALIGN_MIDDLE);
  nvgFillColor(c->vg, to_nvg(color));
  nvgText(c->vg, x, y, text, NULL);
}
