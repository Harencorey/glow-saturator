"""Refine each drive's 5 params with the shared input stage held fixed."""
import json, sys
import numpy as np
from scipy.optimize import least_squares
sys.argv = [sys.argv[0], "b", sys.argv[1]]
d = int(sys.argv[2])
sys.argv = sys.argv[:2]
sys.path.insert(0, "model")
import fit_joint as J
J.LW, J.BW = 6.0, 0.12

j = json.load(open("model/joint_fit_b.json"))
st1 = j["st1"]
v0 = np.array([j["per"][str(d)][k] for k in J.KEYS])
lo = [-10, -40, 1.5, -0.4, -0.5]; hi = [50, 15, 40, 0.4, 0.5]


def R(v):
    return J.residuals_drive(d, st1, {d: dict(zip(J.KEYS, v))})


r0 = R(v0)
# analysis-based gain staging: small-signal gain at 1 kHz and clipped ceiling from the prints
base = {0: (1.0, -0.7), 25: (7.6, -3.2), 50: (14.8, -7.5), 75: (22.3, -10.0), 100: (33.1, -14.3)}[d]
starts = [v0]
for dp in (-2.0, 0.0, 2.0):
    for sh in (5.0, 15.0):
        starts.append(np.array([base[0] + dp, base[1] - 0.4 * dp, sh, v0[3], v0[4]]))
best = None
for st in starts:
    r = least_squares(R, np.clip(st, np.array(lo) + 1e-3, np.array(hi) - 1e-3), bounds=(lo, hi),
                      x_scale=[1, 1, 2, 0.02, 0.02], diff_step=1e-2, max_nfev=60)
    e = np.sqrt(np.mean(r.fun ** 2))
    if best is None or e < best[0]:
        best = (e, r.x)
print(d, "rms", np.sqrt(np.mean(r0 ** 2)), "->", best[0], np.round(best[1], 3).tolist(), flush=True)
json.dump(dict(zip(J.KEYS, [float(x) for x in best[1]])), open(f"model/polish_d{d}.json", "w"))
