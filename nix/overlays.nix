flakeInputs: {
    default = final: prev: {
    pbe-dsp = final.callPackage ./pbe-dsp.nix { };
    pbe-plugin = final.callPackage ./pbe-plugin.nix {
      cplug-src = flakeInputs.cplug;
      pugl-src = flakeInputs.pugl;
      nanovg-src = flakeInputs.nanovg;
    };
  };
}
