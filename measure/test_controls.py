"""Check the Even/Odd/HPF/shelf controls do what they claim (via GlowRender)."""
import subprocess, numpy as np, soundfile as sf
R = "plugin/build-linux/GlowRender_artefacts/Release/GlowRender"
T = "/tmp/claude-0/ph"
SR = 48000


def render(x, **kw):
    sf.write(f"{T}/tc_in.wav", x.astype(np.float32), SR, subtype="FLOAT")
    subprocess.run([R, f"{T}/tc_in.wav", f"{T}/tc_out.wav", *[f"{k}={v}" for k, v in kw.items()]], check=True, capture_output=True)
    return sf.read(f"{T}/tc_out.wav")[0]


def harmonics(y, f0, nh=6):
    seg = y[SR // 2: SR // 2 + SR // 2]
    t = np.arange(len(seg)) / SR
    w = np.hanning(len(seg))
    c = [abs(2 * np.sum(seg * w * np.exp(-2j * np.pi * k * f0 * t)) / np.sum(w)) for k in range(1, nh + 1)]
    return 20 * np.log10(c[0]), [20 * np.log10(v / c[0] + 1e-12) for v in c[1:]]


f0 = 200.0
x = 10 ** (-12 / 20) * np.sin(2 * np.pi * f0 * np.arange(SR) / SR)
print("200 Hz @ -12 dBFS, Drive 50            fund    H2     H3     H4     H5")
for label, kw in [("defaults (even 50 / odd 50)", {}), ("even 0", dict(even=0)), ("even 100", dict(even=100)),
                  ("odd 0", dict(odd=0)), ("odd 100", dict(odd=100)), ("even 100 + odd 0", dict(even=100, odd=0))]:
    fu, h = harmonics(render(x, drive=50, **kw), f0)
    print(f"  {label:30s} {fu:6.1f} {h[0]:6.1f} {h[1]:6.1f} {h[2]:6.1f} {h[3]:6.1f}")

# EQ checks at Drive 0 / -30 dBFS (nearly linear), relative to the plain response
def resp(**kw):
    out = {}
    for f in (40, 80, 160, 1000, 4000, 8000, 16000):
        xx = 10 ** (-30 / 20) * np.sin(2 * np.pi * f * np.arange(SR) / SR)
        out[f] = harmonics(render(xx, drive=0, density=0, **kw), f)[0]
    return out

base = resp()
for label, kw in [("HPF 160 Hz", dict(hpf=160)), ("pre-EQ high shelf 4k +6 dB", dict(peq=1, peqHF=4000, peqHG=6)),
                  ("pre-EQ mid 1k -6 dB", dict(peq=1, peqMF=1000, peqMG=-6))]:
    r = resp(**kw)
    print(f"{label:22s} " + "  ".join(f"{f}:{r[f] - base[f]:+5.1f}" for f in r))
