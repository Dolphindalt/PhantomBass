flakeInputs:
let
  inherit (flakeInputs.nixpkgs) lib;
  systems = [
    "x86_64-linux"
    "aarch64-linux"
    "x86_64-darwin"
    "aarch64-darwin"
  ];
in
lib.genAttrs systems (
  system:
  let
    pkgs = flakeInputs.self.legacyPackages.${system};
    isLinux = lib.hasSuffix "-linux" system;

    # Cross package sets, keyed by the suffix used in the package names.
    # Both packages are built for every target that has the toolchain and
    # libraries for it; the bare-metal target only makes sense for the DSP.
    crossAll =
      lib.optionalAttrs isLinux {
        x86_64-windows = pkgs.pkgsCross.mingwW64;
      }
      // lib.optionalAttrs (system == "x86_64-linux") {
        aarch64-linux = pkgs.pkgsCross.aarch64-multiplatform;
      }
      // lib.optionalAttrs (system == "aarch64-linux") {
        x86_64-linux = pkgs.pkgsCross.gnu64;
      };
    crossDspOnly = {
      arm-none-eabi = pkgs.pkgsCross.arm-embedded; # RP2040, -mcpu=cortex-m0
    };

    forEach = names: f: lib.concatMapAttrs f names;
  in
  {
    default = pkgs.pbe-plugin;
    pbe-dsp = pkgs.pbe-dsp;
    pbe-plugin = pkgs.pbe-plugin;
  }
  // forEach crossAll (
    target: cross: {
      "pbe-dsp-${target}" = cross.pbe-dsp;
      "pbe-plugin-${target}" = cross.pbe-plugin;
    }
  )
  // forEach crossDspOnly (target: cross: { "pbe-dsp-${target}" = cross.pbe-dsp; })
)
