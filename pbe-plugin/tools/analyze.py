#!/usr/bin/env python3
"""Measures what the plugin does to test signals, via tools/render.c.

    analyze.py <pbe-render> <PhantomBass.clap> [out.png]

Renders each signal with the enhancement off (gain knob at its minimum) and on, then
reports per-band levels, the change in the band above the harmonics cutoff
(which must be untouched), clipping, and plots the spectra.
"""
import subprocess, sys, tempfile, os
import numpy as np
from scipy.signal import welch

FS = 48000
DUR = 4.0
CUTOFF, HIGH = 100.0, 500.0

def sine(f, a, t): return a * np.sin(2 * np.pi * f * t)

def make_signals():
    t = np.arange(int(FS * DUR)) / FS
    sig = {}
    sig["50 Hz tone"] = sine(50, 0.25, t)
    sig["bass chord + 1 kHz"] = sine(41.2, 0.2, t) + sine(61.7, 0.2, t) + sine(1000, 0.1, t)
    # Synthetic music: bass line, kick, mid chord, hi-hat noise bursts.
    rng = np.random.default_rng(1)
    bass = np.zeros_like(t)
    for i, f in enumerate([41.2, 55.0, 73.4, 49.0] * 2):
        seg = (t >= i * 0.5) & (t < (i + 1) * 0.5)
        env = np.exp(-(t[seg] - i * 0.5) * 3.0)
        bass[seg] = 0.35 * env * np.sin(2 * np.pi * f * t[seg])
    kick = np.zeros_like(t)
    for i in range(8):
        seg = (t >= i * 0.5) & (t < i * 0.5 + 0.25)
        tt = t[seg] - i * 0.5
        kick[seg] = 0.4 * np.exp(-tt * 12) * np.sin(2 * np.pi * (40 + 60 * np.exp(-tt * 30)) * tt)
    chord = sum(sine(f, 0.08, t) for f in (220, 277.2, 329.6, 440))
    noise = rng.standard_normal(len(t))
    from scipy.signal import butter, sosfilt
    hat = sosfilt(butter(4, 5000, "high", fs=FS, output="sos"), noise)
    hat_env = np.zeros_like(t)
    for i in range(16):
        seg = (t >= i * 0.25) & (t < i * 0.25 + 0.08)
        hat_env[seg] = np.exp(-(t[seg] - i * 0.25) * 60)
    music = bass + kick + chord + 0.15 * hat * hat_env
    sig["synthetic music"] = music / np.max(np.abs(music)) * 0.7
    return t, sig

def render(render_bin, clap, x, gain_db, cutoff=CUTOFF, high=HIGH):
    stereo = np.repeat(x.astype(np.float32)[:, None], 2, axis=1).ravel()
    with tempfile.TemporaryDirectory() as d:
        fi, fo = os.path.join(d, "in.f32"), os.path.join(d, "out.f32")
        stereo.tofile(fi)
        subprocess.run([render_bin, clap, fi, fo, str(gain_db), str(cutoff), str(high)],
                       check=True, capture_output=True)
        y = np.fromfile(fo, dtype=np.float32).reshape(-1, 2)
    return y[:, 0].astype(np.float64)

def band_db(f, p, lo, hi):
    m = (f >= lo) & (f < hi)
    return 10 * np.log10(np.trapezoid(p[m], f[m]) + 1e-20)

def spectrum(x):
    f, p = welch(x[FS:], FS, nperseg=8192)  # skip the first second (settling)
    return f, p

def main():
    render_bin, clap = sys.argv[1], sys.argv[2]
    out_png = sys.argv[3] if len(sys.argv) > 3 else None
    t, sig = make_signals()
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    fig, axes = plt.subplots(len(sig), 1, figsize=(10, 3.4 * len(sig)))
    for ax, (name, x) in zip(axes, sig.items()):
        y_off = render(render_bin, clap, x, -100.0)  # clamps to the knob minimum: Off
        y_on = render(render_bin, clap, x, 0.0)
        y_hot = render(render_bin, clap, x, 12.0)
        fx, px = spectrum(x)
        fo, po = spectrum(y_off)
        fn, pn = spectrum(y_on)
        fh, ph = spectrum(y_hot)
        bands = [("below cutoff", 20, CUTOFF), ("harmonics band", CUTOFF, HIGH), ("above", HIGH, 20000)]
        print(f"== {name}")
        for label, lo, hi in bands:
            print(f"  {label:15s} in {band_db(fx, px, lo, hi):6.1f} dB  off {band_db(fo, po, lo, hi):6.1f}"
                  f"  on(0dB) {band_db(fn, pn, lo, hi):6.1f}  on(+12dB) {band_db(fh, ph, lo, hi):6.1f}")
        # Energy added above the harmonics cutoff (the bandpass is only
        # 12 dB/oct, so the upper harmonics leak) relative to the whole input.
        m = fx > HIGH * 1.5
        total_in = np.trapezoid(px, fx)
        for label, p in (("0 dB", pn), ("+12 dB", ph)):
            added = np.trapezoid(np.maximum(p[m] - px[m], 0.0), fx[m])
            print(f"  energy added above {HIGH*1.5:.0f} Hz at {label:6s}: {10*np.log10(added/total_in + 1e-20):6.1f} dB re input")
        for label, y in (("off", y_off), ("on 0 dB", y_on), ("on +12 dB", y_hot)):
            print(f"  peak {label:9s} {np.max(np.abs(y)):.3f}  clipped samples {(np.abs(y) >= 0.999).sum()}")
        ax.semilogx(fx, 10 * np.log10(px + 1e-20), label="input", color="0.6")
        ax.semilogx(fo, 10 * np.log10(po + 1e-20), label="enhancement off", color="tab:blue")
        ax.semilogx(fn, 10 * np.log10(pn + 1e-20), label="on, 0 dB", color="tab:orange")
        ax.axvline(CUTOFF, color="k", ls=":", lw=0.8); ax.axvline(HIGH, color="k", ls=":", lw=0.8)
        ax.set_xlim(20, 20000); ax.set_ylim(-140, -20); ax.set_title(name); ax.grid(alpha=0.3)
        ax.set_ylabel("dB/Hz")
    axes[-1].set_xlabel("Hz"); axes[0].legend(loc="upper right")
    fig.tight_layout()
    if out_png:
        fig.savefig(out_png, dpi=110)
        print("wrote", out_png)

if __name__ == "__main__":
    main()
