"""Switching/automation test: filters and comps switched in and out mid-stream.
For a steady sine through each path (solo wet / solo dry, both processed), checks
  1) clicks: broadband energy away from the sine (above 3x f0, or below f0/3 for high f0),
     peak during switching vs. the steady-state floor. (Phase rotating smoothly at the
     sine's frequency is a legitimate filter change, not a click, so it's not counted.)
  2) phase lock: the wet-vs-dry phase difference at the sine frequency over the whole render,
     including the 20-60 ms transitions."""
import subprocess, numpy as np, soundfile as sf
from scipy.signal import butter, sosfiltfilt

R = "plugin/build-linux/GlowRender_artefacts/Release/GlowRender"
T = "/tmp/claude-0/ph"
SR = 48000

SCEN = {
    "HPF in/out + sweeps": (150, ["at=0.5", "hpf=20", "at=1.0", "hpf=200", "at=1.5", "hpf=19", "at=2.0", "hpf=400", "at=2.5", "hpf=19"]),
    "pre-EQ + Link, HPF slope, Keep lows": (5000, ["peq=1", "at=0.5", "peqHG=8", "at=1.0", "peqLink=1", "at=1.5", "peqHG=0", "peqMF=4000", "peqMG=-9",
                                                  "at=2.0", "peqLink=0", "at=2.5", "peqMG=0"]),
    "Keep lows on/off @ 80 Hz": (80, ["hpf=150", "at=0.5", "keep=1", "at=1.0", "keep=0", "at=1.5", "keep=1", "hpf24=0", "at=2.0", "hpf=19", "at=2.5", "hpf=200"]),
    "Mix moves 100 -> 30 -> 70 %": (1000, ["xmix=100", "peq=1", "peqMG=6", "at=0.5", "xmix=30", "at=1.0", "xmix=70",
                                          "at=1.5", "xmix=45", "at=2.0", "xmix=60"]),
    "master EQ / comp on/off + order": (1000, ["masterF2=1000", "masterG2=8", "masterIn=12", "masterRatio=4",
                                               "at=0.5", "masterEq=1", "at=1.0", "masterComp=1", "at=1.5", "masterOrder=1",
                                               "at=2.0", "masterEq=0", "at=2.5", "masterComp=0"]),
}


def render(x, args):
    sf.write(f"{T}/sw_in.wav", np.stack([x, x], 1).astype(np.float32), SR, subtype="FLOAT")
    subprocess.run([R, f"{T}/sw_in.wav", f"{T}/sw_out.wav", *args], check=True, capture_output=True)
    return sf.read(f"{T}/sw_out.wav")[0][:, 0]


def demod(y, f0):
    t = np.arange(len(y)) / SR
    sos = butter(4, 40, fs=SR, output="sos")
    z = y * np.exp(-2j * np.pi * f0 * t) * 2
    zl = sosfiltfilt(sos, z.real) + 1j * sosfiltfilt(sos, z.imag)
    if f0 <= 2000: sos2 = butter(8, 3 * f0, "highpass", fs=SR, output="sos")
    else:          sos2 = butter(8, f0 / 3, "lowpass", fs=SR, output="sos")
    return zl, sosfiltfilt(sos2, y)


worst_click, worst_phase, worst_loss = -999, 0, 0
for name, (f0, args) in SCEN.items():
    t = np.arange(SR * 3) / SR
    x = 10 ** (-24 / 20) * np.sin(2 * np.pi * f0 * t)
    base = ["drive=0", "density=0", "dryLevel=0"]
    w = render(x, base + ["solo=1"] + args)
    d = render(x, base + ["solo=2"] + args)
    zw, rw = demod(w, f0)
    zd, rd = demod(d, f0)
    core = slice(int(0.2 * SR), int(2.9 * SR))   # skip the demod filter's edges
    amp = 10 ** (-24 / 20)
    rep = []
    for lab, r in (("wet", rw), ("dry", rd)):
        floor = 20 * np.log10(np.percentile(np.abs(r[core]), 50) / amp + 1e-12)
        peak = 20 * np.log10(np.max(np.abs(r[core])) / amp + 1e-12)
        worst_click = max(worst_click, peak)
        rep.append(f"{lab} residual peak {peak:6.1f} dB (floor {floor:6.1f})")
    # phase is only meaningful where both paths carry the tone (a band centre passes through
    # zero for a moment while it switches - on both paths at once)
    ok = (np.abs(zw[core]) > 0.1 * np.median(np.abs(zw[core]))) & (np.abs(zd[core]) > 0.1 * np.median(np.abs(zd[core])))
    dphi = np.degrees(np.angle(zw[core][ok] / zd[core][ok]))
    worst_phase = max(worst_phase, np.max(np.abs(dphi)))
    # what matters: how much the actual wet+dry sum falls short of a perfect in-phase sum
    # (only where both paths carry the tone - at a band centre's momentary zero both paths are
    # silent together, so there's nothing to lose)
    a, b = zw[core][ok], zd[core][ok]
    loss = 20 * np.log10(np.abs(a + b) / (np.abs(a) + np.abs(b) + 1e-15))
    worst_loss = min(worst_loss, loss.min())
    print(f"{name:32s} @{f0:5d} Hz  phase max {np.max(np.abs(dphi)):5.2f} deg, sum loss worst {loss.min():6.3f} dB | " + " | ".join(rep))
print(f"\nworst click residual {worst_click:.1f} dB re. signal; worst wet/dry phase {worst_phase:.2f} deg; "
      f"worst parallel-sum loss at any moment {worst_loss:.3f} dB")
