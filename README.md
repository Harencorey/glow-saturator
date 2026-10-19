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
- **Factory presets** are starting points: Default, Subtle Warmth, Tube Bus Glue, Vocal Air & Grit, Bass Weight, Parallel Drum Smash, Lo-Fi Crush, Iron Console Bus, Tape Glue, Diode Grit, Clip Smash.
- **Your presets** are plain files in `~/Library/Audio/Presets/Norka DSP/Glow/` (*.glowpreset), so you can back them up or copy them to another Mac.
  The menu has Save As, Delete, and Show Presets Folder.
- A preset stores every setting **except Mute and Solo**, so loading one never silences what you're listening to.
- Loading goes through the host like a knob move (undoable and automatable). The session remembers which preset you were on.

**A / B:** two complete settings to compare. Click **B** and it starts as a copy of A; tweak, then flip back and forth.
**Undo / Redo** (the arrows): every knob move, click and drag is one undo step.

## Controls

The window, left to right: **HPF / PRE-SAT EQ** (click the arrow to widen the window into the full pre-saturation
EQ), the **tubes** (Even / Density / Odd above Drive, the envelope follower either side of it) and **MIX** (the Mix
knob, with the Dry path's EQ / COMP on its left and the Wet path's on its right). Along the bottom: **IN / OUT meters**,
**Input**, **Output** and **Auto**. In the header:
presets, **A/B** and **undo/redo**. Right-click the background for the interface size (75-130 %).

**Input** (before the tubes)
| Control | What it does |
|---|---|
| **High-Pass** | 20 Hz–1 kHz, −3 dB at the knob frequency. The very bottom of the knob is Off; the first step up is 20 Hz. Keeps lows out of the saturation. It also shows as the **HP** point on the EQ curve above (drag it sideways; double-click it for Off). |
| **Slope 6 / 12 / 24 / 36** | High-pass slope in dB/oct (the four squares right of the knob): 6 (a broad tilt), 12 (gentle), 24 (Linkwitz-Riley, steep, the default) or 36 (Linkwitz-Riley 36, a wall). Every slope is −3 dB at the knob frequency. Switching glides, no click. 12, 24 and 36 are phase-locked to the dry path; **6 dB/oct can't be** (a 1st-order filter has no all-pass twin), so with Keep Lows **off** and Mix below 100 % the low end of the blend can sit up to ~2 dB under a perfect sum around the corner frequency. With Keep Lows on (the default), 6 dB/oct is exact too. Sessions from before the 4-way slope keep their 12 or 24. |
| **Keep Lows** | The square button left of the knob: everything the high-pass removes skips the tubes and comes back **clean, at full level**, so the low end stays full and solid while only what's above it saturates (a clean low-band split, like a parallel crossover). The split is exact: on quiet (clean) signal the output is flat within 0.3 dB vs the HPF off. Off = a plain cut. The dashed line on the curve shows the clean lows. Switching fades over 20 ms. |
| **Pre-sat EQ** | The small curve at the top of the card: what goes into the tubes. It's the whole EQ (there's no separate expanded view). **M** = a fully parametric mid bell (40 Hz–12 kHz, ±15 dB, Q 0.1–10), **H** = a high shelf (1–20 kHz, Q shapes the corner: 0.71 plain, higher adds the passive-style bump/dip). The curve includes the high-pass (cut region shaded). **Drag** a point = frequency + gain, **Cmd-drag** = Q, **Option-drag** = frequency only, **hold Shift** = solo the band, **double-click** = bypass the band, Option-double-click = gain to 0 dB. Dragging switches the EQ on. The lamp next to PRE-SAT EQ switches it on/off. |
| **MIRROR** | (Was LINK.) The exact opposite EQ runs after the tubes (the dashed mirror curve), so you change *what saturates* (push the mids into the tubes, say) while the overall tone stays put. Measured: linked, it nets to 0.00 dB on clean signal. **With Mirror off there is no post EQ**: shape the saturated sound with the Wet EQ instead. Note: boosting into the tubes makes them compress harder, so with Mirror the tone stays put but the level drops (about 2.5 dB for +8 dB mid / +6 dB high at Drive 50); turn on Auto to compare at matched loudness. |

**Saturation** (the middle section, with **TYPE** under it)
| Control | What it does |
|---|---|
| **TYPE** | Which saturation engine Drive pushes. **TUBE**: two triode stages, the original Glow sound (bit-identical to earlier versions). **IRON**: a console output transformer; the core saturates on flux, so the lows thicken and compress while the highs stay clean and open. **TAPE**: 15 ips tape; soft, mostly odd saturation, and loud highs soften first (self-erasure), so transients round off. **DIODE**: a diode clipper, overdrive-pedal style; harder than tube, odd-heavy bite. **CLIP**: a true hard clipper, clean right up to the ceiling, then flat-topped, with antiderivative antialiasing (worst aliasing −78 dBc even at Drive 100 with a 0 dBFS top-octave tone). Every type is exactly clean at low level, so Mix, Keep Lows and the Dry/Wet phase lock work the same in all of them. Each type's output staging is calibrated so its loudness follows TUBE's across the Drive range (IRON's makeup is capped at +3 dB so it never turns up the highs; it can sit a few dB quieter on bass-heavy material, which Auto Gain covers). Switching crossfades over 25 ms and is click-free, even while playing. |
| **Drive** | Macro control: more of both even and odd harmonics. Partly auto-gained, like BB Tubes. The glow around the knob is a meter: it brightens with how hard the tubes are actually working on the incoming audio (quiet input at low Drive barely lights it, hot input or high Drive makes it bloom) and settles to a faint glow showing the knob setting when nothing's playing. |
| **Envelope Amount** | (low left of Drive; the dotted lead runs to the Drive ring) Envelope follower: Drive follows the input's dynamics. **+** pushes louder moments harder (hits grit up, quiet parts stay clean); **−** gives quiet parts more drive and loud parts less (steady, even colour). It reacts to the material's own dynamics (moments rising above its ~3 s average), so it works the same at any track level, up to ±50 Drive. **The Drive knob is the resting point:** with + the follower only ever adds drive above the knob, with − it only takes drive away, and it always comes back to exactly the knob setting, never past it. The dot and line on the Drive ring show where it's pushing Drive. Off at 12 o'clock. |
| **Envelope Speed** | How fast the envelope follows: fast (1 ms attack / 25 ms release) catches individual hits, slow (60 ms / 1.5 s) follows phrases. |
| **Even** | (Same in every type.) Scales only the even-symmetric part of each stage's curve, so it moves 2nd/4th/6th harmonics without touching the odd ones. 0 % = no even harmonics, 50 % = as measured, 100 % = 3×. |
| **Odd** | (Same in every type.) How hard the output stage clips: ±12 dB around where Drive puts it (in IRON it pushes only the core, never the clean highs). Symmetric clipping makes the 3rd/5th/7th harmonics. Adaptive auto-gain keeps the loudness steady (within ±0.25 dB on test material). |
| **Density** | Each type's own character control (the tooltip changes with TYPE). **TUBE**: how hard the input stage is hit; higher = more lopsided bloom and "breathing" after loud hits. **IRON**: core size; low = only the deepest lows saturate, high = the saturation reaches into the low mids (~40 → 400 Hz). **TAPE**: tape level; higher = the highs soften earlier and lower down (12 → 3 kHz). **DIODE**: germanium (soft knee) → silicon (hard knee). **CLIP**: the corner, slightly rounded → brickwall. |

**Mix, Dry / Wet modules, meters**
| Control | What it does |
|---|---|
| **Mix** | Dry/wet: 100 % = all saturated, 0 % = all clean. A linear crossfade (the two paths are phase-locked copies, so a blend never thins out or bumps the level in the middle). At 100 % the clean path is switched off completely. |
| **DRY / WET EQ, COMP** | Each path's own EQ and compressor, before the Mix: DRY (left) works on the clean path, WET (right) on the saturated one. Click the lamp (or double-click the button) to switch one on/off; click the rest to open its editor in the drawer under the main window. The arrows between them swap that path's order (top runs first). |
| **IN / OUT meters** | Peak meters, L and R: IN is after the Input knob (what hits the tubes), OUT is the plug-in's output. The number is the highest peak so far (red once it has gone over 0 dBFS); click a meter to reset it. |
| **Input** | −24 to +24 dB, before everything: push it to drive the tubes harder. Auto Gain measures the input before this trim, so with Auto on, Input changes the drive, not the level: after you move Input (or Drive, Density, Even, Odd, Mix) it re-matches in about half a second, then goes back to its slow, steady 3 s average. (Only knob turns in the window do this; host automation keeps the slow match, so automated moves never make it ride the level.) |
| **Output** | −24 to +24 dB trim, the last thing in the chain (0 dB = knob straight up, like Input). |
| **Auto** | Master auto gain (note: Drive 0 is not a bypass. The tube voicing EQ stays in, about +1.6 dB around 100 Hz, so AUTO typically shows −0.1 to −1 dB there, more on bass-heavy material): keeps the output as loud as the input (K-weighted, like LUFS, averaged over ~3 s), so Drive/EQ/comp moves don't fool your ears and bypass A/Bs are fair. The AUTO tooltip shows what it's applying (±18 dB max). It holds through silence and only updates while audio is playing. Output trims on top. It's slow on purpose: it matches loudness, it doesn't ride the level. |

The EQ / COMP editors (Dry and Wet):
Signal order in each path: EQ → Comp (with its Mix), or Comp → EQ when flipped, then the Mix.

Older sessions and presets load with the same sound: their Dry/Wet balance becomes the matching Mix setting (plus Output, unless Auto Gain was on), with their EQ / comp settings unchanged. Automation written on the old Dry/Wet faders or Mute/Solo has nothing to drive any more (those parameters are gone); an old session that had both paths muted loads as 100 % Wet.

At the default settings (Even, Odd and Density at 50 %, HPF off, pre-sat EQ off, Mix 100 %, EQs and comps off) the output is bit-identical to v0.1.5, the BB Tubes-matched version.

## Dry/Wet phase lock

Whatever either path is doing, Dry and Wet arrive at the Mix in phase. Every user filter (HPF, the pre-sat EQ,
both paths' EQ bands) is built as a pair: a normal bell/shelf/high-pass at half the gain, then the same filter
with its numerator reversed. The pair gives the full gain with a phase response that is **exactly** an all-pass,
and the other path runs that all-pass (flat magnitude). It's the same trick that keeps a Linkwitz-Riley
crossover's two halves in phase. Both paths read one shared per-32-sample plan, so they stay locked while knobs move
and while bands fade in or out.

Measured (`measure/test_coherence.py`, `measure/test_switching.py`): wet-vs-dry phase stays within about 4° (the old
fixed tube-voicing filter fit, unchanged) with HPF, Keep Lows, pre-sat EQ, both EQs and both comps in use, at
44.1/48 kHz (up to ~11° at 96 kHz with Keep Lows, unchanged since v0.1.26). A 50/50 parallel sum loses under 0.01 dB. v0.1.8 lost up to 12 dB with the HPF up. During
switching or automation the worst momentary loss is 0.1 dB.

Trade-off: phase-locking adds an all-pass to the *whole* output around the frequencies you EQ, which smears the
low end in time (e.g. ~5-6 ms around a 100 Hz shelf vs ~1 ms for a normal EQ). So Glow only phase-locks when it
matters: **a filter is phase-locked only while the clean path is audible** (Mix below 100 %). Otherwise it's an ordinary EQ with normal, tight phase, and it crossfades between the two in 50 ms when
that changes. **Linked pairs** (pre-sat EQ Mirror) always use an ordinary EQ plus its exact inverse, which
cancels completely, phase included (measured 0.00 ms / 0.00 dB), so they need no all-pass at all. A flat band (0 dB) is fully switched out.

## Signal flow

```
in ─┬─ HPF → pre-sat EQ → input coupling HPFs (~24 Hz res.) → low bump (+2.8 dB @ 44 Hz) → presence dip
    │   → [4x oversampled]  input tube stage → coupling cap (~10 Hz) → Drive (+Odd) → output tube stage
    │                       (each stage's even part scaled by Even)
    │   → DC blocker → auto-gain → mirror EQ (Mirror) → + clean lows (Keep Lows) → WET EQ / Comp ─┐ Mix
    └─ dry (latency-matched, + all-passes of the wet filters) → DRY EQ / Comp ─────────────────────┴─(crossfade)
       → Auto Gain → Output
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
  ~16 % with both paths' EQ + comps on. Expect roughly half that on Apple Silicon.

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
- `Source/DriveTypes.h`: the IRON / TAPE / DIODE / CLIP engines. `Source/TypeMap.h`: their Drive gain law, **generated** by `model/gen_typemap.py` (loudness-matched to TUBE).
- `Source/PhaseCoherent.h`: the phase-locked filters (user EQ/HPF/shelves) and their all-pass twins.
- `Source/ChannelStrip.h`: each path's EQ + compressor (with Mix).
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
