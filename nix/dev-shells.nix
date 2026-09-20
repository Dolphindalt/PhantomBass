flakeInputs:
flakeInputs.nixpkgs.lib.genAttrs
  [
    "x86_64-linux"
    "aarch64-linux"
    "x86_64-darwin"
    "aarch64-darwin"
  ]
  (
    system:
    let
      pkgs = flakeInputs.self.legacyPackages.${system};
    in
    {
      default = pkgs.mkShell {
        stdenv = pkgs.clangStdenv;

        buildInputs =
          with pkgs;
          [
            clang
            clang-tools # Includes clangd, clang-format, etc.
            llvmPackages.bintools
            cmake
            ninja
            pkg-config
            unity-test
          ]
          ++ lib.optionals stdenv.hostPlatform.isLinux [
            libX11
            libGL
            alsa-lib
          ];

        # Lets `cmake -DBUILD_PLUGIN=ON` find the vendored plugin dependencies
        # without any extra flags (see pbe-plugin/CMakeLists.txt).
        env = {
          PBE_CPLUG_DIR = "${flakeInputs.cplug}";
          PBE_PUGL_DIR = "${flakeInputs.pugl}";
          PBE_NANOVG_DIR = "${flakeInputs.nanovg}";
          PBE_GUI_FONT = "${pkgs.dejavu_fonts}/share/fonts/truetype/DejaVuSans.ttf";
        };
      };
    }
  )
