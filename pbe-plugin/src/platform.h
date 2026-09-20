#pragma once

// Where the GUI runs. On Windows and macOS the host's UI thread drives our
// window (messages / NSView callbacks). On Linux the host gives us a bare X11
// window and no run loop, so the GUI owns a thread of its own.
#if defined(_WIN32) || defined(__APPLE__)
#define PBE_GUI_ON_HOST_THREAD 1
#else
#define PBE_GUI_ON_HOST_THREAD 0
#endif

// CPLUG provides exchange/load/add/and on its cplug_atomic_i32; we also need
// or, to set bits in the GUI dirty mask.
#include <cplug.h>
#if defined(_MSC_VER) && !defined(__clang__)
extern long _InterlockedOr(long volatile *Destination, long Value);
static inline int pbe_atomic_fetch_or_i32(cplug_atomic_i32 *ptr, int v) {
  return _InterlockedOr((volatile long *)ptr, v);
}
#else
static inline int pbe_atomic_fetch_or_i32(cplug_atomic_i32 *ptr, int v) {
  return __atomic_fetch_or(ptr, v, __ATOMIC_SEQ_CST);
}
#endif
