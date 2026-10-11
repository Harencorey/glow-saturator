"""Render the test signal through the C++ plugin DSP at each measured drive setting
and compare against the BB Tubes prints. Writes renders/, measure/compare.json and
measure/compare_*.png.
"""
import json
import subprocess
import sys
import numpy as np
import soundfile as sf
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

sys.path.insert(0, "model")
from fit_harm import harm, PAD, SIM  # same analysis the fit used

SR = 48000
RENDER = "plugin/build-linux/GlowRender_artefacts/Release/GlowRender"
man = json.load(open("measure/saturator_test_signal.json"))
sines = [s for s in man["segments"] if s["name"].startswith("sine_")]
DRIVES = [0, 25, 50, 75, 100]


def tone_table(y):
    out = {}
    for s in sines:
        a = int(round(s["start"] * SR))
        f, r, p = harm(y[a:a + SIM], s["freq"])
        out[s["name"]] = (s["freq"], s["level_db"], f, r, p)
    return out


summary = {}
fig, axes = plt.subplots(len(DRIVES), 3, figsize=(16, 3.2 * len(DRIVES)))
for i, d in enumerate(DRIVES):
    out = f"renders/glow_testsignal_drive{d}.wav"
    subprocess.run([RENDER, "measure/saturator_test_signal.wav", out, f"drive={d}"], check=True,
                   capture_output=True)
    g, _ = sf.read(out)
    b, _ = sf.read(f"prints/bbtubes_drive{d}.wav")
    b = np.concatenate([b[1:, 0], [0.0]])[: len(g)]
    G, B = tone_table(g), tone_table(b)
    gain_err, h_err = [], []
    for k in G:
        gain_err.append(G[k][2] - B[k][2])
        for h in range(3):
            if B[k][3][h] > -60 or G[k][3][h] > -60:
                h_err.append(max(G[k][3][h], -80) - max(B[k][3][h], -80))
    summary[d] = {"gain_err_db_max": float(np.max(np.abs(gain_err))), "gain_err_db_rms": float(np.sqrt(np.mean(np.square(gain_err)))),
                  "h2_h4_err_db_rms": float(np.sqrt(np.mean(np.square(h_err)))) if h_err else 0.0}
    for j, f0 in enumerate((100.0, 1000.0)):
        ax = axes[i, j]
        ks = [k for k in G if G[k][0] == f0]
        lv = [G[k][1] for k in ks]
        for h, col in ((0, "C0"), (1, "C1"), (2, "C2")):
            ax.plot(lv, [B[k][3][h] for k in ks], "-", color=col, lw=2, alpha=.5, label=f"BB H{h + 2}")
            ax.plot(lv, [G[k][3][h] for k in ks], "--", color=col, lw=1.3, label=f"Glow H{h + 2}")
        ax.set_ylim(-100, 0); ax.set_title(f"drive {d}: harmonics @ {int(f0)} Hz (dBc)"); ax.grid(alpha=.3)
        if i == 0 and j == 0: ax.legend(fontsize=7, ncol=2)
    ax = axes[i, 2]
    for f0, col in ((100.0, "C3"), (1000.0, "C4")):
        ks = [k for k in G if G[k][0] == f0]
        ax.plot([B[k][1] for k in ks], [B[k][2] for k in ks], "-", color=col, lw=2, alpha=.5, label=f"BB {int(f0)}")
        ax.plot([G[k][1] for k in ks], [G[k][2] for k in ks], "--", color=col, lw=1.3, label=f"Glow {int(f0)}")
    ax.set_title(f"drive {d}: output level vs input (dBFS)"); ax.grid(alpha=.3); ax.legend(fontsize=7)
plt.tight_layout()
plt.savefig("measure/compare_harmonics.png", dpi=72)
json.dump(summary, open("measure/compare.json", "w"), indent=1)
for d, s in summary.items():
    print(f"drive {d:3d}: level err rms {s['gain_err_db_rms']:.2f} dB (max {s['gain_err_db_max']:.2f}), "
          f"H2-H4 err rms {s['h2_h4_err_db_rms']:.1f} dB")
