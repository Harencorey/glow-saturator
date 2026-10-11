"""Analyze prints of the measurement signal.

usage: python analyze.py manifest.json print1.wav [print2.wav ...] > results.json
Works for prints from the reference plugin and from our own prototype.
"""
import json
import sys
import numpy as np
import soundfile as sf

OFFSET = 1  # samples of latency measured from the sync click


def harmonics(y, f0, sr, nh=9):
    """Return (fund_db, [h2..h_nh dB rel fund], [h2..h phase rel fund], dc)."""
    n = len(y)
    # trim to whole periods, skip fades
    skip = int(0.05 * sr)
    y = y[skip:n - skip]
    per = sr / f0
    m = int(np.floor(len(y) / per) * per)
    y = y[:m]
    t = np.arange(m) / sr
    w = np.hanning(m)
    amps, phs = [], []
    for k in range(1, nh + 1):
        ph = np.exp(-2j * np.pi * k * f0 * t)
        c = 2 * np.sum(y * w * ph) / np.sum(w)
        amps.append(np.abs(c))
        phs.append(np.angle(c))
    amps = np.array(amps)
    phs = np.array(phs)
    fund = 20 * np.log10(amps[0] + 1e-15)
    rel = 20 * np.log10(amps[1:] / (amps[0] + 1e-15) + 1e-15)
    # phase of harmonic k relative to k * fundamental phase (shape-invariant)
    relph = np.angle(np.exp(1j * (phs[1:] - np.arange(2, nh + 1) * phs[0])))
    thd = 20 * np.log10(np.sqrt(np.sum(amps[1:] ** 2)) / (amps[0] + 1e-15) + 1e-15)
    return fund, rel.tolist(), relph.tolist(), float(np.mean(y)), thd


def sweep_response(y, seg, sr):
    """Linear frequency response via Farina inverse filter (linear IR only)."""
    f1, f2 = seg["f1"], seg["f2"]
    T = seg["length"]
    n = int(round(T * sr))
    t = np.arange(n) / sr
    L = T / np.log(f2 / f1)
    x = np.sin(2 * np.pi * f1 * L * (np.exp(t / L) - 1))
    inv = x[::-1] * np.exp(-t / L)
    N = 1 << int(np.ceil(np.log2(len(y) + n)))
    h = np.fft.irfft(np.fft.rfft(y, N) * np.fft.rfft(inv, N), N)
    pk = np.argmax(np.abs(h))
    lin = h[pk - 2048: pk + 8192] * np.hanning(10240)
    H = np.fft.rfft(lin, 1 << 16)
    f = np.fft.rfftfreq(1 << 16, 1 / sr)
    mag = 20 * np.log10(np.abs(H) + 1e-15)
    pts = [30, 50, 100, 200, 500, 1000, 2000, 4000, 8000, 12000, 16000, 20000]
    i1k = np.argmin(np.abs(f - 1000))
    return {str(p): round(float(mag[np.argmin(np.abs(f - p))] - mag[i1k]), 2) for p in pts}


def burst_envelope(y, x, sr):
    """Gain (out/in, dB) over time in 5 ms frames around the level steps."""
    hop = int(0.005 * sr)
    g = []
    for i in range(0, len(x) - hop, hop):
        xi = np.sqrt(np.mean(x[i:i + hop] ** 2)) + 1e-12
        yi = np.sqrt(np.mean(y[i:i + hop] ** 2)) + 1e-12
        g.append(20 * np.log10(yi / xi))
    return np.array(g)


def main():
    man = json.load(open(sys.argv[1]))
    sr = man["sr"]
    ref, _ = sf.read(man.get("ref", "measure/saturator_test_signal.wav"))
    out = {}
    for path in sys.argv[2:]:
        y, s = sf.read(path)
        assert s == sr
        if y.ndim > 1:
            y = y[:, 0]
        y = np.concatenate([y[OFFSET:], np.zeros(OFFSET)])
        r = {"sines": {}, "sweep": None}
        for seg in man["segments"]:
            a = int(round(seg["start"] * sr))
            b = a + int(round(seg["length"] * sr))
            ys = y[a:b]
            if seg["name"].startswith("sine_"):
                fund, rel, ph, dc, thd = harmonics(ys, seg["freq"], sr)
                r["sines"][seg["name"]] = {
                    "in_db": seg["level_db"], "freq": seg["freq"],
                    "out_db": round(fund, 2), "gain_db": round(fund - seg["level_db"], 2),
                    "h_db": [round(v, 1) for v in rel], "h_ph": [round(v, 2) for v in ph],
                    "thd_db": round(thd, 1), "dc": dc}
            elif seg["name"].startswith("sweep"):
                r["sweep"] = sweep_response(ys, seg, sr)
            elif seg["name"].startswith("burst"):
                g = burst_envelope(ys, ref[a:b], sr)
                r["burst_gain_db"] = [round(v, 2) for v in g]
        out[path] = r
    json.dump(out, sys.stdout, indent=1)


if __name__ == "__main__":
    main()
