"""Python reference model of the saturator. The C++ plugin mirrors this exactly.

Chain (per channel):
  in -> preGain -> pre-EQ (resonant HPF + low bump + presence dip)
     -> [oversampled] bias-shifted asymmetric soft clipper
     -> post DC-blocker (2nd order, sub-audio) -> postGain -> out
"""
import numpy as np
from scipy.signal import lfilter, resample_poly
from numba import njit

SR = 48000


# ---------- RBJ biquads (same formulas as the C++ Biquad class) ----------
def biquad_hpf(fc, q, sr=SR):
    w = 2 * np.pi * fc / sr
    al = np.sin(w) / (2 * q)
    c = np.cos(w)
    b = np.array([(1 + c) / 2, -(1 + c), (1 + c) / 2])
    a = np.array([1 + al, -2 * c, 1 - al])
    return b / a[0], a / a[0]


def onepole_hpf(fc, sr=SR):
    """Bilinear 1st-order high-pass (coupling cap)."""
    k = np.tan(np.pi * fc / sr)
    b = np.array([1.0, -1.0]) / (1 + k)
    a = np.array([1.0, (k - 1) / (1 + k)])
    return b, a


def biquad_peak(fc, gain_db, q, sr=SR):
    A = 10 ** (gain_db / 40)
    w = 2 * np.pi * fc / sr
    al = np.sin(w) / (2 * q)
    c = np.cos(w)
    b = np.array([1 + al * A, -2 * c, 1 - al * A])
    a = np.array([1 + al / A, -2 * c, 1 - al / A])
    return b / a[0], a / a[0]


def biquad_lowshelf(fc, gain_db, q, sr=SR):
    A = 10 ** (gain_db / 40)
    w = 2 * np.pi * fc / sr
    al = np.sin(w) / (2 * q)
    c = np.cos(w)
    sA = 2 * np.sqrt(A) * al
    b = np.array([A * ((A + 1) - (A - 1) * c + sA), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sA)])
    a = np.array([(A + 1) + (A - 1) * c + sA, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sA])
    return b / a[0], a / a[0]


# ---------- default (fitted) parameters ----------
EQ = dict(hp1_fc=10.0, hp_fc=32.0, hp_q=0.9, bump_fc=90.0, bump_db=1.4, bump_q=0.7, dip_fc=2200.0, dip_db=-0.25, dip_q=0.8)
POST = dict(dc_fc=1.5, dc_q=0.9)


def pre_eq(x, eq=EQ, sr=SR):
    for b, a in (onepole_hpf(eq["hp1_fc"], sr), biquad_hpf(eq["hp_fc"], eq["hp_q"], sr),
                 biquad_peak(eq["bump_fc"], eq["bump_db"], eq["bump_q"], sr),
                 biquad_peak(eq["dip_fc"], eq["dip_db"], eq["dip_q"], sr)):
        x = lfilter(b, a, x)
    return x


def shaper(u, sharp, asym, k2):
    """Asymmetric soft clipper.
    k2   : quadratic (even-order) term applied before clipping - small-signal 2nd harmonic
    asym : negative half clips at (1+asym), positive at (1-asym)  - large-signal even harmonics / DC
    sharp: knee hardness (2 = soft, 8+ = near hard clip)
    """
    # bounded even-order term: ~k2*u^2 for small signals (no u^4 term, so no stray
    # 4th harmonic), saturates to +k2 for large ones (can never fold back at high drive)
    uu = u * u
    v = u + k2 * uu / np.sqrt(1.0 + uu * uu)
    ceil = np.where(v >= 0, 1.0 - asym, 1.0 + asym)
    r = v / ceil
    return ceil * r / np.power(1.0 + np.power(np.abs(r), sharp), 1.0 / sharp)


@njit(cache=True)
def shaper_grid(u, sharp, asym, k2, g, att, rel, sgn):
    """Shaper with grid-conduction bias shift (sample loop, oversampled rate).
    When the signal pushes past the conduction point g (on the side given by sgn),
    the coupling cap charges (rate att) and shifts the operating point by b;
    b bleeds off with per-sample factor rel. Louder => more lopsided clipping,
    and after a loud passage the bias recovers over a few hundred ms ("breathing").
    """
    out = np.empty_like(u)
    b = 0.0
    for i in range(u.shape[0]):
        x = u[i]
        xx = x * x
        v = x + k2 * xx / np.sqrt(1.0 + xx * xx) - b
        ceil = 1.0 - asym if v >= 0.0 else 1.0 + asym
        r = v / ceil
        ar = abs(r)
        if ar < 1e-9:
            y = v
        elif ar <= 1.0:
            y = ceil * r * (1.0 + ar ** sharp) ** (-1.0 / sharp)
        else:
            y = ceil * (1.0 if r > 0 else -1.0) * (1.0 + ar ** (-sharp)) ** (-1.0 / sharp)
        out[i] = y
        e = sgn * v - g
        if e > 0.0:
            b += sgn * att * e
        b *= rel
    return out


GRID = dict(g=0.8, att=2000.0, tau=0.3, sgn=1.0, amount=0.0)  # grid model off (superseded by stage1)


def run_shaper(u, p, rate):
    gp = p.get("grid", GRID)
    att = gp["amount"] * gp["att"] / rate
    rel = np.exp(-1.0 / (gp["tau"] * rate))
    return shaper_grid(np.ascontiguousarray(u, dtype=np.float64), float(p["sharp"]), float(p["asym"]),
                       float(p["k2"]), float(gp["g"]), float(att), float(rel), float(gp["sgn"]))


ST1 = None  # input stage off unless given: dict(in_db, sharp, asym, k2, hp_fc, hp_q, amount)


def stage1(u, st, rate):
    """Input tube stage: unity gain for small signals, bends lopsidedly near its
    threshold (in_db above 0 dBFS), then a coupling cap (HPF) into the drive stage.
    Its DC shift is what makes the main clipper go lopsided at hot input levels,
    and the cap recovering is the ~0.5 s 'breathing' after loud passages."""
    a = st.get("amount", 1.0)
    if a <= 0:
        return u
    k = 10 ** (-(st["in_db"]) / 20) * a  # scale into the stage
    w = u * k
    ww = w * w
    w = w + st["k2"] * ww / np.sqrt(1.0 + ww * ww)
    ceil = np.where(w >= 0, 1.0 - st["asym"], 1.0 + st["asym"])
    r = w / ceil
    ar = np.abs(r)
    y = ceil * np.sign(r) * np.where(ar <= 1.0, ar * (1.0 + ar ** st["sharp"]) ** (-1.0 / st["sharp"]),
                                     (1.0 + np.maximum(ar, 1e-12) ** (-st["sharp"])) ** (-1.0 / st["sharp"]))
    b, aa = biquad_hpf(st["hp_fc"], st["hp_q"], rate)
    return lfilter(b, aa, y) / k


def process(x, p, os=4, sr=SR):
    """p: dict with pre_db, post_db, sharp, asym, k2 (+ optional eq/post/st1 overrides)."""
    g = 10 ** (p["pre_db"] / 20)
    u = pre_eq(x, p.get("eq", EQ), sr)
    st = p.get("st1", ST1)
    if os > 1:
        uo = resample_poly(u, os, 1, window=("kaiser", 10.0))
        if st:
            uo = stage1(uo, st, sr * os)
        yo = run_shaper(uo * g, p, sr * os)
        y = resample_poly(yo, 1, os, window=("kaiser", 10.0))[: len(x)]
    else:
        if st:
            u = stage1(u, st, sr)
        y = run_shaper(u * g, p, sr)
    post = p.get("post", POST)
    b, a = biquad_hpf(post["dc_fc"], post["dc_q"], sr)
    y = lfilter(b, a, y)
    return y * 10 ** (p["post_db"] / 20)
