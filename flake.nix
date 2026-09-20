{
  description = "Phantom Bass: psychoacoustic bass enhancement DSP library and VST3/CLAP plugin";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-26.05";

    # Third-party sources compiled into the plugin (see pbe-plugin/CMakeLists.txt).
    cplug = {
      url = "github:Tremus/CPLUG";
      flake = false;
    };
    pugl = {
      url = "github:lv2/pugl";
      flake = false;
    };
    nanovg = {
      url = "github:memononen/nanovg";
      flake = false;
    };
  };

  outputs = flakeInputs: {
    devShells = import ./nix/dev-shells.nix flakeInputs;
    legacyPackages = import ./nix/legacy-packages.nix flakeInputs;
    overlays = import ./nix/overlays.nix flakeInputs;
    packages = import ./nix/packages.nix flakeInputs;
  };
}
