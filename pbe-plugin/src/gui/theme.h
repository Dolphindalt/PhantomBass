#pragma once

#include "canvas.h"

// Colours and metrics shared by the editor and its widgets.

static inline CanvasColor theme_rgb(unsigned char r, unsigned char g, unsigned char b) {
  CanvasColor c = {r / 255.0f, g / 255.0f, b / 255.0f, 1.0f};
  return c;
}

#define THEME_BG          theme_rgb(0x1b, 0x1d, 0x22)
#define THEME_HEADER_LINE theme_rgb(0x2e, 0x32, 0x3a)
#define THEME_TRACK       theme_rgb(0x34, 0x38, 0x42)
#define THEME_ACCENT      theme_rgb(0xf2, 0x9e, 0x4c)
#define THEME_ACCENT_DIM  theme_rgb(0x8c, 0x5e, 0x30)
#define THEME_KNOB        theme_rgb(0x26, 0x29, 0x30)
#define THEME_KNOB_EDGE   theme_rgb(0x48, 0x4e, 0x5a)
#define THEME_KNOB_HOVER  theme_rgb(0x6a, 0x72, 0x82)
#define THEME_POINTER     theme_rgb(0xf4, 0xf4, 0xf6)
#define THEME_TEXT        theme_rgb(0xd8, 0xdb, 0xe0)
#define THEME_TEXT_DIM    theme_rgb(0x84, 0x8a, 0x96)

#define THEME_EDITOR_WIDTH  480
#define THEME_EDITOR_HEIGHT 250

#define THEME_KNOB_RADIUS     34.0f
#define THEME_KNOB_TRACK      5.0f
#define THEME_KNOB_LABEL_SIZE 13.0f
#define THEME_KNOB_VALUE_SIZE 14.0f
#define THEME_TITLE_SIZE      13.0f
#define THEME_TAGLINE_SIZE    11.5f
