"""Aliasing, stability and edge-case checks on the C++ DSP (via GlowRender)."""
import subprocess
import numpy as np
import soundfile as sf

R = "plugin/build-linux/GlowRender_artefacts/Release/GlowRender"
T = "/tmp/claude-0/ph"


def render(x, sr, args, block=512):
    sf.write(f"{T}/st_in.wav", x.astype(np.float32), sr, subtype="FLOAT")
    subprocess.run([R, f"{T}/st_in.wav", f"{T}/st_out.wav", *map(str, args), str(block)], check=True, capture_output=True)
    y, _ = sf.read(f"{T}/st_out.wav")
    return y


ok = True
# 1. aliasing: 1 s sine near the top of the band, full drive, 0 dBFS
for sr in (44100, 48000, 96000):
    for f0 in (5000.0, 9000.0, 15000.0):
        n = sr
        x = 0.99 * np.sin(2 * np.pi * f0 * np.arange(n) / sr)
        y = render(x, sr, [100, 0, 50, 100, 0])
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
    for args in ([0, -100, 0, 100, 0], [100, 100, 100, 100, 12], [100, -100, 100, 0, -24], [50, 0, 50, 50, 0]):
        for block in (1, 37, 512, 4096):
            y = render(x, sr, args, block)
            bad = not np.all(np.isfinite(y))
            peak = np.max(np.abs(y))
            if bad or peak > 8:
                ok = False
                print(f"FAIL {name} args {args} block {block}: finite={not bad} peak {peak:.2f}")
    print(f"extremes {name}: done")

# 3. silence in -> silence out (no noise floor / DC creep)
y = render(np.zeros(sr * 2), sr, [100, 100, 100, 100, 12])
print(f"silence out peak at full settings: {20 * np.log10(np.max(np.abs(y)) + 1e-30):.1f} dBFS")

# 4. mix = 0 must be a clean, latency-compensated pass-through
x = np.random.default_rng(2).standard_normal(sr) * 0.1
y = render(x, sr, [100, 0, 50, 0, 0])
err = 20 * np.log10(np.max(np.abs(y - x)) + 1e-30)
print(f"mix 0% null vs input: {err:.1f} dB  {'OK' if err < -90 else 'FAIL'}")
ok &= err < -90

# 5. block-size independence (same output whatever the host buffer)
x = np.random.default_rng(3).standard_normal(sr) * 0.3
a = render(x, sr, [70, 30, 60, 100, 0], 64)
b = render(x, sr, [70, 30, 60, 100, 0], 1024)
d = 20 * np.log10(np.max(np.abs(a - b)) + 1e-30)
print(f"block 64 vs 1024 max diff: {d:.1f} dB  {'OK' if d < -80 else 'CHECK'}")
print("ALL OK" if ok else "SOME FAILURES")
