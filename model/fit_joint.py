"""Joint fit: shared grid-conduction (bias shift) parameters + per-drive shaper/gain,
against harmonics of all stepped tones and the level-step burst at all 5 drive settings.

usage: python model/fit_joint.py <sgn: 1 or -1>
"""
import json
import sys
import numpy as np
import soundfile as sf
from scipy.optimize import least_squares

sys.path.insert(0, "model")
import model as M
from fit_harm import harm, prep, HW, FLOOR, PAD, EQ, KEYS, ref, man

SR = 48000
DRIVES = [0, 25, 50, 75, 100]
TAG = sys.argv[1] if len(sys.argv) > 1 else "a"
STARTS = {"a": [(-3.0, 0.3), (0.0, 0.3)], "b": [(-6.0, 0.4), (-3.0, -0.3)]}[TAG]
burst = [s for s in man["segments"] if s["name"].startswith("burst")][0]
BA = int(round(burst["start"] * SR))
BN = int(round(burst["length"] * SR))
HOP = int(0.005 * SR)


def frame_gain(y, x):
    n = (len(x) // HOP) * HOP
    yr = np.sqrt(np.mean(y[:n].reshape(-1, HOP) ** 2, axis=1)) + 1e-12
    xr = np.sqrt(np.mean(x[:n].reshape(-1, HOP) ** 2, axis=1)) + 1e-12
    return 20 * np.log10(yr / xr)


# frames to compare: right after the step up (1.0 s) and the recovery after the step down (2.0 s)
FR = np.r_[200:240, 400:560]

prints, tones, bursts = {}, {}, {}
for d in DRIVES:
    y, _ = sf.read(f"prints/bbtubes_drive{d}.wav")
    y = np.concatenate([y[1:, 0], [0.0]])
    tones[d] = prep(y)
    xb = ref[BA - PAD:BA + BN]
    bursts[d] = (xb, frame_gain(y[BA:BA + BN], ref[BA:BA + BN])[FR])

LW, BW = 2.0, 0.35
GKEYS = ["in_db", "sharp", "asym", "k2", "hp_fc", "hp_q"]
NG = len(GKEYS)


def unpack(v):
    st1 = dict(zip(GKEYS, v[:NG])); st1["amount"] = 1.0
    per = {d: dict(zip(KEYS, v[NG + 5 * i: NG + 5 + 5 * i])) for i, d in enumerate(DRIVES)}
    return st1, per


def residuals(v):
    grid, per = unpack(v)
    return np.concatenate([residuals_drive(d, grid, per) for d in DRIVES])


def residuals_drive(d, grid, per):
    out = []
    if True:
        p = dict(per[d]); p["eq"] = EQ; p["st1"] = grid
        for s, x, (fy, ry, py) in tones[d]:
            m = M.process(x, p, os=4)
            fm, rm, pm = harm(m[PAD:], s["freq"])
            out.append(LW * (fm - fy))
            out.extend(((np.maximum(rm, FLOOR) - np.maximum(ry, FLOOR)) * HW).tolist())
            for k in (0, 1):
                if ry[k] > -55:
                    out.append(6.0 * np.angle(np.exp(1j * (pm[k] - py[k]))) / np.pi)
        xb, gb = bursts[d]
        mb = M.process(xb, p, os=4)[PAD:]
        gm = frame_gain(mb, xb[PAD:])[FR]
        out.extend((BW * (gm - gb)).tolist())
    return np.array(out)


if __name__ == "__main__":
    v0 = [-3.0, 3.0, 0.3, 0.2, 1.0, 0.7]
    lo = [-15.0, 1.2, -0.6, -1.0, 0.2, 0.4]
    hi = [10.0, 30.0, 0.6, 1.0, 10.0, 2.0]
    for d in DRIVES:
        p = json.load(open(f"model/shaper_fit_d{d}.json"))["params"]
        v0 += [p[k] for k in KEYS]
        lo += [-10, -40, 1.5, -0.4, -0.5]
        hi += [50, 15, 40, 0.4, 0.5]
    v0 = np.clip(v0, np.array(lo) + 1e-3, np.array(hi) - 1e-3)
    xs = [1, 1, 0.05, 0.05, 0.3, 0.1] + [1, 1, 2, 0.02, 0.02] * len(DRIVES)
    # Jacobian sparsity: drive d's residuals only depend on the shared grid params + its own 5
    counts = []
    for d in DRIVES:
        counts.append(len(residuals_drive(d, *unpack(v0))))
    from scipy.sparse import lil_matrix
    S = lil_matrix((sum(counts), len(v0)), dtype=int)
    row = 0
    for i, n in enumerate(counts):
        S[row:row + n, :NG] = 1
        S[row:row + n, NG + 5 * i: NG + 5 + 5 * i] = 1
        row += n
    r0 = residuals(v0)
    print(f"{TAG}: start rms {np.sqrt(np.mean(r0 ** 2)):.3f}", flush=True)
    best = None
    for g0, a0 in (STARTS[0], STARTS[1]):
        v = np.array(v0); v[0] = g0; v[2] = a0
        r = least_squares(residuals, v, bounds=(lo, hi), x_scale=xs, diff_step=1e-3, max_nfev=40, jac_sparsity=S, verbose=0)
        rms = float(np.sqrt(np.mean(r.fun ** 2)))
        print(f"  start in={g0} asym={a0}: rms {rms:.3f}  st1 {np.round(r.x[:NG], 3).tolist()}", flush=True)
        if best is None or rms < best[1]:
            best = (r.x, rms)
    grid, per = unpack(best[0])
    json.dump({"rms": best[1], "st1": grid, "per": {str(k): v for k, v in per.items()}},
              open(f"model/joint_fit_{TAG}.json", "w"), indent=1)
    print("done", best[1], flush=True)
