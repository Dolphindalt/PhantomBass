#pragma once

// Plugin identity, consumed by CPLUG's format wrappers (force-included into
// cplug_vst3.c / cplug_clap.c by CMake) and by our own sources.

#define CPLUG_IS_INSTRUMENT    0
#define CPLUG_WANT_GUI         1
#define CPLUG_GUI_RESIZABLE    0
#define CPLUG_WANT_MIDI_INPUT  0
#define CPLUG_WANT_MIDI_OUTPUT 0

#define CPLUG_COMPANY_NAME   "Dalton Caron"
#define CPLUG_COMPANY_EMAIL  ""
#define CPLUG_PLUGIN_NAME    "Phantom Bass"
#define CPLUG_PLUGIN_URI     "https://github.com/Dolphindalt/PhantomBass"
#ifndef PBE_VERSION_STRING
#error "PBE_VERSION_STRING must come from the build; it is read from the VERSION file"
#endif
#define CPLUG_PLUGIN_VERSION PBE_VERSION_STRING

// VST3 subcategories come from a fixed list; "Mastering" is the closest fit
// for "prepare a mix for playback on small speakers".
// https://steinbergmedia.github.io/vst3_doc/vstinterfaces/namespaceSteinberg_1_1Vst_1_1PlugType.html
#define CPLUG_VST3_CATEGORIES "Fx|Mastering|Stereo"

// Stable 128-bit class ids. Never change these once released.
#define CPLUG_VST3_TUID_COMPONENT  0x50424501, 0x636F6D70, 0x62617373, 0x00000001
#define CPLUG_VST3_TUID_CONTROLLER 0x50424501, 0x65646974, 0x62617373, 0x00000001

#define CPLUG_CLAP_ID          "com.daltoncaron.phantom-bass"
#define CPLUG_CLAP_DESCRIPTION                                                 \
  "Bass enhancer for band-limited speakers (laptops, phones, small Bluetooth " \
  "speakers): replaces bass below the speaker cutoff with harmonics the "      \
  "speaker can play"
// CLAP features: the standard tags hosts sort by, plus free-form keywords
// (allowed by the CLAP spec; hosts ignore the ones they do not know).
#define CPLUG_CLAP_FEATURES                                                    \
  CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, CLAP_PLUGIN_FEATURE_MASTERING,             \
      CLAP_PLUGIN_FEATURE_STEREO, "bass-enhancer", "virtual-bass",             \
      "band-limited-speakers"

// Name and tagline shown in the editor, and the space-free form used for
// file, bundle and window-class names.
#define PBE_PLUGIN_TITLE     "Phantom Bass"
#define PBE_PLUGIN_TAGLINE   "bass for band-limited speakers"
#define PBE_PLUGIN_FILE_NAME "PhantomBass"
