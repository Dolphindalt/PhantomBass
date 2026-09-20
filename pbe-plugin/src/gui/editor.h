#pragma once

#include <stdbool.h>

#include "canvas.h"
#include "plugin.h"

// The plugin editor: owns the widgets, lays them out, turns pointer input
// into parameter gestures, and paints through a Canvas. It knows nothing
// about windows, threads, or plugin formats; window-pugl.c feeds it events.
typedef struct Editor Editor;

enum {
  EDITOR_MOD_SHIFT = 1 << 0,
  EDITOR_MOD_CTRL = 1 << 1,
};

Editor *Editor_Create(PbePlugin *plugin);
void Editor_Destroy(Editor *editor);

// Logical size in pixels (before any HiDPI scale).
void Editor_GetSize(unsigned *width, unsigned *height);

// Reloads every knob from the plugin; call when the window is (re)attached.
void Editor_SyncFromPlugin(Editor *editor);

// Periodic poll for parameter changes from the host. Returns true when
// something changed and a repaint is needed.
bool Editor_Tick(Editor *editor);

void Editor_Paint(Editor *editor, Canvas *canvas, float width, float height);

// Pointer input in logical coordinates. `time` is in seconds (any origin)
// and is used for double-click detection.
void Editor_MouseDown(Editor *editor, float x, float y, int button, unsigned mods, double time);
void Editor_MouseUp(Editor *editor, float x, float y, int button);
void Editor_MouseMove(Editor *editor, float x, float y, unsigned mods);
void Editor_MouseLeave(Editor *editor);
void Editor_Scroll(Editor *editor, float x, float y, float lines, unsigned mods);

// True once after anything that requires a repaint. Clears the flag.
bool Editor_TakeDirty(Editor *editor);
