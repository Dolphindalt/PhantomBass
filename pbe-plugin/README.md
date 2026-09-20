# pbe-plugin

The Phantom Bass VST3 and CLAP plugin around `pbe-dsp`, built with [CPLUG](https://github.com/Tremus/CPLUG). Use it on material bound for band-limited speakers (laptops, phones, small Bluetooth speakers) and set Speaker Cutoff to the speaker's cutoff. The build produces one module and lays it out as `PhantomBass.vst3` and `PhantomBass.clap`.

```
host (VST3 / CLAP)
   │  cplug_vst3.c / cplug_clap.c  (CPLUG, vendored)
   ▼
src/plugin.c        CPLUG entry points: busses, parameters, state, process()
   │                  ├─ src/params.[ch]   parameter table: ids, ranges, defaults, log/lin, text
   │                  ├─ src/engine.[ch]   plugin internals: owns PbeDsp_t, float to fixed, block chunking
   │                  └─ src/param-queue.h lock-free GUI to audio parameter events
   │
   │  plugin.h (GUI-facing API: read params, begin/set/end gestures, dirty mask)
   ▼
src/gui/window-pugl.c   cplug_createGUI to cplug_setParent: pugl view, GL context, threading
   ├─ src/gui/editor.[ch]      layout, input to gestures, painting (framework-agnostic)
   ├─ src/gui/knob.[ch]        rotary widget: hit test, drag, wheel, double-click, paint
   ├─ src/gui/canvas.h         drawing interface the editor paints through
   ├─ src/gui/canvas-nanovg.c  Canvas implemented with NanoVG on OpenGL 2
   ├─ src/gui/gl-loader.[ch]   GL headers; runtime loader for Windows' opengl32
   └─ src/gui/theme.h          colours and metrics
```

Only `plugin.c` and `window-pugl.c` include CPLUG. Only `window-pugl.c`, `canvas-nanovg.c` and `gl-loader.*` touch pugl, OpenGL or NanoVG. To change the GUI framework, replace those three files and keep the editor and widgets.

## GUI stack

CPLUG passes the plugin a native parent handle (HWND, NSView or X11 window id) and nothing else. pugl embeds a GL view into that parent on X11, Win32 and Cocoa. NanoVG draws antialiased vector shapes and text on GL 2 and needs no runtime library beyond the system GL. The build compiles both from source into the module and hides every symbol except the plugin entry points. The module therefore ships no shared libraries, carries no `/nix/store` paths into the host, and coexists with plugins that bundle other pugl versions.

The build embeds the font (`cmake/EmbedFile.cmake`). Nix supplies DejaVu Sans. Plain CMake builds search the usual system directories; override the font with `-DPBE_GUI_FONT=/path/to/font.ttf`.

## Threading

* Audio thread: `cplug_process()` drains the GUI queue, applies the values to the engine, forwards GUI events to the host, then runs `PbeDsp_ProcessBlock`.
* GUI: on Windows and macOS the host's UI thread drives the pugl view and a pugl timer ticks it. On Linux the host provides no run loop for embedded X11 views, so `window-pugl.c` runs its own thread. That thread owns the view and pumps `puglUpdate()` at about 60 Hz.
* Parameters: `plugin.c` stores plain values as aligned floats, which never tear on the supported targets. Host changes set a bit in an atomic dirty mask that the editor polls. GUI changes travel through a single-producer, single-consumer ring to the audio thread.

## Build

The dev shell exports `PBE_CPLUG_DIR`, `PBE_PUGL_DIR`, `PBE_NANOVG_DIR` and `PBE_GUI_FONT`, so CMake finds the vendored sources and the font:

```sh
nix develop -c cmake -S . -B build -G Ninja -DBUILD_TESTING=ON -DBUILD_PLUGIN=ON
nix develop -c cmake --build build
nix develop -c ctest --test-dir build -R smoke --output-on-failure
```

Find the outputs in `build/plugins/PhantomBass.vst3` and `build/plugins/PhantomBass.clap`. Copy them to `~/.vst3/` and `~/.clap/` on Linux.

Build the nix package (Release build; the smoke tests run in the sandbox):

```sh
nix build                              # result/lib/{vst3,clap} for this machine
nix build .#pbe-plugin-x86_64-windows  # Windows x86_64, cross-compiled on Linux
nix build .#pbe-plugin-aarch64-linux   # aarch64 Linux, cross-compiled on x86_64-linux
```

To build without nix, install a C11 compiler, CMake 3.20 or newer, X11 and GL headers on Linux, and checkouts of CPLUG, pugl and NanoVG:

```sh
cmake -S . -B build -DBUILD_PLUGIN=ON -DPBE_CPLUG_DIR=<cplug> -DPBE_PUGL_DIR=<pugl> -DPBE_NANOVG_DIR=<nanovg>
```

## Tools (`tools/`)

* `pbe-clap-smoke <PhantomBass.clap>` loads the module as a CLAP host does. It checks the parameter metadata, runs a 50 Hz tone with the enhancement on and off and a 1 kHz tone, and round-trips the state into a second instance. `ctest` runs it.
* `pbe-vst3-smoke <PhantomBass.so>` does the same through the VST3 C API: factory, component, bus arrangement, `setupProcessing`, automation. `ctest` runs it.
* `pbe-render <PhantomBass.clap> in.f32 out.f32 [gain] [speaker] [harmonics]` pushes a raw float32 stereo file through the plugin.
* `tools/analyze.py <pbe-render> <PhantomBass.clap> [out.png]` renders test signals (50 Hz tone, bass chord with 1 kHz, synthetic music) through `pbe-render`, reports per-band levels, leakage above the harmonics cutoff and clipping, and plots the spectra. Requires numpy, scipy and matplotlib.
* `pbe-tone [--freq 50] [--level -18] [--device default] [--bassline]` (Linux, ALSA) plays a test tone through the plugin to the sound card with the editor open. Keys: `+` and `-` step the tone by a semitone, `<` and `>` change the level, `m` plays a bass line, `s` returns to the sine, `b` bypasses the plugin, `q` quits. Start with a tone between 40 and 60 Hz and raise Gain.
* `pbe-gui-preview [seconds]` (Linux) opens the editor in an X11 window, pumps silent audio through `cplug_process()` and prints the parameter values on exit.

Executables built in the nix dev shell link nix's `libGLX` and `libasound`. On a non-NixOS host they cannot find the system Mesa driver or the PipeWire ALSA plugin. To run `pbe-tone` or `pbe-gui-preview` there, build a second tree with the system toolchain:

```sh
env -i HOME=$HOME PATH=/usr/bin:/bin cmake -S . -B build-host -G Ninja -DCMAKE_C_COMPILER=/usr/bin/gcc -DBUILD_PLUGIN=ON -DPBE_CPLUG_DIR=<cplug> -DPBE_PUGL_DIR=<pugl> -DPBE_NANOVG_DIR=<nanovg>
```

The plugin itself is unaffected: the DAW loads it with the system loader.

## Measurements

`tools/analyze.py` at the defaults (Speaker 100 Hz, Harmonics 500 Hz):

* A 50 Hz tone produces the harmonic series 100, 150, 200 Hz and up. At 0 dB gain the series sits about 12 dB below the tone; at +12 dB it carries equal energy. The crossover cuts the band below the cutoff by 14 dB. With the enhancement off, the band above 500 Hz stays within 0.1 dB of the input.
* The bandpass upper edge rolls off at 12 dB/oct, so harmonics leak above the Harmonics cutoff: about -33 dB re the input at 0 dB, -20 dB at +12 dB. A steeper lowpass would reduce this.
* Chords produce intermodulation products, as every nonlinear virtual-bass method does.
* The output stage clips at ±1.0, as the RP2040 target requires. At +12 dB with full-scale bass the clipping is audible. Keep peaks in check upstream.

## Status

* Verified: Linux build, smoke tests, editor rendering and interaction under Xvfb, nix package check, Windows and aarch64 cross builds. macOS compiles in theory only (`mac.m`, `mac_gl.m`, exported-symbols list); untested.
* The editor has a fixed size (`CPLUG_GUI_RESIZABLE 0`). The host content scale applies only when the view is created.
* GUI parameter edits reach a VST3 host through output parameter changes on the audio thread, and on Windows and macOS also through `IComponentHandler` on the UI thread. FL Studio and Ableton record automation only from the latter, which the Linux GUI thread cannot use.
* The plugin adds no parameter smoothing beyond the DSP library's own.
