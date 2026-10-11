"""Fit per-drive shaper + gain staging by matching what you hear on steady tones:
fundamental gain (compression curve) and harmonic levels/polarity H2..H7,
at 100 Hz and 1 kHz, across the full 36 dB level range.

usage: python model/fit_harm.py <drive>
"""
import json
import sys
import numpy as np
import soundfile as sf
from scipy.optimize import minimize, least_squares

sys.path.insert(0, "model")
import model as M

SR = 48000
NH = 7
man = json.load(open("measure/saturator_test_signal.json"))
ref, _ = sf.read("measure/saturator_test_signal.wav")
EQ = json.load(open("model/eq_fit.json"))["eq"]
sines = [s for s in man["segments"] if s["name"].startswith("sine_")]
PAD = int(0.1 * SR)
SIM = int(0.32 * SR)          # simulate this much of each tone
WIN = (int(0.17 * SR), int(0.31 * SR))  # analyse this steady-state window


def harm(y, f0):
    seg = y[WIN[0]:WIN[1]]
    per = SR / f0
    m = int(np.floor(len(seg) / per) * per)
    seg = seg[:m]
    t = np.arange(m) / SR
    w = np.hanning(m)
    c = np.array([2 * np.sum(seg * w * np.exp(-2j * np.pi * k * f0 * t)) / np.sum(w) for k in range(1, NH + 1)])
    fund = 20 * np.log10(abs(c[0]) + 1e-15)
    rel = 20 * np.log10(np.abs(c[1:]) / (abs(c[0]) + 1e-15) + 1e-15)
    ph = np.angle(c[1:] * np.exp(-1j * np.arange(2, NH + 1) * np.angle(c[0])))
    return fund, rel, ph


def prep(y):
    data = []
    for s in sines:
        a = int(round(s["start"] * SR))
        x = ref[a - PAD:a - PAD + PAD + SIM]
        yy = y[a - PAD:a - PAD + PAD + SIM]
        data.append((s, x, harm(yy[PAD:], s["freq"])))
    return data


HW = np.array([1.0, 1.0, 0.5, 0.5, 0.3, 0.3])  # weights for H2..H7
FLOOR = -75.0


def loss_terms(p, data):
    terms = []
    for s, x, (fy, ry, py) in data:
        m = M.process(x, p, os=4)
        fm, rm, pm = harm(m[PAD:], s["freq"])
        terms.append(2.0 * (fm - fy))
        e = (np.maximum(rm, FLOOR) - np.maximum(ry, FLOOR)) * HW
        terms.extend(e.tolist())
        for k in (0, 1):  # H2, H3 polarity/shape where audible
            if ry[k] > -55:
                d = np.angle(np.exp(1j * (pm[k] - py[k])))
                terms.append(6.0 * d / np.pi)
    return np.array(terms)


KEYS = ["pre_db", "post_db", "sharp", "asym", "k2"]


def fit(d, starts):
    y, _ = sf.read(f"prints/bbtubes_drive{d}.wav")
    y = np.concatenate([y[1:, 0], [0.0]])
    data = prep(y)

    def L(v):
        p = dict(zip(KEYS, v)); p["eq"] = EQ
        if p["sharp"] < 1.2 or p["sharp"] > 40 or abs(p["asym"]) > 0.6 or abs(p["k2"]) > 1.5:
            return 1e4
        return float(np.sqrt(np.mean(loss_terms(p, data) ** 2)))

    def R(v):
        p = dict(zip(KEYS, v)); p["eq"] = EQ
        return loss_terms(p, data)

    lo = [-10, -40, 1.5, -0.4, -0.5]
    hi = [50, 15, 40, 0.4, 0.5]
    best = None
    for v0 in starts:
        v0 = np.clip(v0, np.array(lo) + 1e-3, np.array(hi) - 1e-3)
        r = least_squares(R, v0, bounds=(lo, hi), x_scale=[1, 1, 2, 0.02, 0.02], diff_step=1e-3, max_nfev=120)
        rms = float(np.sqrt(np.mean(r.fun ** 2)))
        print(f"  d{d} start {np.round(v0, 2).tolist()}: rms err {rms:.3f} dB -> {np.round(r.x, 3).tolist()}", flush=True)
        if best is None or rms < best[1]:
            best = (r.x, rms)
    p = dict(zip(KEYS, [float(v) for v in best[0]]))
    print(f"d{d}: {p} rms err {best[1]:.3f} dB", flush=True)
    return p, best[1]


if __name__ == "__main__":
    d = int(sys.argv[1])
    # starting points from the gain-structure analysis (pre gain sets where clipping starts,
    # post gain sets the clipped ceiling)
    base = {0: (1.0, -0.7), 25: (7.6, -3.2), 50: (14.8, -7.5), 75: (22.3, -10.0), 100: (33.1, -14.3)}[d]
    starts = [[base[0], base[1], sh, 0.05, -0.035] for sh in (6.0, 12.0, 20.0)]
    try:  # warm start from a previous fit
        prev = json.load(open(f"model/shaper_fit_d{d}.json"))["params"]
        if "placeholder" not in json.load(open(f"model/shaper_fit_d{d}.json")):
            starts = [[prev[k] for k in KEYS]] + starts
    except (FileNotFoundError, KeyError):
        pass
    p, err = fit(d, starts)
    json.dump({"params": p, "rms_err_db": err}, open(f"model/shaper_fit_d{d}.json", "w"), indent=1)
