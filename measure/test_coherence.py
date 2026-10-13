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
    "HPF 120 + pre +6@3k + post -4@8k": dict(hpf=120, preHz=3000, preDb=6, postHz=8000, postDb=-4),
    "pre/post linked +9 / -9 @ 4k": dict(preHz=4000, preDb=9, postHz=4000, postDb=-9),
    "wet EQ (LS+4@100, bell+8@300 Q4, HS-6@6k)": dict(wetEq=1, wetF1=100, wetG1=4, wetF2=300, wetG2=8, wetQ2=4, wetF4=6000, wetG4=-6),
    "dry EQ (bell+6@60 Q.5, bell-10@2k Q8)": dict(dryEq=1, dryLS=0, dryF1=60, dryG1=6, dryQ1=0.5, dryF3=2000, dryG3=-10, dryQ3=8),
    "everything + both comps w/ Mix + colour": dict(hpf=80, preDb=4, postDb=-3, wetEq=1, wetG2=6, wetQ2=2, wetG4=-5,
                                                   dryEq=1, dryG1=5, dryG3=-8, dryQ3=6, wetComp=1, wetIn=14, wetRatio=5,
                                                   wetColour=1, wetMix=50, dryComp=1, dryIn=10, dryColour=2, dryMix=30),
    "same, both paths Comp -> EQ": dict(hpf=80, preDb=4, postDb=-3, wetEq=1, wetG2=6, wetQ2=2, wetG4=-5,
                                        dryEq=1, dryG1=5, dryG3=-8, dryQ3=6, wetComp=1, wetIn=14, wetRatio=5,
                                        wetColour=1, wetMix=50, dryComp=1, dryIn=10, dryColour=2, dryMix=30,
                                        wetOrder=1, dryOrder=1),
    "pre-sat EQ (low +5 shelf, mid -8 Q3, high +6)": dict(peq=1, peqLG=5, peqMF=800, peqMG=-8, peqMQ=3, peqHG=6),
    "pre-sat EQ linked + envelope follower": dict(peq=1, peqLink=1, peqLG=6, peqMF=1500, peqMG=9, peqMQ=1.5, peqHG=-6, env=80, envSpeed=70),
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
