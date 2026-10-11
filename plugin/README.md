# Glow (Haren Audio tube saturator)

A two-stage tube saturator voiced against measurements of Waves BB Tubes.
It uses original DSP; no third-party code or assets are included.

## Build and install (Mac)

```bash
cd Glow/plugin
bash build_mac.sh                    # downloads JUCE 8.0.15 automatically
# or use your own JUCE checkout:
bash build_mac.sh ~/path/to/JUCE    # must be JUCE 8.x
```

The script needs the Xcode command line tools (`xcode-select --install`), plus CMake (`brew install cmake`).
It builds a universal (Apple Silicon + Intel) **AU, VST3 and Standalone** and installs
the plug-ins into `~/Library/Audio/Plug-Ins/`. Rescan in your DAW and look for **Haren Audio > Glow**.
For Logic, you can validate the AU with `auval -v aufx Glw1 Hrna`.

## Controls

**Pre** (before the tubes)
| Control | What it does |
|---|---|
| **High-Pass** | 12 dB/oct, 10 Hz–1 kHz, with Off at the bottom. Keeps lows out of the saturation. |
| **Shelf Freq / Gain** | High shelf, 1–16 kHz, ±12 dB. Boost it to make the top end hit the tubes harder. |

**Tubes**
| Control | What it does |
|---|---|
| **Drive** | Macro control: more of both even and odd harmonics. Partly auto-gained, like BB Tubes. |
| **Even** | Scales only the even-symmetric part of each tube stage's curve, so it moves 2nd/4th/6th harmonics without touching the odd ones. 0 % = no even harmonics, 50 % = as measured, 100 % = 3×. |
| **Odd** | How hard the output stage clips: ±12 dB around where Drive puts it. Symmetric clipping makes the 3rd/5th/7th harmonics. Adaptive auto-gain keeps the loudness steady (within ±0.25 dB on test material). |
| **Density** | How hard the input tube stage is hit. 50 % = matched; higher gives more lopsided bloom and "breathing" after loud hits. |

**Post** (after the tubes)
| Control | What it does |
|---|---|
| **Shelf Freq / Gain** | High shelf, 1–16 kHz, ±12 dB. |
| **Link** | The post shelf mirrors the pre shelf (same frequency, opposite gain), so the top end saturates harder or softer while the overall tonal balance stays put. This replaces the old Tone knob. |
| **Mix** | Parallel blend. The dry path is latency-compensated, so 0 % is a perfect null. |
| **Output** | −24 to +12 dB trim. |

At the default settings (Even, Odd and Density at 50 %, HPF off, shelves at 0 dB) the output is
bit-identical to v0.1.5, the BB Tubes-matched version.

## Signal flow

```
in ─┬─ HPF → pre shelf → input coupling HPFs (~24 Hz res.) → low bump (+2.8 dB @ 44 Hz) → presence dip
    │   → [4x oversampled]  input tube stage → coupling cap (~10 Hz) → Drive (+Odd) → output tube stage
    │                       (each stage's even part scaled by Even)
    │   → DC blocker → auto-gain → post shelf ─┐
    └──────────────── dry (latency-matched) ─────────┴─ Mix → Output
```

Latency: 61 samples at 48 kHz (linear-phase oversampling, reported to the host).

## How close it is to BB Tubes (measured at Drive 0/25/50/75/100)

- Output level vs input level: within 0.1–0.6 dB RMS (max 1.7 dB) across a 36 dB input range.
- Low-end EQ curve: within 0.26 dB, 28 Hz–19 kHz.
- 3rd/5th harmonic onset and ceiling: tracks closely at every setting.
- 2nd harmonic: tracks, except with full-scale input at Drive 75–100, where BB climbs to about −20 dBc
  and Glow reaches about −30 dBc. Turn Density up for more.
- Aliasing: inaudible at normal settings (about −72 dBc or lower). With a full-scale top-octave tone at Drive 100,
  it measures the same as or better than BB Tubes.

## Folder map

- `Source/GlowDSP.h`: the entire signal path (shared by the plug-in and the render tool).
- `Source/DriveMap.h`: fitted constants, **generated** by `model/gen_drivemap.py`; don't hand-edit.
- `Source/Biquad.h`: double-precision RBJ filters.
- `Source/PluginProcessor.*`, `Source/PluginEditor.*`: JUCE wrapper and UI.
- `tools/RenderTool.cpp`: offline renderer (`-DGLOW_BUILD_TOOLS=ON`), used for the A/B tests.
- `../measure/`: test-signal generator, analysis, BB comparison, and the aliasing/stress test.
- `../model/`: Python reference model and fitting scripts.

## Renaming it

To rename it, change `PRODUCT_NAME`, `PLUGIN_CODE` and `BUNDLE_ID` in `CMakeLists.txt` and the title text in `PluginEditor.cpp`.
Keep the 4-letter `PLUGIN_CODE` unique once you've used it in sessions.

## Automated macOS builds and installer

Every push to `main` builds Glow on a GitHub macOS runner (`.github/workflows/build-mac.yml`) as version `0.1.<build number>`.
The run checks that the binaries are universal and builds **Glow-Installer.pkg** (`packaging/make_pkg.sh`).
It then test-installs that .pkg on the runner and runs Apple `auval` plus pluginval at strictness 10 on the installed AU and VST3.
Finally it publishes the installer to the **latest** release: https://github.com/Harencorey/glow-saturator/releases/tag/latest

**Installing:** double-click `Glow-Installer.pkg`. It isn't Apple-notarized yet, so the first time, macOS blocks it.
Allow it with **System Settings > Privacy & Security > Open Anyway**, or right-click the .pkg and choose **Open**.
It installs to `/Library/Audio/Plug-Ins/` and removes older per-user copies.
The build number shows in the bottom-right of the plug-in window.

The next step for a warning-free install is Developer ID signing plus notarization, which needs an Apple Developer account.
