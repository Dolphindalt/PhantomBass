#pragma once

#include <stddef.h>

#include "canvas.h"

// NanoVG (OpenGL 2) implementation of the Canvas interface. All functions
// must be called with the view's GL context current.

// font_data must outlive the canvas (it is not copied).
Canvas *CanvasNanoVG_Create(const unsigned char *font_data, size_t font_size);
void CanvasNanoVG_Destroy(Canvas *canvas);

// Frame bracket. width/height are logical pixels; pixel_ratio scales them to
// the framebuffer, whose viewport must already be set by the caller.
void CanvasNanoVG_BeginFrame(Canvas *canvas, float width, float height, float pixel_ratio);
void CanvasNanoVG_EndFrame(Canvas *canvas);
