"""Calibrates the IRON / TAPE / DIODE output staging (IRON's makeup capped at +3 dB, see below) (TypeMap.h) so each type's
loudness follows TUBE's at every Drive setting: switching type at the same Drive stays
roughly level-matched. Loudness = BS.1770 K-weighted energy, averaged over two materials
(the synthetic demo loop at mix-bus level, and pink noise at -18 dBFS RMS).

Run from the repo root after building GlowRender:  python3 model/gen_typemap.py
The post gain sits after the nonlinear core (linear), so one pass is exact; it re-renders
once to verify."""
import subprocess, re, numpy as np, soundfile as sf
from scipy.signal import lfilter

R = "plugin/build-linux/GlowRender_artefacts/Release/GlowRender"
HDR = "plugin/Source/TypeMap.h"
T = "/tmp/claude-0/ph"
SR = 48000
DRIVES = list(range(0, 101, 5))

# BS.1770 K-weighting at 48 kHz
KB1, KA1 = [1.53512485958697, -2.69169618940638, 1.19839281085285], [1.0, -1.69065929318241, 0.73248077421585]
KB2, KA2 = [1.0, -2.0, 1.0], [1.0, -1.99004745483398, 0.99007225036621]


def loud(y):
    k = lfilter(KB2, KA2, lfilter(KB1, KA1, y, axis=0), axis=0)
    return 10 * np.log10(np.mean(k[SR // 2:] ** 2) + 1e-20)


def materials():
    loop, sr = sf.read("measure/demo_loop.wav")
    assert sr == SR
    loop = np.tile(loop, (2, 1))
    rng = np.random.default_rng(11)
    w = rng.standard_normal((SR * 8, 2))
    f = np.fft.rfftfreq(len(w), 1 / SR); f[0] = 1
    pink = np.fft.irfft(np.fft.rfft(w, axis=0) / np.sqrt(f)[:, None], axis=0)
    pink *= 10 ** (-18 / 20) / np.sqrt(np.mean(pink ** 2))
    out = []
    for i, m in enumerate((loop, pink)):
        p = f"{T}/cal{i}.wav"
        sf.write(p, m.astype(np.float32), SR, subtype="FLOAT")
        out.append(p)
    return out


def render(path, **kw):
    o = f"{T}/cal_out.wav"
    subprocess.run([R, path, o, *[f"{k}={v}" for k, v in kw.items()]], check=True, capture_output=True)
    return sf.read(o)[0]


def measure(mats, t, d):
    return np.mean([loud(render(m, type=t, drive=d, density=50, even=50, odd=50)) for m in mats])


def read_post():
    s = open(HDR).read()
    body = s[s.index("post[3][21] = {"):]
    rows = re.findall(r"\{([^{}]*)\}", body)[:3]
    return [[float(v.strip().rstrip("f")) for v in r.split(",")] for r in rows], s


def write_post(post, s):
    rows = ",\n".join("    { " + ", ".join(f"{v:.2f}f" for v in r) + " }" for r in post)
    a = s.index("post[3][21] = {")
    b = s.index("};", a)
    s = s[:a] + "post[3][21] = {\n" + rows + "\n" + s[b:]
    open(HDR, "w").write(s)


if __name__ == "__main__":
    import sys
    mats = materials()
    tube = {d: measure(mats, 0, d) for d in DRIVES}
    post, src = read_post()
    verify = "--verify" in sys.argv
    worst = 0.0
    for t in range(1, 4):
        for j, d in enumerate(DRIVES):
            delta = tube[d] - measure(mats, t, d)
            worst = max(worst, abs(delta))
            if not verify:
                post[t - 1][j] += delta
                if t == 1:
                    # IRON only compresses the lows: matching Tube's loudness would mean turning up
                    # the uncompressed highs (a tilt, and hot peaks on bright material). A real
                    # transformer leaves the highs at unity, so its makeup is capped at +3 dB.
                    pre = 24.0 * d / 100.0
                    post[t - 1][j] = min(post[t - 1][j], -pre + 3.0)
        print(f"type {t}: post = " + " ".join(f"{v:+.1f}" for v in post[t - 1]))
    if verify:
        print(f"worst loudness difference vs TUBE: {worst:.2f} dB")
    else:
        write_post(post, src)
        print("wrote", HDR)
