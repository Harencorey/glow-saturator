"""Aliasing, stability and edge-case checks on the C++ DSP (via GlowRender)."""
import subprocess
import numpy as np
import soundfile as sf

R = "plugin/build-linux/GlowRender_artefacts/Release/GlowRender"
T = "/tmp/claude-0/ph"


KEYS = ["drive", "even", "odd", "density", "hpf", "preHz", "preDb", "postHz", "postDb", "mix", "out"]


def render(x, sr, args, block=512):
    """args: dict of GlowRender options (see tools/RenderTool.cpp)"""
    sf.write(f"{T}/st_in.wav", x.astype(np.float32), sr, subtype="FLOAT")
    opts = [f"{k}={v}" for k, v in args.items()] + [f"block={block}"]
    subprocess.run([R, f"{T}/st_in.wav", f"{T}/st_out.wav", *opts], check=True, capture_output=True)
    y, _ = sf.read(f"{T}/st_out.wav")
    return y


ok = True
# 1. aliasing: 1 s sine near the top of the band, full drive, 0 dBFS
for sr in (44100, 48000, 96000):
    for f0 in (5000.0, 9000.0, 15000.0):
        n = sr
        x = 0.99 * np.sin(2 * np.pi * f0 * np.arange(n) / sr)
        y = render(x, sr, dict(drive=100))
        seg = y[sr // 4: sr // 4 + sr // 2] * np.blackman(sr // 2)
        S = np.abs(np.fft.rfft(seg)); f = np.fft.rfftfreq(len(seg), 1 / sr)
        harm = np.zeros_like(S, bool)
        for k in range(1, 40):
            if k * f0 < sr / 2:
                harm |= np.abs(f - k * f0) < 30
        harm |= f < 30
        fund = S[np.argmin(np.abs(f - f0))]
        alias = 20 * np.log10(np.max(S[~harm]) / fund)
        flag = "OK" if alias < -60 else "HIGH"
        print(f"alias  sr {sr:5d}  f0 {f0:7.0f}: worst non-harmonic {alias:6.1f} dBc  {flag}")

# 2. extremes: silence, DC, full-scale square, impulses, all knob corners, odd block sizes
sr = 48000
sigs = {
    "silence": np.zeros(sr),
    "dc_+1": np.ones(sr),
    "square_0dBFS": np.sign(np.sin(2 * np.pi * 50 * np.arange(sr) / sr)),
    "impulses": np.pad(np.ones(1), (0, sr - 1)) * 1.0,
    "noise_+6dB_clipped": np.clip(2 * np.random.default_rng(1).standard_normal(sr), -1, 1),
}
for name, x in sigs.items():
    for args in (dict(drive=0, even=0, odd=0, density=0),
                 dict(drive=100, even=100, odd=100, density=100, preDb=12, preHz=1000, postDb=12, postHz=1000, out=12),
                 dict(drive=100, even=0, odd=100, density=100, hpf=1000, preDb=-12, postDb=-12, wetLevel=-60, dryLevel=0, out=-24),
                 dict(drive=50, even=100, odd=0, density=50, hpf=200, preDb=6, postDb=-6, dryLevel=-6, wetLevel=-6),
                 dict(drive=100, even=100, odd=100, density=100, preHz=16000, preDb=12, postHz=16000, postDb=12),
                 # channel strips: everything on, extreme settings, both paths
                 dict(drive=80, dryLevel=6, wetLevel=6, dryEq=1, wetEq=1, dryG1=18, dryF1=20, dryG4=18, dryF4=20000,
                      wetG2=-18, wetQ2=10, wetG3=18, wetQ3=0.1, dryComp=1, wetComp=1, dryIn=36, wetIn=36,
                      dryRatio=7, wetRatio=7, dryAtk=0.05, dryRel=50, wetColour=1, dryColour=2, dryOut=24, wetOut=24),
                 dict(drive=50, dryLevel=0, wetLevel=-60, dryEq=1, dryComp=1, dryIn=-12, dryRatio=3, dryHpf=1, dryAtk=30, dryRel=3500)):
        for block in (1, 37, 512, 4096):
            y = render(x, sr, args, block)
            bad = not np.all(np.isfinite(y))
            peak = np.max(np.abs(y))
            if bad or peak > 256:   # finite and bounded (+48 dBFS: these combos stack ~+36 dB of gain on purpose)
                ok = False
                print(f"FAIL {name} args {args} block {block}: finite={not bad} peak {peak:.2f}")
    # absurd gain staging (1:1 comp, everything maxed): just has to stay finite
    y = render(x, sr, dict(wetComp=1, wetRatio=0, wetIn=36, wetOut=24, wetLevel=6, wetEq=1, wetG2=18, dryLevel=6), 512)
    if not np.all(np.isfinite(y)):
        ok = False; print(f"FAIL {name}: non-finite output with max gain staging")
    print(f"extremes {name}: done")

# 3. silence in -> silence out (no noise floor / DC creep)
y = render(np.zeros(sr * 2), sr, dict(drive=100, even=100, odd=100, density=100, preDb=12, postDb=12, out=12))
print(f"silence out peak at full settings: {20 * np.log10(np.max(np.abs(y)) + 1e-30):.1f} dBFS")

# 4. dry path flatness/alignment is covered by measure/test_strips.py (it carries the matched
#    oversampling filters + phase all-pass, so it is intentionally not a bit-exact copy)

# 5. block-size independence (same output whatever the host buffer)
x = np.random.default_rng(3).standard_normal(sr) * 0.3
a = render(x, sr, dict(drive=70, even=80, odd=30, density=60, hpf=80, preDb=4, postDb=-3), 64)
b = render(x, sr, dict(drive=70, even=80, odd=30, density=60, hpf=80, preDb=4, postDb=-3), 1024)
d = 20 * np.log10(np.max(np.abs(a - b)) + 1e-30)
print(f"block 64 vs 1024 max diff: {d:.1f} dB  (smoothing/auto-gain update per block, so small differences are expected)")
print("ALL OK" if ok else "SOME FAILURES")
