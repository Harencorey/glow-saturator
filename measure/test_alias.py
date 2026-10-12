"""Aliasing audit: worst non-harmonic component (dBc) for sine tones through Glow."""
import subprocess, sys, numpy as np, soundfile as sf
T = "/tmp/claude-0/ph"
SR = 48000


def worst_alias(R, f0, lvl, **kw):
    x = 10 ** (lvl / 20) * np.sin(2 * np.pi * f0 * np.arange(SR) / SR)
    sf.write(f"{T}/al_in.wav", x.astype(np.float32), SR, subtype="FLOAT")
    out = subprocess.run([R, f"{T}/al_in.wav", f"{T}/al_out.wav", *[f"{k}={v}" for k, v in kw.items()]],
                         check=True, capture_output=True, text=True).stdout
    y = sf.read(f"{T}/al_out.wav")[0]
    seg = y[SR // 4: SR // 4 + SR // 2] * np.blackman(SR // 2)
    S = np.abs(np.fft.rfft(seg)); f = np.fft.rfftfreq(SR // 2, 1 / SR)
    harm = (f < 25) | (f > 20000)          # only the audible band counts
    for k in range(1, 200):
        if k * f0 <= SR / 2: harm |= np.abs(f - k * f0) < 25
    return 20 * np.log10(S[~harm].max() / S[np.argmin(np.abs(f - f0))])


cases = [("Drive 50, -12 dBFS", dict(drive=50), -12), ("Drive 75, -6 dBFS", dict(drive=75), -6),
         ("Drive 100, -6 dBFS", dict(drive=100), -6), ("Drive 100, 0 dBFS (extreme)", dict(drive=100), 0),
         ("Drive 50 + Odd 100, -6 dBFS", dict(drive=50, odd=100), -6),
         ("Comp Dist 3 on wet, Input +18, -6 dBFS", dict(drive=0, wetComp=1, wetRatio=0, wetIn=18, wetColour=2), -6),
         ("Comp Dist 2 on wet, Input +18, -6 dBFS", dict(drive=0, wetComp=1, wetRatio=0, wetIn=18, wetColour=1), -6)]
bins = dict(x.split("=") for x in sys.argv[1:])
freqs = (3100.0, 7300.0, 11700.0, 15300.0)
print(f"{'case':42s}" + "".join(f"{n:>9s}" for n in bins) + "   (worst over " + ", ".join(f"{int(f)}" for f in freqs) + " Hz)")
for name, kw, lvl in cases:
    row = f"{name:42s}"
    for n, R in bins.items():
        row += f"{max(worst_alias(R, f0, lvl, **kw) for f0 in freqs):9.1f}"
    print(row + "  dBc")
