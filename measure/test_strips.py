"""Measure the Dry/Wet channel strips: alignment, EQ response, compressor curve/timing/colour."""
import subprocess, numpy as np, soundfile as sf
R = "plugin/build-linux/GlowRender_artefacts/Release/GlowRender"
T = "/tmp/claude-0/ph"
SR = 48000


def render(x, **kw):
    sf.write(f"{T}/ts_in.wav", np.asarray(x, np.float32), SR, subtype="FLOAT")
    subprocess.run([R, f"{T}/ts_in.wav", f"{T}/ts_out.wav", *[f"{k}={v}" for k, v in kw.items()]], check=True, capture_output=True)
    return sf.read(f"{T}/ts_out.wav")[0]


def tone(f, db, secs=1.0):
    return 10 ** (db / 20) * np.sin(2 * np.pi * f * np.arange(int(SR * secs)) / SR)


def level(y, f):
    seg = y[len(y) // 2: len(y) // 2 + SR // 4]
    t = np.arange(len(seg)) / SR
    return 20 * np.log10(abs(2 * np.mean(seg * np.exp(-2j * np.pi * f * t))) + 1e-12)


DRY_ONLY = dict(wetLevel=-60, dryLevel=0)
ok = True

# 1. dry path is flat and latency-aligned with the input (it carries the same linear-phase
#    oversampling filters and a low-end phase-match all-pass, so it is not a bit-exact copy)
x = np.random.default_rng(0).standard_normal(SR * 30) * 0.2
y = render(x, **DRY_ONLY)
from scipy.signal import csd, welch
fr, Pxy = csd(x, y, fs=SR, nperseg=65536)       # averaged transfer function H = Pxy / Pxx
_, Pxx = welch(x, fs=SR, nperseg=65536)
band = (fr > 30) & (fr < 18000)
mag = 20 * np.log10(np.abs(Pxy[band] / Pxx[band]))
lag = np.argmax(np.correlate(y[1000:5000], x[1000:5000], "full")) - (4000 - 1)
print(f"dry-only vs input: magnitude {mag.min():+.2f}..{mag.max():+.2f} dB (30 Hz-18 kHz), time offset {lag} samples "
      f"{'OK' if abs(mag).max() < 0.1 and lag == 0 else 'FAIL'}"); ok &= abs(mag).max() < 0.1 and lag == 0

# 2. dry 0 dB + wet at drive 0 -> parallel sum, check it sums coherently (no comb filtering)
y = render(tone(1000, -30), drive=0, density=0, dryLevel=0)
w = render(tone(1000, -30), drive=0, density=0)
print(f"parallel dry+wet @1k: {level(y, 1000) - level(w, 1000):+.2f} dB vs wet alone (≈ +6 means aligned)")

# 3. EQ: each band vs the target curve
print("EQ (dry path, -30 dBFS tones):")
for desc, kw, checks in [
    ("band2 bell 1 kHz +6 dB Q1", dict(dryF2=1000, dryG2=6, dryQ2=1), [(1000, 6.0), (100, 0.0), (10000, 0.0)]),
    ("band3 bell 3 kHz -9 dB Q4", dict(dryF3=3000, dryG3=-9, dryQ3=4), [(3000, -9.0), (1000, 0.0)]),
    ("band1 low shelf 100 Hz +6", dict(dryF1=100, dryG1=6), [(30, 6.0), (100, 3.0), (2000, 0.0)]),
    ("band1 as bell 100 Hz +6", dict(dryF1=100, dryG1=6, dryLS=0, dryQ1=1), [(100, 6.0), (30, 0.6)]),
    ("band4 high shelf 8k -6", dict(dryF4=8000, dryG4=-6), [(16000, -6.0), (8000, -3.0), (1000, 0.0)]),
]:
    out = []
    for f, want in checks:
        got = level(render(tone(f, -30), **DRY_ONLY, dryEq=1, **kw), f) + 30
        out.append(f"{f} Hz {got:+.2f} (want ≈{want:+.1f})")
    print(f"  {desc:28s} " + " | ".join(out))

# 4. compressor static curve: fixed -18 dBFS threshold, 4:1
print("Comp static curve, 1 kHz, input knob 0 dB:")
for ratio_i, name in ((1, "2:1"), (3, "4:1"), (6, "20:1"), (7, "NUKE")):
    row = []
    for lv in (-30, -18, -12, -6, 0):
        out = level(render(tone(1000, lv, 1.5), **DRY_ONLY, dryComp=1, dryRatio=ratio_i, dryAtk=1, dryRel=100), 1000)
        row.append(f"{lv}->{out:.1f}")
    print(f"  {name:5s} " + "  ".join(row))

# 5. attack / release on a level step (-30 -> -6 -> -30), 4:1, input +0
x = np.concatenate([tone(1000, -30, 0.5), tone(1000, -6, 0.5), tone(1000, -30, 1.5)])
for atk, rel in ((1, 100), (10, 1000)):
    y = render(x, **DRY_ONLY, dryComp=1, dryRatio=3, dryAtk=atk, dryRel=rel)
    hop = SR // 1000
    env = 20 * np.log10(np.sqrt(np.mean((y[: len(y) // hop * hop].reshape(-1, hop)) ** 2, axis=1)) + 1e-9)
    on = env[500:1000]; fin = on[-50:].mean(); start = on[1]
    t63 = np.argmax(on <= start - 0.63 * (start - fin))
    off = env[1000:2500]; fin2 = off[-100:].mean(); s2 = off[1]
    r63 = np.argmax(off >= s2 + 0.63 * (fin2 - s2))
    print(f"  attack {atk} ms / release {rel} ms -> measured ~{t63} ms to 63% GR, ~{r63} ms to 63% recovery")

# 6. colour modes: harmonics at -12 dBFS, ratio 1:1 (no compression, just colour)
def harm(y, f0):
    seg = y[SR // 2: SR]; t = np.arange(len(seg)) / SR; w = np.hanning(len(seg))
    c = [abs(np.sum(seg * w * np.exp(-2j * np.pi * k * f0 * t))) for k in (1, 2, 3)]
    return [20 * np.log10(c[k] / c[0] + 1e-12) for k in (1, 2)]
for col, name in ((0, "CLEAN"), (1, "DIST 2"), (2, "DIST 3")):
    h = harm(render(tone(200, -12), **DRY_ONLY, dryComp=1, dryRatio=0, dryColour=col), 200)
    print(f"  colour {name:6s}: H2 {h[0]:7.1f} dBc  H3 {h[1]:7.1f} dBc")

# 7. detector HPF: 50 Hz tone should compress much less with HPF on
for hp in (0, 1):
    out = level(render(tone(50, -6, 1.5), **DRY_ONLY, dryComp=1, dryRatio=3, dryHpf=hp), 50)
    print(f"  50 Hz @ -6 dBFS, detector HPF {'on ' if hp else 'off'}: out {out:.1f} dBFS")
print("ALL OK" if ok else "FAILURES")
