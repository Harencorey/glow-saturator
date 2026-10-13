# Glow (Norka DSP tube saturator)

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
the plug-ins into `~/Library/Audio/Plug-Ins/`. Rescan in your DAW and look for **Norka DSP > Glow**.
For Logic, you can validate the AU with `auval -v aufx Glw1 Nrka`.

**Hover help:** rest the mouse on any control for about 3 seconds for a short explanation of what it does.

## Presets

The bar at the top: click the name for the menu, use **<** / **>** to step through presets, and **SAVE** to store the current settings.
A **\*** after the name means you've changed something since loading it.
- **Factory presets** are starting points: Default, Subtle Warmth, Tube Bus Glue, Vocal Air & Grit, Bass Weight, Parallel Drum Smash, Lo-Fi Crush.
- **Your presets** are plain files in `~/Library/Audio/Presets/Norka DSP/Glow/` (*.glowpreset), so you can back them up or copy them to another Mac.
  The menu has Save As, Delete, and Show Presets Folder.
- A preset stores every setting **except Mute and Solo**, so loading one never silences what you're listening to.
- Loading goes through the host like a knob move (undoable and automatable). The session remembers which preset you were on.

## Controls

**Pre** (before the tubes)
| Control | What it does |
|---|---|
| **High-Pass** | 24 dB/oct (Linkwitz-Riley), 20 Hz–1 kHz, −3 dB at the knob frequency. The very bottom of the knob is Off; the first step up is 20 Hz. Keeps lows out of the saturation. |
| **Shelf Freq / Gain** | High shelf, 1–16 kHz, ±12 dB. Boost it to make the top end hit the tubes harder. |

**Tubes**
| Control | What it does |
|---|---|
| **Drive** | Macro control: more of both even and odd harmonics. Partly auto-gained, like BB Tubes. The glow around the knob is a meter: it brightens with how hard the tubes are actually working on the incoming audio (quiet input at low Drive barely lights it, hot input or high Drive makes it bloom) and settles to a faint glow showing the knob setting when nothing's playing. |
| **Even** | Scales only the even-symmetric part of each tube stage's curve, so it moves 2nd/4th/6th harmonics without touching the odd ones. 0 % = no even harmonics, 50 % = as measured, 100 % = 3×. |
| **Odd** | How hard the output stage clips: ±12 dB around where Drive puts it. Symmetric clipping makes the 3rd/5th/7th harmonics. Adaptive auto-gain keeps the loudness steady (within ±0.25 dB on test material). |
| **Density** | How hard the input tube stage is hit. 50 % = matched; higher gives more lopsided bloom and "breathing" after loud hits. |

**Post** (after the tubes)
| Control | What it does |
|---|---|
| **Shelf Freq / Gain** | High shelf, 1–16 kHz, ±12 dB. |
| **Link** | The post shelf mirrors the pre shelf (same frequency, opposite gain), so the top end saturates harder or softer while the overall tonal balance stays put. While linked, the pre shelf knobs turn amber and read LINKED FREQ / LINKED GAIN (they now set both shelves), and the post shelf knobs grey out. This replaces the old Tone knob. |
| **Output** | −24 to +12 dB trim (after Dry + Wet are summed). |
| **Auto** | Master auto gain (note: Drive 0 is not a bypass. The tube voicing EQ stays in, about +1.6 dB around 100 Hz, so AUTO typically shows −0.1 to −1 dB there, more on bass-heavy material): keeps the output as loud as the input (K-weighted, like LUFS, averaged over ~3 s), so Drive/EQ/comp moves don't fool your ears and bypass A/Bs are fair. The readout shows what it's applying (±18 dB max). It holds through silence and only updates while audio is playing. Output trims on top. It's slow on purpose: it matches loudness, it doesn't ride the level. |

**Mix** (Dry and Wet channel strips)
| Control | What it does |
|---|---|
| **Dry / Wet faders** | −60 dB (Off) to +6 dB, each with a peak meter. The two are summed in parallel. The dry path is latency-aligned, so it never comb-filters. Defaults are Wet 0 dB and Dry Off. |
| **M / S** | Mute and Solo per path, console-style: any Solo mutes the un-soloed path, and Mute always wins. Both fade over 10 ms, and the muted path keeps processing underneath, so un-muting is seamless and stays phase-locked. A path's name dims when you can't hear it. |
| **Order (⇅)** | The EQ and COMP icons stack in signal order (top runs first). Click the little arrows between them to flip that path between EQ → Comp and Comp → EQ. The path fades out and back in (~30 ms) around the swap so it never clicks. |
| **EQ icon** | Click: opens that path's 4-band parametric EQ. **Double-click: switches the EQ on/off** (lit = on) with a response curve. **Drag a point** to set frequency/gain (hold Shift for frequency only), **scroll** over a point for Q, and **double-click** a point to reset its gain. Dragging switches a bypassed EQ on. Each band has Freq (20 Hz–20 kHz), Gain (±18 dB) and Q (0.1–10); bands 1 and 4 switch between shelf and bell. Settings glide over ~30 ms, so moves never zipper. Bands fade in and out (~60 ms) instead of switching. The icon lights up when the EQ is on. |
| **COMP icon** | Click opens, **double-click switches on/off**. Opens that path's "Distressor-style" compressor. There's no threshold: **Input** drives into a fixed −18 dBFS threshold and **Output** sets the level after. Attack runs 0.05–30 ms, release 50 ms–3.5 s. Ratios are 1:1, 2, 3, 4, 6, 10, 20 and **NUKE** (brickwall), with the knee hardening as the ratio rises. **Distortion** offers Clean / Dist 2 (2nd harmonic) / Dist 3 (3rd harmonic), and at 1:1 the comp is just a colour box. **Detector HPF** keeps lows from pumping it. **Auto Makeup** sets makeup gain from Input and Ratio (worked out from the settings, so it can't pump). Push Input for more compression without the level jumping. The panel shows the makeup it's applying. A track peaking around −12 dBFS stays within about 1 dB at any setting; quieter tracks come up louder as Input rises. Output trims on top. **Mix** blends the uncompressed signal back in (parallel compression inside the path; it's phase-exact because both sides of the blend share the same EQ and oversampling). Switching the comp on/off fades over 20 ms. Includes a gain-reduction meter. |

Signal order in each path: EQ → Comp (with Mix) → fader → mute/solo, or Comp → EQ when flipped.

At the default settings (Even, Odd and Density at 50 %, HPF off, shelves at 0 dB, Wet 0 dB / Dry Off,
EQ and Comp off) the output is bit-identical to v0.1.5, the BB Tubes-matched version.

## Dry/Wet phase lock

Whatever either path is doing, Dry and Wet arrive at the sum in phase. Every user filter (HPF, both shelves,
both strips' EQ bands) is built as a pair: a normal bell/shelf/high-pass at half the gain, then the same filter
with its numerator reversed. The pair gives the full gain with a phase response that is **exactly** an all-pass,
and the other path runs that all-pass (flat magnitude). It's the same trick that keeps a Linkwitz-Riley
crossover's two halves in phase. Both paths read one shared per-32-sample plan, so they stay locked while knobs move
and while bands fade in or out.

Measured (`measure/test_coherence.py`, `measure/test_switching.py`): wet-vs-dry phase stays within about 4° (the old
fixed tube-voicing filter fit, unchanged) with HPF, shelves, both EQs, both comps and Mix all in use, at
44.1/48/96 kHz. A 50/50 parallel sum loses under 0.01 dB. v0.1.8 lost up to 12 dB with the HPF up. During
switching or automation the worst momentary loss is 0.1 dB.

Trade-off: like any phase-coherent parallel design, the all-passes rotate the *overall* output's phase around
the frequencies you EQ (the dry path follows the wet path's filters instead of staying untouched). Relative to
other tracks that's the same kind of shift any minimum-phase EQ makes. A flat band (0 dB) is fully switched out, so it adds nothing.

## Signal flow

```
in ─┬─ HPF → pre shelf → input coupling HPFs (~24 Hz res.) → low bump (+2.8 dB @ 44 Hz) → presence dip
    │   → [4x oversampled]  input tube stage → coupling cap (~10 Hz) → Drive (+Odd) → output tube stage
    │                       (each stage's even part scaled by Even)
    │   → DC blocker → auto-gain → post shelf → WET strip (EQ + dry-EQ all-passes → Comp/Mix → fader) ─┐
    └─ dry (latency-matched, + all-passes of the wet filters) → DRY strip (EQ + wet-EQ all-passes → Comp/Mix → fader) ─┴─ sum → Output
```

### Quality and timing
- **Oversampling:** the tube stages run at ~384 kHz whatever the session rate (8× at 44.1/48k,
  4× at 88.2/96k, 2× at 176.4/192k), using linear-phase FIR filters. The comps (gain, distortion colour and Mix) run 4× (2× at 88.2k and up).
- **Latency:** 103 samples at 44.1/48 kHz, reported to the host and compensated. **Bypass keeps the same latency**,
  so toggling bypass never shifts the track in time.
- **Dry/Wet phase:** the dry path runs through an identical copy of the oversampling filters, plus an all-pass
  (flat magnitude) fitted to the wet path's low-end filters, plus the all-passes of every user filter (see *Dry/Wet phase lock*).
  The two sum coherently: under 7° of difference from 20 Hz–20 kHz, and the dry path is flat within ±0.04 dB.
- **Stereo:** identical L/R in gives bit-identical L/R out, with no crosstalk. The compressors and the Odd auto-gain are stereo-linked.
- **CPU** (one core of a 2.8 GHz cloud Xeon, 48 kHz stereo): ~7 % at default, ~12 % with Even/Odd in use,
  ~16 % with both strips' EQ + comps on. Expect roughly half that on Apple Silicon.

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
- `Source/PhaseCoherent.h`: the phase-locked filters (user EQ/HPF/shelves) and their all-pass twins.
- `Source/ChannelStrip.h`: Dry/Wet strip: EQ, compressor (with Mix), fader.
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
It also publishes a permanent **v0.1.<build number>** release with the same files and that commit's change notes, so every past
version stays downloadable from https://github.com/Harencorey/glow-saturator/releases (the "latest" one is replaced on each build).
Versions 0.1.5–0.1.11 were added afterwards from the saved build files. 0.1.5–0.1.9 show up in DAWs as **Haren Audio > Glow**;
0.1.10 onwards as **Norka DSP > Glow**. They install to the same place, so installing any version replaces the current one.

**Installing:** double-click `Glow-Installer.pkg`. It isn't Apple-notarized yet, so the first time, macOS blocks it.
Allow it with **System Settings > Privacy & Security > Open Anyway**, or right-click the .pkg and choose **Open**.
It installs to `/Library/Audio/Plug-Ins/` and removes older per-user copies.
The build number shows in the bottom-right of the plug-in window.

The next step for a warning-free install is Developer ID signing plus notarization, which needs an Apple Developer account.
