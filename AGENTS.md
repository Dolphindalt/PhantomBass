# phantom-bass agent notes

Phantom Bass monorepo: a bass enhancer for band-limited speakers. `pbe-dsp/` holds the C11 static DSP library for the RP2040 (Cortex-M0+, 196.5 MHz, 48 kHz). `pbe-plugin/` holds the host plugin that uses it. One CMake tree, one nix flake.
Samples are Q2.30 `int32_t` (`fixed_t`, full scale `1<<30`). Keep per-sample code to int32/int64; use float only in `*_Init` and `*_Set*`.

## Configure once, in the nix dev shell

```sh
nix develop -c cmake -S . -B build -G Ninja -DBUILD_TESTING=ON
```

## Targets

```sh
nix develop -c cmake --build build --target help        # list all build targets
nix develop -c cmake --build build                      # build everything
nix develop -c cmake --build build --target pbe-dsp     # build one target
nix develop -c ctest --test-dir build -N                # list tests
nix develop -c ctest --test-dir build --output-on-failure            # run all tests
nix develop -c ctest --test-dir build -R band-pass --output-on-failure   # run tests matching a regex
```

Tests use Unity (`pkg-config` via `cmake/FindUnity.cmake`). Add a test with `pbe_add_test(<name>-test)` in `<subproject>/tests/CMakeLists.txt`; put the source in `<subproject>/tests/<name>-test.c`.

## Nix packages

```sh
nix flake show                        # every package for every host system
nix build                             # packages.<host>.default = pbe-plugin, native
nix build .#pbe-dsp                   # native, runs tests in the sandbox
nix build .#pbe-dsp-arm-none-eabi     # arm-none-eabi, -mcpu=cortex-m0
nix build .#pbe-plugin-x86_64-windows # mingw cross build (Linux hosts)
nix build .#pbe-plugin-aarch64-linux  # aarch64 cross build (x86_64-linux host); aarch64 hosts get pbe-*-x86_64-linux
```

Put recipes in `nix/<name>.nix` and register them in `nix/overlays.nix`. `nix/legacy-packages.nix` applies the overlay to nixpkgs, so every recipe also exists under `pkgsCross.*`. `nix/packages.nix` selects the native and cross variants each host can build and names them `<package>-<target>`. Flakes see only git-tracked files: run `git add -N` on new files before `nix build`.

## Plugin (`pbe-plugin/`)

The plugin uses CPLUG for VST3 and CLAP, and pugl with NanoVG for the editor. See `pbe-plugin/README.md` for the module layout and threading model. Enable it with `-DBUILD_PLUGIN=ON`:

```sh
nix develop -c cmake -S . -B build -G Ninja -DBUILD_TESTING=ON -DBUILD_PLUGIN=ON   # dev shell exports PBE_CPLUG_DIR, PBE_PUGL_DIR, PBE_NANOVG_DIR, PBE_GUI_FONT
nix develop -c cmake --build build                    # writes build/plugins/PhantomBass.{vst3,clap}
nix develop -c ctest --test-dir build -R smoke --output-on-failure
nix build .#pbe-plugin                                # Release, runs the smoke tests
nix build .#pbe-plugin-x86_64-windows                 # Windows
```

The flake inputs `cplug`, `pugl` and `nanovg` (`flake = false`) provide the third-party sources. The build compiles them into the module and links only X11, GL and ALSA from the system. Run `pbe-gui-preview` and `pbe-tone` from a build made with the system toolchain; nix-built executables cannot load the system GL driver.

## Version and release

`VERSION` is the only place the version lives; CMake (`PBE_VERSION`, `PBE_VERSION_STRING` for `config.h`) and both nix packages read it. `scripts/check-version.sh` runs in CI and fails when `VERSION` is not `x.y.z`, is older than the latest `v*` tag, or equals a released version on a commit other than the tagged one. So: bump `VERSION` in the first change after every release.

To release:

1. Bump `VERSION` and add a `## <version>` section to `CHANGELOG.md`; merge to `main`.
2. Run the Release workflow by hand (Actions, Release, Run workflow). Tick `dry_run` to build the zips as a workflow artifact without publishing.
3. The workflow runs `scripts/check-version.sh --release` (tag must not exist, changelog entry must exist), builds `scripts/package-release.sh` (Linux x86_64, Linux aarch64, Windows x86_64 zips), tags `v<version>` and creates the GitHub Release with the changelog section as notes.

Run `scripts/package-release.sh` locally to produce the same zips in `dist/`.
