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
    "IRON + HPF 120 Keep lows + pre-EQ": dict(type=1, hpf=120, keep=1, peq=1, peqMF=900, peqMG=6),
    "TAPE + HPF 120 Keep lows + wet EQ": dict(type=2, hpf=120, keep=1, wetEq=1, wetG2=6, wetQ2=2),
    "DIODE + HPF 200 Keep lows + both comps": dict(type=3, hpf=200, keep=1, wetComp=1, wetIn=10, dryComp=1, dryIn=10),
    "DIODE + HPF 80 36 dB Keep lows + mirror EQ": dict(type=3, hpf=80, slope=3, keep=1, peq=1, peqLink=1, peqMF=1500, peqMG=8),
    "HPF 150 36 dB": dict(hpf=150, slope=3, keep=0),
    "HPF 150 36 dB + Keep lows": dict(hpf=150, slope=3, keep=1),
    "HPF 150 6 dB + Keep lows": dict(hpf=150, slope=0, keep=1),
    "HPF 150 6 dB (not phase-locked: expect up to ~85 deg)": dict(hpf=150, slope=0, keep=0),
    "HPF 200 24 dB + Keep lows + linked EQ": dict(hpf=200, keep=1, peq=1, peqLink=1, peqMF=900, peqMG=8, peqHG=-6),
    "wet EQ (LS+4@100, bell+8@300 Q4, HS-6@6k)": dict(wetEq=1, wetF1=100, wetG1=4, wetF2=300, wetG2=8, wetQ2=4, wetF4=6000, wetG4=-6),
    "dry EQ (bell+6@60 Q.5, bell-10@2k Q8)": dict(dryEq=1, dryLS=0, dryF1=60, dryG1=6, dryQ1=0.5, dryF3=2000, dryG3=-10, dryQ3=8),
    "everything + both comps w/ Mix + colour": dict(hpf=80, keep=1, peq=1, peqHG=4, wetEq=1, wetG2=6, wetQ2=2, wetG4=-5,
                                                   dryEq=1, dryG1=5, dryG3=-8, dryQ3=6, wetComp=1, wetIn=14, wetRatio=5,
                                                   wetColour=1, wetMix=50, dryComp=1, dryIn=10, dryColour=2, dryMix=30),
    "same, both paths Comp -> EQ": dict(hpf=80, keep=1, peq=1, peqHG=4, wetEq=1, wetG2=6, wetQ2=2, wetG4=-5,
                                        dryEq=1, dryG1=5, dryG3=-8, dryQ3=6, wetComp=1, wetIn=14, wetRatio=5,
                                        wetColour=1, wetMix=50, dryComp=1, dryIn=10, dryColour=2, dryMix=30,
                                        wetOrder=1, dryOrder=1),
    "pre-sat EQ (mid -8 Q3, high +6)": dict(peq=1, peqMF=800, peqMG=-8, peqMQ=3, peqHG=6),
    "pre-sat EQ linked + envelope follower": dict(peq=1, peqLink=1, peqMF=1500, peqMG=9, peqMQ=1.5, peqHG=-6, env=80, envSpeed=70),
    "mixed order (wet Comp -> EQ, dry EQ -> Comp)": dict(wetEq=1, wetG2=8, wetQ2=3, dryEq=1, dryG3=-6, wetComp=1,
                                                       wetIn=12, dryComp=1, dryIn=12, wetOrder=1),
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
