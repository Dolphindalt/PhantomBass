{
  lib,
  stdenv,
  buildPackages,
  cmake,
  ninja,
  libX11,
  libGL,
  alsa-lib,
  cplug-src,
  pugl-src,
  nanovg-src,
}:

let
  canRunTests = stdenv.buildPlatform.canExecute stdenv.hostPlatform;
  isLinux = stdenv.hostPlatform.isLinux;
  # The font is embedded at build time, so it's build-platform data.
  font = "${buildPackages.dejavu_fonts}/share/fonts/truetype/DejaVuSans.ttf";
in
stdenv.mkDerivation {
  pname = "pbe-plugin";
  version = lib.fileContents ../VERSION;

  src = lib.fileset.toSource {
    root = ../.;
    fileset = lib.fileset.unions [
      ../VERSION
      ../CMakeLists.txt
      ../cmake
      ../pbe-dsp
      ../pbe-plugin
    ];
  };

  nativeBuildInputs = [
    cmake
    ninja
  ];

  # Windows gets OpenGL/GDI from the MinGW runtime and macOS from the SDK
  # frameworks; only X11 needs explicit libraries.
  buildInputs = lib.optionals isLinux [
    libX11
    libGL
    alsa-lib # pbe-tone audition tool only
  ];

  cmakeFlags = [
    (lib.cmakeBool "BUILD_TESTING" false)
    (lib.cmakeBool "BUILD_PLUGIN" true)
    (lib.cmakeBool "PBE_PLUGIN_TOOLS" canRunTests)
    (lib.cmakeFeature "PBE_CPLUG_DIR" "${cplug-src}")
    (lib.cmakeFeature "PBE_PUGL_DIR" "${pugl-src}")
    (lib.cmakeFeature "PBE_NANOVG_DIR" "${nanovg-src}")
    (lib.cmakeFeature "PBE_GUI_FONT" font)
  ];

  doCheck = canRunTests && !stdenv.hostPlatform.isWindows;
  checkPhase = ''
    runHook preCheck
    ./pbe-plugin/pbe-clap-smoke ./plugins/PhantomBass.clap
    ./pbe-plugin/pbe-vst3-smoke ./pbe-plugin/PhantomBass.so
    runHook postCheck
  '';

  meta = {
    description = "Phantom Bass: psychoacoustic bass enhancement VST3/CLAP plugin";
    license = lib.licenses.mit;
    platforms = lib.platforms.linux ++ lib.platforms.darwin ++ lib.platforms.windows;
  };
}
