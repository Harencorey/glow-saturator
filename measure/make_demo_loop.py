"""A short synthetic loop (kick, snare, hats, bass, chord pad) at mix-bus level,
so the plugin can be heard on something musical without needing session files."""
import numpy as np
import soundfile as sf

SR = 48000
BPM = 96
beat = 60 / BPM
bars = 4
n = int(SR * beat * 4 * bars)
t = np.arange(n) / SR
rng = np.random.default_rng(7)
mix = np.zeros((n, 2))


def place(sig, at, gain=1.0, pan=0.0):
    i = int(at * SR)
    j = min(n, i + len(sig))
    l, r = np.cos((pan + 1) * np.pi / 4), np.sin((pan + 1) * np.pi / 4)
    mix[i:j, 0] += sig[: j - i] * gain * l * 1.414
    mix[i:j, 1] += sig[: j - i] * gain * r * 1.414


def kick():
    tt = np.arange(int(0.45 * SR)) / SR
    f = 45 + 110 * np.exp(-tt * 28)
    return np.sin(2 * np.pi * np.cumsum(f) / SR) * np.exp(-tt * 7) + 0.3 * np.exp(-tt * 300) * rng.standard_normal(len(tt))


def snare():
    tt = np.arange(int(0.3 * SR)) / SR
    body = np.sin(2 * np.pi * 185 * tt) * np.exp(-tt * 25)
    noise = rng.standard_normal(len(tt)) * np.exp(-tt * 14)
    noise = np.convolve(noise, [1, -0.6], "same")
    return 0.6 * body + 0.5 * noise


def hat():
    tt = np.arange(int(0.08 * SR)) / SR
    x = rng.standard_normal(len(tt))
    x = x - np.convolve(x, np.ones(4) / 4, "same")
    return x * np.exp(-tt * 60)


for bar in range(bars):
    for b in range(4):
        at = (bar * 4 + b) * beat
        if b in (0, 2) or (b == 3 and bar % 2):
            place(kick(), at + (beat / 2 if b == 3 else 0), 0.9)
        if b in (1, 3):
            place(snare(), at, 0.55, 0.05)
        for h in range(2):
            place(hat(), at + h * beat / 2, 0.18 if h else 0.25, 0.4)

# bass: root notes, saw-ish with lowpass feel
roots = [41.2, 41.2, 49.0, 36.7]  # E1 E1 G1 D1
bass = np.zeros(n)
for bar, f in enumerate(roots):
    for k in range(8):
        i0 = int((bar * 4 * beat + k * beat / 2) * SR)
        L = int(beat / 2 * SR * 0.9)
        tt = np.arange(L) / SR
        s = sum(np.sin(2 * np.pi * f * h * tt) / h for h in range(1, 8))
        bass[i0:i0 + L] += s * np.exp(-tt * 3) * 0.5
mix[:, 0] += bass * 0.55
mix[:, 1] += bass * 0.55

# pad: triads with slow attack
chords = [[164.8, 196.0, 246.9], [164.8, 207.7, 246.9], [196.0, 246.9, 293.7], [146.8, 185.0, 220.0]]
for bar, ch in enumerate(chords):
    i0 = int(bar * 4 * beat * SR)
    L = int(4 * beat * SR)
    tt = np.arange(L) / SR
    env = np.minimum(tt / 0.4, 1) * np.exp(-tt * 0.3)
    for k, f in enumerate(ch):
        for det, pan in ((0.998, -0.5), (1.002, 0.5)):
            s = np.sin(2 * np.pi * f * det * tt) + 0.3 * np.sin(2 * np.pi * 2 * f * det * tt)
            place(s * env * 0.06, bar * 4 * beat, 1.0, pan)

mix *= 10 ** (-1.0 / 20) / np.max(np.abs(mix))  # peak -1 dBFS, like a hot mix bus
sf.write("measure/demo_loop.wav", mix.astype(np.float32), SR, subtype="PCM_24")
print(f"{n / SR:.1f} s, rms {20 * np.log10(np.sqrt(np.mean(mix ** 2))):.1f} dBFS")
