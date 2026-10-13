#pragma once
#include <juce_dsp/juce_dsp.h>
#include "Biquad.h"
#include "DriveMap.h"
#include "ChannelStrip.h"
#include "DryPhase.h"

// The whole signal path, independent of the plugin wrapper so the offline
// render tool (tools/RenderTool.cpp) runs the exact same code the plugin does.
//
//  in ─┬─ HPF → pre high shelf → input coupling (HPFs) → low bump → presence dip
//      │   → [oversampled to ~384 kHz: input tube stage → coupling cap → Drive(+Odd) gain → output stage]
//      │   → DC blocker → auto-gain → post high shelf → WET strip (EQ → Comp/Mix → fader) ─┐
//      └─ dry → same oversampling filters (no saturation) → phase-match all-passes → DRY strip ─┴─ sum → Output
//
// Dry/Wet phase: the dry path goes through an identical copy of the oversampling filters, so
// its latency and top-end phase match the wet path exactly, plus an all-pass (flat magnitude)
// fitted to the wet path's fixed low-end filters. Every user filter (HPF, shelves, both strip
// EQs) is phase-coherent (PhaseCoherent.h): its phase is exactly an all-pass, and the other
// path runs that all-pass. So Dry + Wet sum in phase whatever either side is doing.
//
// Even/Odd: each tube stage's curve f(x) is split into its odd part (f(x)-f(-x))/2 and
// its even part (f(x)+f(-x))/2. Only the even part makes even harmonics, so scaling it
// (Even knob) changes 2nd/4th/6th... without touching the odd ones. Odd sets how hard the
// output stage clips (symmetric clipping = odd harmonics). At 50/50 both are as measured.
// The tube stages' knee function h(b) = (1 + b^s)^(-1/s) on b in [0, 1], tabulated with
// Catmull-Rom interpolation. Replaces two pow() calls per sample; matches the exact
// formula to better than -120 dB. Rebuilt only when the knee hardness s changes.
class KneeTable
{
public:
    void build (float s) noexcept
    {
        sharp = s;
        for (int k = 0; k < N + 4; ++k)
        {
            const double b = std::abs ((double) (k - 1) / N);
            t[(size_t) k] = (float) std::pow (1.0 + std::pow (b, (double) s), -1.0 / (double) s);
        }
    }
    // a change of 0.1 % in knee hardness is far below audibility; avoids rebuilding every block
    bool needs (float s) const noexcept { return std::abs (s - sharp) > 1.0e-3f * s; }

    inline float operator() (float b) const noexcept   // b in [0, 1]
    {
        const float pos = b * (float) N;
        const int i = juce::jlimit (0, N - 1, (int) pos);
        const float f = pos - (float) i;
        const float y0 = t[(size_t) i], y1 = t[(size_t) i + 1], y2 = t[(size_t) i + 2], y3 = t[(size_t) i + 3];
        return y1 + 0.5f * f * (y2 - y0 + f * (2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3 + f * (3.0f * (y1 - y2) + y3 - y0)));
    }

private:
    static constexpr int N = 2048;
    std::array<float, N + 4> t {};
    float sharp = -1.0f;
};

class GlowDSP
{
public:
    // HPF knob: the bottom of its travel is Off, then it starts at 20 Hz
    static constexpr float kHpfOffHz = 19.0f, kHpfMinHz = 20.0f;
    struct Params
    {
        float drive = 25.0f;    // 0..100     macro: more of both even and odd
        float even = 50.0f;     // 0..100     50 = as measured, 0 = no even harmonics, 100 = 3x
        float odd = 50.0f;      // 0..100     50 = as measured, -/+12 dB of clipping depth at 0/100
        float density = 50.0f;  // 0..100     input-stage heat: 50 = measured, 0 = clean, 100 = +12 dB hotter
        float hpfHz = kHpfOffHz; // Off, or 20..1000 Hz   pre-saturation high-pass
        float preShelfHz = 5000.0f, preShelfDb = 0.0f;    // pre-saturation high shelf
        float postShelfHz = 5000.0f, postShelfDb = 0.0f;  // post-saturation high shelf
        float outputDb = 0.0f;  // -24..+12 dB
        bool shelfLink = false; // pre/post shelves linked (post = -pre): ordinary exact-inverse pair
        bool autoGain = false;  // match output loudness to input (before the Output trim)
        float envAmount = 0.0f; // -100..+100 %: envelope follower moves Drive with the input's dynamics
        float envSpeed = 50.0f; // 0..100: slow (follows phrases) .. fast (follows hits)
        EqSettings preEq;       // expanded pre-saturation EQ (bands 1 low, 2 mid, 4 high; band 3 unused)
        bool preEqLink = false; // mirror it after the tubes at opposite gain
        ChannelStrip::Settings dry { ChannelStrip::kOffDb, {}, {} };   // dry path off by default
        ChannelStrip::Settings wet { 0.0f, {}, {} };                   // wet path at unity
    };

    void prepare (double sampleRate, int maxBlock, int numChannels)
    {
        sr = sampleRate;
        channels = juce::jmax (1, numChannels);

        // keep the tube stages at ~384 kHz whatever the session rate: 8x at 44.1/48k,
        // 4x at 88.2/96k, 2x at 176.4/192k (same aliasing protection, no wasted CPU)
        osStages = sr < 60000.0 ? 3 : (sr < 120000.0 ? 2 : 1);
        oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
            (size_t) channels, osStages,
            juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        oversampler->initProcessing ((size_t) maxBlock);

        // dry path: identical oversampling filters (no saturation) -> same latency and phase
        dryOversampler = std::make_unique<juce::dsp::Oversampling<float>> (
            (size_t) channels, osStages,
            juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        dryOversampler->initProcessing ((size_t) maxBlock);
        jassert (std::abs (dryOversampler->getLatencyInSamples() - oversampler->getLatencyInSamples()) < 1.0e-6f);
        dryBuf.setSize (channels, maxBlock, false, true, true);
        dryStrip.prepare (sr, channels, maxBlock);
        wetStrip.prepare (sr, channels, maxBlock);

        ch.assign ((size_t) channels, Channel {});
        hpfStage.prepare (sr, maxBlock, Coherent::Kind::highPass);
        preStage.prepare (sr, maxBlock, Coherent::Kind::highShelf);
        postStage.prepare (sr, maxBlock, Coherent::Kind::highShelf);
        for (auto& c : ch)
        {
            c.hp1.setOnePoleHighPass (DriveMap::eq.hp1Fc, sr);
            c.hp2.setHighPass (DriveMap::eq.hpFc, DriveMap::eq.hpQ, sr);
            c.bump.setPeak (DriveMap::eq.bumpFc, DriveMap::eq.bumpDb, DriveMap::eq.bumpQ, sr);
            c.dip.setPeak (DriveMap::eq.dipFc, DriveMap::eq.dipDb, DriveMap::eq.dipQ, sr);
            c.dc.setHighPass (DriveMap::post.dcFc, DriveMap::post.dcQ, sr);
            c.inter.setHighPass (DriveMap::st1.hpFc, DriveMap::st1.hpQ,
                                 sr * (double) oversampler->getOversamplingFactor());
            for (int k = 0; k < DryPhase::numSecondOrder; ++k)
                c.dryPhase[(size_t) k].setAllPass (DryPhase::secondOrder[k].fc, DryPhase::secondOrder[k].q, sr);
        }
        for (auto* k : { &kIn, &kOut })
            for (auto& c : *k)
            {
                c[0].setHighShelf (1681.974, 3.99984, 0.7071752, sr);   // BS.1770 pre-filter
                c[1].setHighPass (38.13547, 0.5003270, sr);             // BS.1770 RLB high-pass
            }
        knee1.build (DriveMap::st1.sharp);
        knee2.build (DriveMap::at (params.drive).sharp);
        if (kneeCache.empty())
        {
            kneeCache.resize (101);   // ~0.8 MB, built once
            for (int d = 0; d <= 100; ++d) kneeCache[(size_t) d].build (DriveMap::at ((float) d).sharp);
        }
        preEqF.prepare (sr, maxBlock);
        postEqF.prepare (sr, maxBlock);
        smEnvAmt.reset (sr, 0.05);

        smPre.reset (sr, 0.05);
        smPost.reset (sr, 0.05);
        smOut.reset (sr, 0.05);
        smDrive.reset (sr, 0.05);
        smDensity.reset (sr, 0.05);
        smEven.reset (sr, 0.05);
        smOdd.reset (sr, 0.05);
        firstBlock = true;
        reset();
    }

    void reset()
    {
        if (oversampler) oversampler->reset();
        if (dryOversampler) dryOversampler->reset();
        dryStrip.reset();
        wetStrip.reset();
        for (auto& c : ch)
        {
            c.hp1.reset(); c.hp2.reset(); c.bump.reset(); c.dip.reset();
            c.dc.reset(); c.inter.reset();
            for (auto& f : c.dryPhase) f.reset();
        }
        for (auto* m : { &hpfMain, &preMain, &postMain }) m->reset();
        for (auto* a : { &hpfAp, &preAp, &postAp }) a->reset();
        for (auto* st : { &hpfStage, &preStage, &postStage }) st->snapNext();
        oddMakeup = 1.0f;
        oddPowAvg = refPowAvg = 0.0f;
        for (auto* k : { &kIn, &kOut }) for (auto& c : *k) { c[0].reset(); c[1].reset(); }
        inShort = outShort = inBlock = inLong = outLong = 0.0f;
        autoGainDb = autoApplied = 0.0f;
        preEqF.reset(); postEqF.reset(); preEqF.snap(); postEqF.snap();
        envLin = 0.0f; envAvgDb = -100.0f; envOffset = 0.0f; sampleCount = 0;
        smEnvAmt.setCurrentAndTargetValue (params.envAmount);
    }

    // tube-stage oversampling + the channel strips' colour oversampling (same on both paths)
    int getLatencySamples() const
    {
        return (oversampler ? (int) std::round (oversampler->getLatencyInSamples()) : 0) + wetStrip.getLatencySamples();
    }

    void setParams (const Params& p)
    {
        params = p;
        smDrive.setTargetValue (p.drive);
        smDensity.setTargetValue (p.density);
        smEven.setTargetValue (p.even);
        smOdd.setTargetValue (p.odd);
        smOut.setTargetValue (juce::Decibels::decibelsToGain (p.outputDb));
    }

    void process (juce::dsp::AudioBlock<float>& block)
    {
        // The envelope follower moves Drive; run in short chunks while it's active so the
        // movement is smooth whatever the host's buffer size.
        // Chunks sit on a fixed grid of the running sample count, so the follower updates at
        // the same moments whatever buffer size the host uses (realtime and bounces match).
        const int total = (int) block.getNumSamples();
        const bool chunked = std::abs (params.envAmount) > 0.01f || std::abs (envOffset) > 0.01f;
        if (! chunked) { processChunk (block); sampleCount += total; return; }
        for (int start = 0; start < total;)
        {
            const int len = juce::jmin (total - start, kEnvChunk - (int) (sampleCount % kEnvChunk));
            auto sub = block.getSubBlock ((size_t) start, (size_t) len);
            processChunk (sub);
            start += len;
            sampleCount += len;
        }
    }

    void processChunk (juce::dsp::AudioBlock<float>& block)
    {
        const int n = (int) block.getNumSamples();
        const int nch = juce::jmin ((int) block.getNumChannels(), channels);
        if (n == 0) return;
        measureLoudness (block, nch, n, true);   // input loudness, before anything touches it
        followEnvelope (block, nch, n);          // envelope follower -> envOffset (Drive units)

        if (firstBlock)
        {
            // start exactly at the current settings instead of ramping in from defaults
            smDrive.setCurrentAndTargetValue (params.drive);
            smDensity.setCurrentAndTargetValue (params.density);
            smEven.setCurrentAndTargetValue (params.even);
            smOdd.setCurrentAndTargetValue (params.odd);
            smOut.setCurrentAndTargetValue (smOut.getTargetValue());
        }

        // This block's filter plans. Both paths read the same plans, so every filter and its
        // all-pass twin move (and switch in/out) in lockstep.
        // Phase-locking costs time smear (an all-pass on the whole output), so filters only do it
        // when the other path is actually audible. Linked pairs never need it: an ordinary EQ
        // followed by its exact inverse has zero net phase shift by itself.
        const bool anySoloEarly = params.dry.solo || params.wet.solo;
        const bool dryHeard = dryStrip.isActive (params.dry) && ! params.dry.mute && (! anySoloEarly || params.dry.solo);
        const bool wetHeard = wetStrip.isActive (params.wet) && ! params.wet.mute && (! anySoloEarly || params.wet.solo);
        hpfStage.plan (params.hpfHz >= kHpfMinHz - 0.5f, params.hpfHz, 0.0f, 0.70710678f, n);
        preStage.plan (true, params.preShelfHz, params.preShelfDb, 0.70710678f, n, dryHeard && ! params.shelfLink);
        postStage.plan (true, params.postShelfHz, params.postShelfDb, 0.70710678f, n, dryHeard && ! params.shelfLink);
        // mute/solo (like a console: any solo mutes the un-soloed path; mute always wins)
        const bool anySolo = params.dry.solo || params.wet.solo;
        params.dry.audible = ! params.dry.mute && (! anySolo || params.dry.solo);
        params.wet.audible = ! params.wet.mute && (! anySolo || params.wet.solo);
        // EQ band solo (Shift on an EQ point): only the path that EQ is on is heard
        const int bandSoloPath = eqSoloPath.load();
        if (bandSoloPath == 1) params.wet.audible = false;
        if (bandSoloPath == 2 || bandSoloPath == 3) params.dry.audible = false;
        // expanded pre-saturation EQ, and (Link) its mirror image after the tubes
        preEqF.plan (params.preEq, true, n, dryHeard && ! params.preEqLink);
        {
            EqSettings mirror = params.preEq;
            mirror.on = params.preEq.on && params.preEqLink;
            for (auto& gdb : mirror.gain) gdb = -gdb;
            postEqF.plan (mirror, true, n, false);   // linked pair: ordinary filters, exact inverse
        }
        const bool dryActive = dryStrip.isActive (params.dry);
        wetStrip.planEq (params.wet, wetStrip.isActive (params.wet), n, dryHeard);
        dryStrip.planEq (params.dry, dryActive, n, wetHeard);

        // dry path: copy of the input through the same oversampling filters + phase-match all-passes
        auto dry = juce::dsp::AudioBlock<float> (dryBuf).getSubsetChannelBlock (0, (size_t) nch).getSubBlock (0, (size_t) n);
        if (dryActive)
        {
            if (dryStale)   // fader just came up: start the dry filters from a clean state
            {
                dryOversampler->reset();
                for (auto& c : ch) for (auto& f : c.dryPhase) f.reset();
                for (auto* a : { &hpfAp, &preAp, &postAp }) a->reset();
                preEqF.resetAllpass(); postEqF.resetAllpass();
                dryStale = false;
            }
            dry.copyFrom (block.getSubsetChannelBlock (0, (size_t) nch));
            dryOversampler->processSamplesUp (dry);
            dryOversampler->processSamplesDown (dry);
            for (int c = 0; c < nch; ++c)
            {
                auto* d = dry.getChannelPointer ((size_t) c);
                auto& s = ch[(size_t) c];
                for (int i = 0; i < n; ++i)
                {
                    double v = d[i];
                    for (auto& f : s.dryPhase) v = f.process (v);
                    d[i] = (float) v;
                }
            }
            // the wet path's HPF and shelves, as all-passes
            if (hpfStage.busy())  hpfAp.process (hpfStage, dry);
            if (preStage.busy())  preAp.process (preStage, dry);
            if (postStage.busy()) postAp.process (postStage, dry);
            preEqF.allpassOnOtherPath (dry);
            postEqF.allpassOnOtherPath (dry);
        }
        else
        {
            dry.clear();
            dryStale = true;
        }

        // drive/density are smoothed per block; gains are ramped per sample
        const float driveNow = juce::jlimit (0.0f, 100.0f, smDrive.skip (n) + envOffset);
        modDrive.store (driveNow);
        const auto shape = DriveMap::at (driveNow);
        // while the follower moves Drive, use the pre-built curve for the nearest Drive step
        // (rebuilding the curve every few ms would cost a lot of CPU for an inaudible gain)
        const bool envOn = std::abs (envOffset) > 0.0f;
        const KneeTable& kneeOut = envOn ? kneeCache[(size_t) juce::jlimit (0, 100, (int) std::lround (driveNow))] : knee2;
        // Density scales how hard the input stage is hit: 50 % = as measured,
        // 100 % = +12 dB hotter (more lopsided, more "breathing"), 0 % = clean input stage.
        const float dn = smDensity.skip (n) / 50.0f;
        const float amount = juce::jmax (dn * dn, 1.0e-3f);
        const float k1 = DriveMap::st1.inGain * amount;   // into the input stage
        const float k1inv = 1.0f / k1;                     // and back to unity
        // Even: scale of each stage's even-symmetric part (1 = as measured, 0 = none, 3 = 3x)
        const float ev = smEven.skip (n);
        const float evenGain = ev <= 50.0f ? ev / 50.0f : 1.0f + 2.0f * (ev - 50.0f) / 50.0f;
        // Odd: extra/less drive into the output stage, with the same auto-gain law as Drive
        // (measured on the reference: output drops ~0.42 dB per dB of drive)
        const float oddDb = 12.0f * (smOdd.skip (n) - 50.0f) / 50.0f;
        // Odd auto-gain is adaptive (see the oversampled loop): it keeps the loudness equal to
        // what the output stage gives at the nominal Odd setting, whatever the material.
        const bool oddActive = std::abs (oddDb) > 1.0e-4f || std::abs (oddMakeup - 1.0f) > 1.0e-4f;
        const float oddInv = juce::Decibels::decibelsToGain (-oddDb);
        smPre.setTargetValue (juce::Decibels::decibelsToGain (shape.preDb + oddDb));
        smPost.setTargetValue (juce::Decibels::decibelsToGain (shape.postDb) * oddMakeup);
        if (firstBlock)
        {
            smPre.setCurrentAndTargetValue (smPre.getTargetValue());
            smPost.setCurrentAndTargetValue (smPost.getTargetValue());
            firstBlock = false;
        }

        // ---- base rate: pre-EQ ----
        auto wetIn = block.getSubsetChannelBlock (0, (size_t) nch);
        if (hpfStage.busy()) hpfMain.process (hpfStage, wetIn);
        if (preStage.busy()) preMain.process (preStage, wetIn);
        preEqF.process (wetIn);
        for (int c = 0; c < nch; ++c)
        {
            auto* x = block.getChannelPointer ((size_t) c);
            auto& s = ch[(size_t) c];
            for (int i = 0; i < n; ++i)
            {
                double v = (double) x[i];
                v = s.hp1.process (v);
                v = s.hp2.process (v);
                v = s.bump.process (v);
                v = s.dip.process (v);
                x[i] = (float) v;
            }
        }
        // ---- oversampled nonlinearity ----
        const float g0 = smPre.getCurrentValue();
        smPre.skip (n);
        const float g1 = smPre.getCurrentValue();
        auto up = oversampler->processSamplesUp (block);
        const int nu = (int) up.getNumSamples();
        const float gStep = (g1 - g0) / (float) nu;
        if (! envOn && knee2.needs (shape.sharp)) knee2.build (shape.sharp);   // only while Drive moves
        double powOdd = 0.0, powRef = 0.0;   // block energy: actual vs nominal-Odd output stage
        float sumDist = 0.0f, sumOut = 0.0f; // for the glow meter: distortion vs clean-signal energy
        for (int c = 0; c < nch; ++c)
        {
            auto* u = up.getChannelPointer ((size_t) c);
            auto& s = ch[(size_t) c];
            float g = g0;
            for (int i = 0; i < nu; ++i, g += gStep)
            {
                // input stage: unity for small signals, bends lopsidedly when hit hard
                const float w = stage (u[i] * k1, knee1, DriveMap::st1.asym, DriveMap::st1.k2, evenGain);
                // coupling cap: its DC shift is what makes hot passages clip lopsided,
                // and its recovery is the ~half-second "breathing" after loud hits
                const float v = (float) s.inter.process ((double) w) * k1inv;
                // drive into the output stage
                const float y = stage (v * g, kneeOut, shape.asym, shape.k2, evenGain);
                if (oddActive && (i & 3) == 0)
                {
                    // silent reference: the output stage at nominal Odd. Only a loudness estimate
                    // is needed, so every 4th oversampled sample is plenty.
                    const float yRef = stage (v * g * oddInv, kneeOut, shape.asym, shape.k2, evenGain);
                    powOdd += (double) y * y;
                    powRef += (double) yRef * yRef;
                }
                // how hard the tubes are working: what both stages add on top of a clean
                // (linear) path, in this oversampled sample. Feeds the Drive glow.
                const float dist = (y - v * g) + g * (w - u[i] * k1) * k1inv;
                sumDist += dist * dist;
                const float lin = v * g;   // what a clean (unsaturated) path would put out
                sumOut += lin * lin;
                u[i] = y;
            }
        }
        oversampler->processSamplesDown (block);
        if (sumOut > 1.0e-9f)
        {
            const float ratio = juce::jmin (1.0f, sumDist / sumOut);   // 0 = clean ... 1 = completely crushed
            float prev = drivenMeter.load();
            while (ratio > prev && ! drivenMeter.compare_exchange_weak (prev, ratio)) {}
        }

        if (oddActive)
        {
            // ~0.5 s loudness averages, stereo-linked; makeup = how much the Odd setting
            // changed the level. Holds its value through silence.
            const float a = std::exp (-(float) nu / (0.5f * (float) (sr * (double) oversampler->getOversamplingFactor())));
            oddPowAvg = a * oddPowAvg + (1.0f - a) * (float) powOdd;
            refPowAvg = a * refPowAvg + (1.0f - a) * (float) powRef;
            if (oddPowAvg > 1.0e-12f && refPowAvg > 1.0e-12f)
                oddMakeup = juce::jlimit (0.25f, 4.0f, std::sqrt (refPowAvg / oddPowAvg));   // +/-12 dB max
            if (std::abs (oddDb) <= 1.0e-4f && std::abs (oddMakeup - 1.0f) < 2.0e-3f)
            {
                oddMakeup = 1.0f;   // back at nominal: settle exactly, drop the extra path
                oddPowAvg = refPowAvg = 0.0f;
            }
        }

        // ---- base rate: DC block, output staging, post shelf ----
        for (int c = 0; c < nch; ++c)
        {
            auto* y = block.getChannelPointer ((size_t) c);
            auto& s = ch[(size_t) c];
            auto g = smPost;
            for (int i = 0; i < n; ++i)
            {
                y[i] = (float) (s.dc.process ((double) y[i]) * g.getNextValue());
            }
        }
        smPost.skip (n);
        if (postStage.busy()) postMain.process (postStage, wetIn);
        postEqF.process (wetIn);

        // channel strips, then sum (a muted dry path costs nothing)
        auto wetBlock = block.getSubsetChannelBlock (0, (size_t) nch);
        wetStrip.process (wetBlock, params.wet, dryStrip);
        dryStrip.process (dry, params.dry, wetStrip);
        if (testSolo == 2) wetBlock.copyFrom (dry);    // measurement hook (render tool only)
        else if (testSolo != 1) wetBlock.add (dry);

        // auto gain: always measured (so switching it on lands on the right level at once),
        // applied only when enabled. Then the Output trim on top.
        measureLoudness (block, nch, n, false);
        const float autoTarget = params.autoGain ? autoGainDb : 0.0f;
        const float a0 = autoApplied;
        autoApplied = autoTarget + (autoApplied - autoTarget) * std::exp (-(float) n / (0.05f * (float) sr));   // ~50 ms
        if (std::abs (autoApplied - autoTarget) < 1.0e-3f) autoApplied = autoTarget;
        const bool autoMoving = std::abs (a0) > 0.0f || std::abs (autoApplied) > 0.0f;
        const float ga0 = juce::Decibels::decibelsToGain (a0), ga1 = juce::Decibels::decibelsToGain (autoApplied);
        const float gaStep = (ga1 - ga0) / (float) n;
        for (int c = 0; c < nch; ++c)
        {
            auto* y = block.getChannelPointer ((size_t) c);
            auto g = smOut;
            if (autoMoving)
            {
                float ga = ga0;
                for (int i = 0; i < n; ++i) { ga += gaStep; y[i] *= g.getNextValue() * ga; }
            }
            else
                for (int i = 0; i < n; ++i) y[i] *= g.getNextValue();
        }
        smOut.skip (n);
        autoMeter.store (params.autoGain ? autoApplied : 0.0f);
        applyBandSolo (block, nch, n);
        if ((sampleCount + n) % kEnvChunk == 0) updateEnvOffset();
    }

    // UI: highest distortion-to-signal power ratio since the last call (then cleared)
    float takeDrivenness() { return drivenMeter.exchange (0.0f); }

    // UI: the auto gain currently applied, in dB (0 when off)
    float getAutoGainDb() const { return autoMeter.load(); }

    // A tube stage with its even-harmonic content scaled by evenGain.
    // evenGain == 1 is the plain stage (one evaluation); otherwise the curve is split into
    // odd and even parts:  odd = (f(u) - f(-u)) / 2,  even = (f(u) + f(-u)) / 2.
    static inline float stage (float u, const KneeTable& knee, float asym, float k2, float evenGain) noexcept
    {
        // the even-order bend is the same for u and -u, so it is computed once
        const float uu = u * u;
        const float bend = k2 * uu / std::sqrt (1.0f + uu * uu);
        const float a = clipStage (u + bend, knee, asym);
        if (std::abs (evenGain - 1.0f) < 1.0e-6f) return a;   // default: plain stage, one evaluation
        const float b = clipStage (-u + bend, knee, asym);     // f(-u)
        return 0.5f * (a - b) + evenGain * 0.5f * (a + b);
    }

    // Same curve as shapeStageExact(), with the knee read from the table.
    static inline float shapeStage (float u, const KneeTable& knee, float asym, float k2) noexcept
    {
        const float uu = u * u;
        return clipStage (u + k2 * uu / std::sqrt (1.0f + uu * uu), knee, asym);
    }

    static inline float clipStage (float v, const KneeTable& knee, float asym) noexcept
    {
        const float ceil = v >= 0.0f ? 1.0f - asym : 1.0f + asym;
        const float r = v / ceil;
        const float ar = std::abs (r);
        if (ar < 1.0e-9f) return v;
        if (ar <= 1.0f) return ceil * r * knee (ar);
        return ceil * (r > 0.0f ? 1.0f : -1.0f) * knee (1.0f / ar);
    }

    // One tube stage: bounded even-order bend (k2) then an asymmetric soft clip with
    // knee hardness 'sharp'. Mirrors the shaper in model/model.py exactly (reference version).
    static inline float shapeStageExact (float u, float sharp, float asym, float k2) noexcept
    {
        const float uu = u * u;
        const float v = u + k2 * uu / std::sqrt (1.0f + uu * uu);
        const float ceil = v >= 0.0f ? 1.0f - asym : 1.0f + asym;
        const float r = v / ceil;
        const float ar = std::abs (r);
        // r / (1 + |r|^s)^(1/s). Split at |r| = 1 so |r|^s can never overflow.
        if (ar < 1.0e-9f) return v;
        if (ar <= 1.0f)
            return ceil * r * std::pow (1.0f + std::pow (ar, sharp), -1.0f / sharp);
        const float sgn = r > 0.0f ? 1.0f : -1.0f;
        return ceil * sgn * std::pow (1.0f + std::pow (ar, -sharp), -1.0f / sharp);
    }

private:
    size_t osStages = 3;   // set in prepare() from the sample rate

    // Loudness for auto gain: K-weighted (ITU BS.1770 curve: ~+4 dB above 1.5 kHz, rolled off
    // below ~40 Hz), mean square over ~3 s. Only updated while both input and output are
    // actually playing, so silence, fades and muting both paths never drag the gain around.
    void measureLoudness (const juce::dsp::AudioBlock<float>& block, int nch, int n, bool isInput)
    {
        double sum = 0.0;
        nch = juce::jmin (nch, 2);
        for (int c = 0; c < nch; ++c)
        {
            auto& k = isInput ? kIn[(size_t) c] : kOut[(size_t) c];
            const auto* x = block.getChannelPointer ((size_t) c);
            for (int i = 0; i < n; ++i)
            {
                const double v = k[1].process (k[0].process ((double) x[i]));
                sum += v * v;
            }
        }
        const float ms = (float) (sum / (double) (n * juce::jmax (1, nch)));
        const float aShort = std::exp (-(float) n / (0.4f * (float) sr));
        float& st = isInput ? inShort : outShort;
        st = aShort * st + (1.0f - aShort) * ms;
        if (isInput) { inBlock = ms; return; }

        constexpr float kGate = 1.0e-7f;   // -70 dB: below this, hold
        if (inShort > kGate && outShort > kGate)
        {
            const float aLong = std::exp (-(float) n / (3.0f * (float) sr));
            if (inLong <= 0.0f || outLong <= 0.0f) { inLong = inShort; outLong = outShort; }   // first sound: start there
            inLong = aLong * inLong + (1.0f - aLong) * inBlock;
            outLong = aLong * outLong + (1.0f - aLong) * ms;
            autoGainDb = juce::jlimit (-18.0f, 18.0f, 10.0f * std::log10 (inLong / outLong));
        }
    }

    // Envelope follower: a peak follower on the input (stereo-linked) with attack/release set
    // by Speed, compared with the material's own ~3 s average level. Rising above that average
    // adds Drive (positive Amount) or takes it away (negative): 4 Drive units per dB at 100 %,
    // capped at +/-50. The knob is the resting point; relative to the average, so it works the
    // same whatever the track level.
    void followEnvelope (const juce::dsp::AudioBlock<float>& block, int nch, int n)
    {
        const float sp = juce::jlimit (0.0f, 1.0f, params.envSpeed / 100.0f);
        const float atkMs = 60.0f * std::pow (1.0f / 60.0f, sp);    // 60 ms (slow) .. 1 ms (fast)
        const float relMs = 1500.0f * std::pow (25.0f / 1500.0f, sp); // 1.5 s .. 25 ms
        const float aA = std::exp (-1.0f / (atkMs * 0.001f * (float) sr));
        const float aR = std::exp (-1.0f / (relMs * 0.001f * (float) sr));
        for (int i = 0; i < n; ++i)
        {
            float x = 0.0f;
            for (int c = 0; c < nch; ++c) x = juce::jmax (x, std::abs (block.getSample (c, i)));
            envLin = x > envLin ? aA * envLin + (1.0f - aA) * x : aR * envLin + (1.0f - aR) * x;
        }
    }

    // Once per 64-sample grid point (after that chunk is processed): the new Drive offset,
    // used from the next chunk on. Fixed grid = same result at any host buffer size.
    void updateEnvOffset()
    {
        smEnvAmt.setTargetValue (params.envAmount);
        const float amt = smEnvAmt.skip (kEnvChunk);
        const float envDb = juce::Decibels::gainToDecibels (envLin, -100.0f);
        if (envDb > -60.0f)   // average only while there's signal (holds through silence)
        {
            const float aAvg = std::exp (-(float) kEnvChunk / (3.0f * (float) sr));
            envAvgDb = envAvgDb < -90.0f ? envDb : aAvg * envAvgDb + (1.0f - aAvg) * envDb;
        }
        // The Drive knob is the floor (positive Amount) or ceiling (negative): the follower only
        // pushes away from it on the moments that rise above the material's average, and comes
        // back to exactly the knob setting, never past it.
        const float rise = juce::jmax (0.0f, envDb - envAvgDb);
        envOffset = (std::abs (amt) < 0.01f || envAvgDb < -90.0f || envDb < -60.0f) ? 0.0f
                    : juce::jlimit (-50.0f, 50.0f, amt / 100.0f * 4.0f * rise);
    }

public:
    float getModulatedDrive() const { return modDrive.load(); }   // UI: Drive incl. envelope
private:

    // EQ band solo: while you hold Shift on an EQ point, the output is band-passed to that
    // band's region (bell: band-pass at its frequency and Q; low shelf: low-pass; high shelf:
    // high-pass) so you hear exactly what it's working on. Fades in/out over 20 ms.
    void applyBandSolo (juce::dsp::AudioBlock<float>& block, int nch, int n)
    {
        const int path = eqSoloPath.load(), band = eqSoloBand.load();
        const bool want = path > 0 && band >= 0 && band < 4;
        if (! want && soloMix <= 0.0f) return;
        if (want)
        {
            const EqSettings& e = path == 1 ? params.dry.eq : path == 2 ? params.wet.eq : params.preEq;
            const double f = juce::jlimit (20.0, 0.45 * sr, (double) e.freq[band]);
            const double q = juce::jlimit (0.5, 10.0, (double) e.q[band]);
            Biquad proto;
            if (band == 0 && e.lowShelf)       proto.setLowPass (f, 0.70710678, sr);
            else if (band == 3 && e.highShelf) proto.setHighPass (f, 0.70710678, sr);
            else                               proto.setBandPass (f, q, sr);
            for (auto& b : soloBp) { b.b0 = proto.b0; b.b1 = proto.b1; b.b2 = proto.b2; b.a1 = proto.a1; b.a2 = proto.a2; }
            if (soloMix <= 0.0f) for (auto& b : soloBp) b.reset();
        }
        const float step = (float) (1.0 / (0.02 * sr)) * (want ? 1.0f : -1.0f);
        float m0 = soloMix;
        for (int c = 0; c < nch && c < 2; ++c)
        {
            auto* x = block.getChannelPointer ((size_t) c);
            float m = m0;
            for (int i = 0; i < n; ++i)
            {
                m = juce::jlimit (0.0f, 1.0f, m + step);
                const double v = x[i];
                x[i] = (float) (v + m * (soloBp[(size_t) c].process (v) - v));
            }
            soloMix = m;
        }
    }

public:
    // UI: solo an EQ band (path 1 = dry strip EQ, 2 = wet strip EQ, 3 = pre EQ; band 0..3), or -1 = off
    void setEqSolo (int path, int band) { eqSoloBand = band; eqSoloPath = band < 0 ? 0 : path; }
private:

    struct Channel
    {
        Biquad hp1, hp2, bump, dip, dc;    // fixed (measured) EQ
        Biquad inter; // interstage coupling cap, runs at the oversampled rate
        std::array<Biquad, DryPhase::numSecondOrder> dryPhase;   // dry-path phase match
    };

    double sr = 48000;
    int channels = 2;
    std::vector<Channel> ch;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    std::unique_ptr<juce::dsp::Oversampling<float>> dryOversampler;
    juce::AudioBuffer<float> dryBuf;
    ChannelStrip dryStrip, wetStrip;

public:
    // meters for the UI (call from the message thread)
    float takeDryPeak() { return dryStrip.takePeak(); }
    float takeWetPeak() { return wetStrip.takePeak(); }
    float dryGainReductionDb() const { return dryStrip.getGainReductionDb(); }
    float wetGainReductionDb() const { return wetStrip.getGainReductionDb(); }
    float dryMakeupDb() const { return dryStrip.getMakeupDb(); }
    float wetMakeupDb() const { return wetStrip.getMakeupDb(); }
private:
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smPre { 1.0f }, smPost { 1.0f }, smOut { 1.0f };
    juce::SmoothedValue<float> smDrive, smDensity, smEven, smOdd;
    bool firstBlock = true;
    // user HPF + pre/post shelves: plans, the wet-path filters, and their dry-path all-passes
    Coherent::Stage hpfStage, preStage, postStage;
    Coherent::Main hpfMain, preMain, postMain;
    Coherent::Allpass hpfAp, preAp, postAp;
    float oddMakeup = 1.0f, oddPowAvg = 0.0f, refPowAvg = 0.0f;
    Params params;
    KneeTable knee1, knee2;   // input stage (fixed), output stage (follows Drive)
    std::vector<KneeTable> kneeCache;   // output-stage curve for Drive 0..100, used while the follower moves Drive
    FourBandEQ preEqF, postEqF;         // expanded pre EQ (wet path), and its Link mirror after the tubes
    static constexpr int kEnvChunk = 64;
    juce::int64 sampleCount = 0;
    float envLin = 0.0f, envAvgDb = -100.0f, envOffset = 0.0f;
    juce::SmoothedValue<float> smEnvAmt;
    std::atomic<float> modDrive { 25.0f };
    bool dryStale = true;     // dry filters skipped while the Dry fader is off
    std::array<std::array<Biquad, 2>, 2> kIn, kOut;   // K-weighting for auto gain (in / out, per channel)
    float inShort = 0.0f, outShort = 0.0f, inBlock = 0.0f, inLong = 0.0f, outLong = 0.0f;
    float autoGainDb = 0.0f, autoApplied = 0.0f;
    std::atomic<float> autoMeter { 0.0f };
    std::atomic<float> drivenMeter { 0.0f };
    std::atomic<int> eqSoloPath { 0 }, eqSoloBand { -1 };
    std::array<Biquad, 2> soloBp;
    float soloMix = 0.0f;
public:
    int testSolo = 0;         // offline measurement only: 1 = output wet path only, 2 = dry only
};
