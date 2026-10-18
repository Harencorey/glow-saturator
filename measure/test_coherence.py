"""Dry/Wet phase lock: wet-only vs dry-only transfer phase with filters/comps loaded on both paths.
Renders low-level noise with Drive/Density at 0 (wet stays near-linear), estimates each path's
transfer function by cross-spectrum, and reports the phase difference and what a 50/50 parallel
sum would lose vs. a perfect in-phase sum."""
import subprocess, sys, numpy as np, soundfile as sf
from scipy.signal import csd, welch

R = "plugin/build-linux/GlowRender_artefacts/Release/GlowRender"
T = "/tmp/claude-0/ph"
SRS = [int(a) for a in sys.argv[1:]] or [44100, 48000, 96000]


def render(path_in, **kw):
    out = f"{T}/coh_out.wav"
    subprocess.run([R, path_in, out, *[f"{k}={v}" for k, v in kw.items()]], check=True, capture_output=True)
    return sf.read(out)[0]


def tf(x, y, sr):
    f, pxy = csd(x, y, sr, nperseg=16384)
    _, pxx = welch(x, sr, nperseg=16384)
    return f, pxy / pxx


SETTINGS = {
    "HPF 120 + pre-EQ mid +6@3k, high -4@8k": dict(hpf=120, peq=1, peqMF=3000, peqMG=6, peqHF=8000, peqHG=-4),
    "HPF 150 12 dB + Keep lows": dict(hpf=150, hpf24=0, keep=1),
    "HPF 200 24 dB + Keep lows + linked EQ": dict(hpf=200, keep=1, peq=1, peqLink=1, peqMF=900, peqMG=8, peqHG=-6),
    "master EQ + comp after the sum": dict(hpf=80, keep=1, peq=1, peqHG=4, masterEq=1, masterG2=6, masterComp=1, masterIn=12, masterRatio=5),
    "pre-sat EQ (mid -8 Q3, high +6)": dict(peq=1, peqMF=800, peqMG=-8, peqMQ=3, peqHG=6),
    "pre-sat EQ linked + envelope follower": dict(peq=1, peqLink=1, peqMF=1500, peqMG=9, peqMQ=1.5, peqHG=-6, env=80, envSpeed=70),
}
CHECK = [20, 30, 50, 80, 120, 200, 300, 500, 1000, 2000, 4000, 6000, 8000, 12000, 16000]

worst_all = 0.0
for sr in SRS:
    rng = np.random.default_rng(3)
    x = rng.standard_normal(sr * 6) * 10 ** (-30 / 20)
    sf.write(f"{T}/coh_in.wav", np.stack([x, x], 1).astype(np.float32), sr, subtype="FLOAT")
    seg = slice(sr, None)   # skip the first second (filter fade-ins)
    print(f"\n=== {sr} Hz ===")
    for name, kw in SETTINGS.items():
        w = render(f"{T}/coh_in.wav", drive=0, density=0, dryLevel=0, wetLevel=0, solo=1, **kw)[:, 0]
        d = render(f"{T}/coh_in.wav", drive=0, density=0, dryLevel=0, wetLevel=0, solo=2, **kw)[:, 0]
        f, hw = tf(x[seg], w[seg], sr)
        _, hd = tf(x[seg], d[seg], sr)
        # judge only where both paths are within 20 dB of each other (where a mismatch could
        # actually change the sum); deep in a filter's stopband phase is irrelevant
        band = (f >= 20) & (f <= min(16000, 0.45 * sr)) & (np.abs(20 * np.log10(np.abs(hw / hd))) < 20)
        dphi = np.degrees(np.angle(hw[band] / hd[band]))
        worst = np.max(np.abs(dphi))
        worst_all = max(worst_all, worst)
        loss = 20 * np.log10(np.cos(np.radians(worst) / 2))
        pts = "  ".join(f"{c}:{np.degrees(np.angle(hw[np.argmin(abs(f - c))] / hd[np.argmin(abs(f - c))])):+.1f}" for c in CHECK if c < 0.45 * sr)
        print(f"  {name:44s} worst {worst:5.2f} deg (50/50 sum loses <= {-loss:.3f} dB)")
        print(f"      {pts}")
print(f"\nWORST over everything: {worst_all:.2f} deg")
