"""Generate the measurement signal to print through the reference saturator.

Layout (48 kHz, 24-bit, mono — print as mono or dual-mono stereo):
  sync click -> log sweep -> stepped sines (100 Hz, 1 kHz) -> level-step
  bursts (attack/release) -> multitone -> sync click.
Every segment is separated by silence; segment start times go to a JSON
manifest so the analysis script can slice the prints exactly.
"""
import json
import numpy as np
import soundfile as sf

SR = 48000
GAP = 0.5  # seconds of silence between segments


def db(x):
    return 10 ** (x / 20)


def fade(x, ms=5):
    n = int(SR * ms / 1000)
    w = np.ones_like(x)
    ramp = 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, n))
    w[:n] = ramp
    w[-n:] = ramp[::-1]
    return x * w


segments = []
audio = []
t_cursor = 0.0


def add(name, x, **meta):
    global t_cursor
    segments.append({"name": name, "start": t_cursor, "length": len(x) / SR, **meta})
    audio.append(x)
    t_cursor += len(x) / SR
    gap = np.zeros(int(SR * GAP))
    audio.append(gap)
    t_cursor += GAP


# 1. sync click (single-sample impulse at -6 dBFS) for latency alignment
click = np.zeros(int(SR * 0.05)); click[0] = db(-6)
add("sync_start", click)

# 2. exponential sine sweep 20 Hz-20 kHz, 10 s, at -18 dBFS (frequency response + harmonics)
T = 10.0
f1, f2 = 20.0, 20000.0
t = np.arange(int(SR * T)) / SR
L = T / np.log(f2 / f1)
sweep = np.sin(2 * np.pi * f1 * L * (np.exp(t / L) - 1))
add("sweep_-18", fade(sweep * db(-18)), f1=f1, f2=f2, level_db=-18)

# 3. stepped-level sines: transfer curve + harmonic balance vs level
levels = list(range(-36, 1, 3))  # -36 .. 0 dBFS
for f in (100.0, 1000.0):
    for lv in levels:
        n = int(SR * 0.5)
        tone = np.sin(2 * np.pi * f * np.arange(n) / SR) * db(lv)
        add(f"sine_{int(f)}Hz_{lv}dB", fade(tone), freq=f, level_db=lv)

# 4. level-step bursts for attack/release (program dependence): 1 kHz, -30 -> -6 -> -30 dBFS
n_seg = int(SR * 1.0)
tt = np.arange(3 * n_seg) / SR
env = np.concatenate([np.full(n_seg, db(-30)), np.full(n_seg, db(-6)), np.full(n_seg, db(-30))])
add("burst_1k_-30_-6_-30", fade(np.sin(2 * np.pi * 1000 * tt) * env), step_times=[1.0, 2.0])

# 5. two-tone IMD (60 Hz + 7 kHz, 4:1) at -12 dBFS peak
n = int(SR * 2.0)
tt = np.arange(n) / SR
imd = 0.8 * np.sin(2 * np.pi * 60 * tt) + 0.2 * np.sin(2 * np.pi * 7000 * tt)
add("imd_60_7k_-12", fade(imd * db(-12)))

# 6. closing sync click
add("sync_end", click)

out = np.concatenate(audio)
out = np.concatenate([np.zeros(int(SR * 0.5)), out])  # pre-roll
for s in segments:
    s["start"] += 0.5

sf.write("saturator_test_signal.wav", out.astype(np.float32), SR, subtype="PCM_24")
with open("saturator_test_signal.json", "w") as fh:
    json.dump({"sr": SR, "segments": segments}, fh, indent=1)
print(f"{len(out)/SR:.1f} s, {len(segments)} segments, peak {20*np.log10(np.max(np.abs(out))):.1f} dBFS")
