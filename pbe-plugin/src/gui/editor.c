#include "editor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "knob.h"
#include "theme.h"

#define DOUBLE_CLICK_SECONDS 0.35

struct Editor {
  PbePlugin *plugin;
  Knob knobs[PARAM_COUNT];
  int active;  // knob being dragged, or -1
  int hovered; // knob under the pointer, or -1
  bool dirty;

  double last_click_time;
  int last_click_knob;
};

static void layout(Editor *e) {
  // Knobs in one row, evenly spaced across the width, below the header.
  float slot = (float)THEME_EDITOR_WIDTH / PARAM_COUNT;
  float cy = 72.0f + ((float)THEME_EDITOR_HEIGHT - 72.0f) / 2.0f;
  for (int i = 0; i < PARAM_COUNT; i++) {
    Knob_Init(&e->knobs[i], (ParamIndex)i, slot * ((float)i + 0.5f), cy, THEME_KNOB_RADIUS);
  }
}

Editor *Editor_Create(PbePlugin *plugin) {
  Editor *e = calloc(1, sizeof(*e));
  if (!e) return NULL;
  e->plugin = plugin;
  e->active = -1;
  e->hovered = -1;
  e->last_click_knob = -1;
  e->last_click_time = -1e9;
  layout(e);
  Editor_SyncFromPlugin(e);
  return e;
}

void Editor_Destroy(Editor *e) { free(e); }

void Editor_GetSize(unsigned *width, unsigned *height) {
  *width = THEME_EDITOR_WIDTH;
  *height = THEME_EDITOR_HEIGHT;
}

void Editor_SyncFromPlugin(Editor *e) {
  for (int i = 0; i < PARAM_COUNT; i++) {
    Knob_SetValue(&e->knobs[i], (float)Plugin_GetParamNormalised(e->plugin, (ParamIndex)i));
  }
  e->dirty = true;
}

bool Editor_Tick(Editor *e) {
  uint32_t mask = Plugin_TakeGuiDirtyMask(e->plugin);
  for (int i = 0; i < PARAM_COUNT; i++) {
    // A knob being dragged already shows the newest value; a host echo of an
    // older one would only make it jitter.
    if (!(mask & (1u << i)) || e->knobs[i].dragging) continue;
    if (Knob_SetValue(&e->knobs[i], (float)Plugin_GetParamNormalised(e->plugin, (ParamIndex)i))) {
      e->dirty = true;
    }
  }
  return e->dirty;
}

bool Editor_TakeDirty(Editor *e) {
  bool d = e->dirty;
  e->dirty = false;
  return d;
}

void Editor_Paint(Editor *e, Canvas *c, float width, float height) {
  Canvas_FillRect(c, 0, 0, width, height, THEME_BG);

  // Header: title, tagline, version.
  Canvas_Text(c, 24.0f, 30.0f, PBE_PLUGIN_TITLE, THEME_TITLE_SIZE, 2.0f, THEME_TEXT,
              CANVAS_ALIGN_LEFT);
  float title_w = Canvas_TextWidth(c, PBE_PLUGIN_TITLE, THEME_TITLE_SIZE, 2.0f);
  Canvas_Text(c, 24.0f + title_w + 14.0f, 30.0f, PBE_PLUGIN_TAGLINE, THEME_TAGLINE_SIZE, 0.0f,
              THEME_TEXT_DIM, CANVAS_ALIGN_LEFT);
  Canvas_Text(c, width - 24.0f, 30.0f, "v" CPLUG_PLUGIN_VERSION, THEME_TITLE_SIZE, 0.0f,
              THEME_ACCENT_DIM, CANVAS_ALIGN_RIGHT);
  Canvas_FillRect(c, 24.0f, 48.0f, width - 48.0f, 1.0f, THEME_HEADER_LINE);
  Canvas_FillRect(c, 24.0f, 48.0f, 48.0f, 1.0f, THEME_ACCENT);

  for (int i = 0; i < PARAM_COUNT; i++) Knob_Paint(&e->knobs[i], c);
  e->dirty = false;
}

static int knob_at(const Editor *e, float x, float y) {
  for (int i = 0; i < PARAM_COUNT; i++) {
    if (Knob_Hit(&e->knobs[i], x, y)) return i;
  }
  return -1;
}

static void set_hovered(Editor *e, int idx) {
  if (idx == e->hovered) return;
  if (e->hovered >= 0) e->knobs[e->hovered].hovered = false;
  if (idx >= 0) e->knobs[idx].hovered = true;
  e->hovered = idx;
  e->dirty = true;
}

static void push_value(Editor *e, int idx) {
  Plugin_GuiSetParamNormalised(e->plugin, (ParamIndex)idx, e->knobs[idx].value);
  e->dirty = true;
}

void Editor_MouseDown(Editor *e, float x, float y, int button, unsigned mods, double time) {
  (void)mods;
  if (button != 0) return;
  int idx = knob_at(e, x, y);
  if (idx < 0) return;

  bool double_click =
      idx == e->last_click_knob && time - e->last_click_time < DOUBLE_CLICK_SECONDS;
  e->last_click_knob = idx;
  e->last_click_time = time;

  Knob *k = &e->knobs[idx];
  if (double_click) {
    // Reset to default as a complete gesture of its own.
    Plugin_GuiBeginGesture(e->plugin, (ParamIndex)idx);
    Knob_SetValue(k, (float)Param_Normalise((ParamIndex)idx, Param_Info((ParamIndex)idx)->def));
    push_value(e, idx);
    Plugin_GuiEndGesture(e->plugin, (ParamIndex)idx);
    return;
  }

  e->active = idx;
  Knob_DragBegin(k, y);
  Plugin_GuiBeginGesture(e->plugin, (ParamIndex)idx);
  e->dirty = true;
}

void Editor_MouseUp(Editor *e, float x, float y, int button) {
  if (button != 0 || e->active < 0) return;
  Knob_DragEnd(&e->knobs[e->active]);
  Plugin_GuiEndGesture(e->plugin, (ParamIndex)e->active);
  e->active = -1;
  e->dirty = true;
  set_hovered(e, knob_at(e, x, y));
}

void Editor_MouseMove(Editor *e, float x, float y, unsigned mods) {
  if (e->active >= 0) {
    bool fine = (mods & (EDITOR_MOD_SHIFT | EDITOR_MOD_CTRL)) != 0;
    if (Knob_DragTo(&e->knobs[e->active], y, fine)) push_value(e, e->active);
    return;
  }
  set_hovered(e, knob_at(e, x, y));
}

void Editor_MouseLeave(Editor *e) {
  if (e->active < 0) set_hovered(e, -1);
}

void Editor_Scroll(Editor *e, float x, float y, float lines, unsigned mods) {
  int idx = e->active >= 0 ? e->active : knob_at(e, x, y);
  if (idx < 0 || lines == 0.0f) return;
  bool fine = (mods & (EDITOR_MOD_SHIFT | EDITOR_MOD_CTRL)) != 0;
  if (!Knob_Step(&e->knobs[idx], lines, fine)) return;
  // A wheel click is its own gesture unless a drag is in progress.
  if (e->active != idx) Plugin_GuiBeginGesture(e->plugin, (ParamIndex)idx);
  push_value(e, idx);
  if (e->active != idx) Plugin_GuiEndGesture(e->plugin, (ParamIndex)idx);
}
