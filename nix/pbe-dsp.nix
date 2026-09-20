{
  lib,
  stdenv,
  cmake,
  ninja,
  pkg-config,
  unity-test,
}:

let
  # Only build and run the unit tests when the build machine can execute
  # host binaries; when cross compiling they are skipped.
  canRunTests = stdenv.buildPlatform.canExecute stdenv.hostPlatform;

  # Bare-metal ARM (arm-none-eabi): target the Cortex-M0.
  isBareMetalArm = stdenv.hostPlatform.isAarch32 && stdenv.hostPlatform.isNone;
in
stdenv.mkDerivation {
  pname = "pbe-dsp";
  version = lib.fileContents ../VERSION;

  src = lib.fileset.toSource {
    root = ../.;
    fileset = lib.fileset.unions [
      ../VERSION
      ../CMakeLists.txt
      ../cmake
      ../pbe-dsp
    ];
  };

  nativeBuildInputs = [
    cmake
    ninja
  ] ++ lib.optionals canRunTests [ pkg-config ];

  checkInputs = lib.optionals canRunTests [ unity-test ];

  cmakeFlags =
    [ (lib.cmakeBool "BUILD_TESTING" canRunTests) ]
    ++ lib.optionals isBareMetalArm [
      # No runtime to link a test executable against on bare metal.
      "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY"
    ];

  env.NIX_CFLAGS_COMPILE = lib.optionalString isBareMetalArm "-mcpu=cortex-m0 -mthumb";

  # Nixpkgs' default hardening flags (stack protector, stack-clash checks,
  # fortify, PIE) assume a hosted libc; stack-clash protection is not even
  # implemented for Thumb-1. None of them apply on a bare-metal MCU.
  hardeningDisable = lib.optionals isBareMetalArm [ "all" ];

  doCheck = canRunTests;

  meta = {
    description = "Psychoacoustic bass enhancement DSP library (Phantom Bass)";
    license = lib.licenses.mit;
    platforms = lib.platforms.all;
  };
}
