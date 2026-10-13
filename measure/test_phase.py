"""L/R and dry/wet phase audit (via GlowRender, stereo files)."""
import subprocess, numpy as np, soundfile as sf
R = "plugin/build-linux/GlowRender_artefacts/Release/GlowRender"
T = "/tmp/claude-0/ph"
SR = 48000


def render(x, **kw):
    sf.write(f"{T}/ph_in.wav", np.asarray(x, np.float32), SR, subtype="FLOAT")
    subprocess.run([R, f"{T}/ph_in.wav", f"{T}/ph_out.wav", *[f"{k}={v}" for k, v in kw.items()]], check=True, capture_output=True)
    return sf.read(f"{T}/ph_out.wav")[0]


def db(x): return 20 * np.log10(np.max(np.abs(x)) + 1e-30)


rng = np.random.default_rng(5)
mono = (rng.standard_normal(SR * 2) * 0.25).clip(-1, 1)
settings = {
    "defaults": {},
    "drive 90 / even 100 / odd 20 / density 100": dict(drive=90, even=100, odd=20, density=100),
    "hpf + pre-EQ": dict(drive=60, hpf=120, peq=1, peqHG=6, peqMG=-4),
    "dry+wet with EQ + comps": dict(drive=70, dryLevel=-3, dryEq=1, dryG2=5, wetEq=1, wetG3=-4, dryComp=1, wetComp=1,
                                    dryIn=12, wetIn=18, wetRatio=7, wetColour=1, dryColour=2),
}
print("1) identical L and R in -> L minus R out (must be silent):")
for name, kw in settings.items():
    y = render(np.stack([mono, mono], 1), **kw)
    print(f"   {name:45s} L-R peak {db(y[:, 0] - y[:, 1]):7.1f} dBFS  (signal {db(y[:, 0]):5.1f} dBFS)")

print("2) signal on L only, silence on R -> anything leaking to R?")
for name, kw in settings.items():
    y = render(np.stack([mono, np.zeros_like(mono)], 1), **kw)
    print(f"   {name:45s} R peak {db(y[:, 1]):7.1f} dBFS")

print("3) dry vs wet phase (Drive 0, low level), summed 50/50 at equal level:")
for f in (20, 30, 40, 60, 100, 200, 500, 1000, 5000, 10000, 16000):
    x = 10 ** (-30 / 20) * np.sin(2 * np.pi * f * np.arange(SR * 2) / SR)
    seg = slice(SR, 2 * SR)
    t = np.arange(SR) / SR
    def ph(y): return np.sum(y[seg] * np.exp(-2j * np.pi * f * t))
    w = render(x, drive=0, density=0)
    d = render(x, wetLevel=-60, dryLevel=0)
    pw, pd = ph(w), ph(d)
    dphi = np.degrees(np.angle(pw / pd))
    s = abs(pw + pd * abs(pw) / abs(pd)) / (2 * abs(pw))  # sum at matched level vs perfect
    print(f"   {f:6d} Hz: wet-dry phase {dphi:+7.1f} deg -> parallel sum loses {20 * np.log10(s):6.2f} dB")
