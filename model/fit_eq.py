"""Fit the linear pre-EQ to the low-drive prints (magnitude from the sweep envelope,
phase from the stepped sines)."""
import json
import sys
import numpy as np
import soundfile as sf
from scipy.signal import hilbert, freqz
from scipy.optimize import minimize

sys.path.insert(0, "model")
import model as M

SR = 48000
man = json.load(open("measure/saturator_test_signal.json"))
segs = {s["name"]: s for s in man["segments"]}
ref, _ = sf.read("measure/saturator_test_signal.wav")


def load(d):
    y, _ = sf.read(f"prints/bbtubes_drive{d}.wav")
    return y[1:, 0]


def sweep_mag(y):
    s = segs["sweep_-18"]
    a = int(s["start"] * SR); n = int(s["length"] * SR)
    T = s["length"]; L = T / np.log(1000)
    ex = np.abs(hilbert(ref[a:a + n])); ey = np.abs(hilbert(y[a:a + n]))
    fs = np.geomspace(28, 19000, 80)
    m = []
    for f in fs:
        i = int(L * np.log(f / 20) * SR); w = slice(i - 200, i + 200)
        m.append(20 * np.log10(np.mean(ey[w]) / np.mean(ex[w])))
    return fs, np.array(m)


def fund_phase(sig, f, a, n):
    t = np.arange(n) / SR
    return np.angle(np.sum(sig[a:a + n] * np.exp(-2j * np.pi * f * t)))


def sine_phase(y, f, lv=-24):
    s = segs[f"sine_{int(f)}Hz_{lv}dB"]
    a = int(s["start"] * SR) + 2400
    n = int(int(0.4 * SR) // (SR / f) * (SR / f))
    return np.angle(np.exp(1j * (fund_phase(y, f, a, n) - fund_phase(ref, f, a, n))))


y0 = load(0)
fs, mag = sweep_mag(y0)
ph100 = sine_phase(y0, 100.0)
ph1k = sine_phase(y0, 1000.0)
print("measured phase 100 Hz %.1f deg, 1 kHz %.1f deg" % (np.degrees(ph100), np.degrees(ph1k)))
i1k = np.argmin(np.abs(fs - 1000))

KEYS = ["hp1_fc", "hp_fc", "hp_q", "bump_fc", "bump_db", "bump_q", "dip_fc", "dip_db", "dip_q"]


def response(eq, f):
    H = np.ones(len(f), complex)
    for b, a in (M.onepole_hpf(eq["hp1_fc"]), M.biquad_hpf(eq["hp_fc"], eq["hp_q"]), M.biquad_peak(eq["bump_fc"], eq["bump_db"], eq["bump_q"]),
                 M.biquad_peak(eq["dip_fc"], eq["dip_db"], eq["dip_q"])):
        H *= freqz(b, a, worN=f, fs=SR)[1]
    return H


def loss(v):
    eq = dict(zip(KEYS, v))
    if eq["hp_q"] <= 0.3 or eq["bump_q"] <= 0.2 or eq["dip_q"] <= 0.2 or eq["hp_fc"] < 5 or eq["hp1_fc"] < 1 or eq["hp1_fc"] > 200:
        return 1e9
    H = response(eq, np.r_[fs, 100.0, 1000.0])
    m = 20 * np.log10(np.abs(H[:-2]))
    e = np.sum(((m - m[i1k]) - (mag - mag[i1k])) ** 2)
    # phase: device also has 1 sample (removed) + linear-phase filters; compare 100 Hz relative to 1 kHz
    dph = np.angle(H[-2]) - np.angle(H[-1])
    e += 4 * (np.angle(np.exp(1j * (dph - (ph100 - ph1k))))) ** 2
    return e


x0 = [M.EQ[k] for k in KEYS]
best = None
for h1 in (5, 15, 40):
  for hp in (20, 30, 40):
    for bf in (70, 100, 140):
        v = list(x0); v[0] = h1; v[1] = hp; v[3] = bf
        r = minimize(loss, v, method="Nelder-Mead", options=dict(maxiter=8000, xatol=1e-4, fatol=1e-7))
        if best is None or r.fun < best.fun:
            best = r
eq = dict(zip(KEYS, [float(v) for v in best.x]))
H = response(eq, np.r_[fs, 100.0, 1000.0])
m = 20 * np.log10(np.abs(H[:-2]))
print("loss", best.fun)
print("max mag err dB", np.max(np.abs((m - m[i1k]) - (mag - mag[i1k]))))
print("model phase 100 vs 1k %.1f deg, measured %.1f" % (np.degrees(np.angle(H[-2]) - np.angle(H[-1])), np.degrees(ph100 - ph1k)))
print("1 kHz small-signal level offset to absorb in gain: %.2f dB (measured %.2f)" % (m[i1k], mag[i1k]))
json.dump({"eq": eq, "lin_gain_1k_db": float(mag[i1k] - m[i1k])}, open("model/eq_fit.json", "w"), indent=1)
print(json.dumps(eq, indent=1))
