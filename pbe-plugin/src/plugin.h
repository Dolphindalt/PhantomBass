#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "params.h"

// The plugin object behind CPLUG's opaque `void* userPlugin`. The definition
// is private to plugin.c; the GUI talks to it through the functions below and
// never sees CPLUG or the DSP engine.
typedef struct PbePlugin PbePlugin;

// ---- GUI-facing API -------------------------------------------------------
// All of these are called from the GUI thread (which on Windows/macOS is the
// host's UI thread, and on Linux is a thread the GUI owns).

double Plugin_GetParamNormalised(const PbePlugin *plugin, ParamIndex idx);
double Plugin_GetParamPlain(const PbePlugin *plugin, ParamIndex idx);

// A knob interaction: BeginGesture, any number of Set..., EndGesture.
void Plugin_GuiBeginGesture(PbePlugin *plugin, ParamIndex idx);
void Plugin_GuiSetParamNormalised(PbePlugin *plugin, ParamIndex idx, double normalised);
void Plugin_GuiEndGesture(PbePlugin *plugin, ParamIndex idx);

// Bit i is set when parameter i changed from outside the GUI (host
// automation, preset load) since the last call. Clears the mask.
uint32_t Plugin_TakeGuiDirtyMask(PbePlugin *plugin);

// Whether GUI-originated parameter changes are made on the host's UI thread.
// Decides how they are reported to the host (see plugin.c).
bool Plugin_GuiRunsOnHostThread(void);
