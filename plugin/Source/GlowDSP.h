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
//      │   → DC blocker → auto-gain → post high shelf → WET strip (EQ → Comp → fader) ─┐
//      └─ dry → same oversampling filters (no saturation) → phase-match all-pass → DRY strip ─┴─ sum → Output
//
// Dry/Wet phase: the dry path goes through an identical copy of the oversampling filters, so
// its latency and top-end phase match the wet path exactly, plus an all-pass (flat magnitude)
// fitted to the wet path's fixed low-end filters. Dry + Wet therefore sum without cancelling.
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
    struct Params
    {
        float drive = 25.0f;    // 0..100     macro: more of both even and odd
        float even = 50.0f;     // 0..100     50 = as measured, 0 = no even harmonics, 100 = 3x
        float odd = 50.0f;      // 0..100     50 = as measured, -/+12 dB of clipping depth at 0/100
        float density = 50.0f;  // 0..100     input-stage heat: 50 = measured, 0 = clean, 100 = +12 dB hotter
        float hpfHz = 10.0f;    // 10..1000   pre-saturation high-pass, 10 = off
        float preShelfHz = 5000.0f, preShelfDb = 0.0f;    // pre-saturation high shelf
        float postShelfHz = 5000.0f, postShelfDb = 0.0f;  // post-saturation high shelf
        float outputDb = 0.0f;  // -24..+12 dB
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
        for (auto& c : ch)
        {
            c.hp1.setOnePoleHighPass (DriveMap::eq.hp1Fc, sr);
            c.hp2.setHighPass (DriveMap::eq.hpFc, DriveMap::eq.hpQ, sr);
            c.bump.setPeak (DriveMap::eq.bumpFc, DriveMap::eq.bumpDb, DriveMap::eq.bumpQ, sr);
            c.dip.setPeak (DriveMap::eq.dipFc, DriveMap::eq.dipDb, DriveMap::eq.dipQ, sr);
            c.dc.setHighPass (DriveMap::post.dcFc, DriveMap::post.dcQ, sr);
            c.inter.setHighPass (DriveMap::st1.hpFc, DriveMap::st1.hpQ,
                                 sr * (double) oversampler->getOversamplingFactor());
            c.hpf.setIdentity();
            c.preShelf.setIdentity();
            c.postShelf.setIdentity();
            for (int k = 0; k < DryPhase::numSecondOrder; ++k)
                c.dryPhase[(size_t) k].setAllPass (DryPhase::secondOrder[k].fc, DryPhase::secondOrder[k].q, sr);
        }
        knee1.build (DriveMap::st1.sharp);
        knee2.build (DriveMap::at (params.drive).sharp);
        hpfOn = false;
        lastHpf = lastPreHz = lastPreDb = lastPostHz = lastPostDb = -1.0e9f;

        smPre.reset (sr, 0.05);
        smPost.reset (sr, 0.05);
        smOut.reset (sr, 0.05);
        smDrive.reset (sr, 0.05);
        smDensity.reset (sr, 0.05);
        smEven.reset (sr, 0.05);
        smOdd.reset (sr, 0.05);
        for (auto* v : { &smHpf, &smPreHz, &smPreDb, &smPostHz, &smPostDb })
            v->reset (sr, 0.05);
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
            c.hpf.reset(); c.preShelf.reset(); c.postShelf.reset();
            for (auto& f : c.dryPhase) f.reset();
        }
        oddMakeup = 1.0f;
        oddPowAvg = refPowAvg = 0.0f;
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
        smHpf.setTargetValue (p.hpfHz);
        smPreHz.setTargetValue (p.preShelfHz);
        smPreDb.setTargetValue (p.preShelfDb);
        smPostHz.setTargetValue (p.postShelfHz);
        smPostDb.setTargetValue (p.postShelfDb);
        smOut.setTargetValue (juce::Decibels::decibelsToGain (p.outputDb));
    }

    void process (juce::dsp::AudioBlock<float>& block)
    {
        const int n = (int) block.getNumSamples();
        const int nch = juce::jmin ((int) block.getNumChannels(), channels);
        if (n == 0) return;

        if (firstBlock)
        {
            // start exactly at the current settings instead of ramping in from defaults
            smDrive.setCurrentAndTargetValue (params.drive);
            smDensity.setCurrentAndTargetValue (params.density);
            smEven.setCurrentAndTargetValue (params.even);
            smOdd.setCurrentAndTargetValue (params.odd);
            for (auto* v : { &smHpf, &smPreHz, &smPreDb, &smPostHz, &smPostDb })
                v->setCurrentAndTargetValue (v->getTargetValue());
            smOut.setCurrentAndTargetValue (smOut.getTargetValue());
        }

        // dry path: copy of the input through the same oversampling filters + phase-match all-pass
        auto dry = juce::dsp::AudioBlock<float> (dryBuf).getSubsetChannelBlock (0, (size_t) nch).getSubBlock (0, (size_t) n);
        const bool dryActive = params.dry.levelDb > ChannelStrip::kOffDb || ! dryStrip.isMutedSteady();
        if (dryActive)
        {
            if (dryStale)   // fader just came up: start the dry filters from a clean state
            {
                dryOversampler->reset();
                for (auto& c : ch) for (auto& f : c.dryPhase) f.reset();
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
        }
        else
        {
            dry.clear();
            dryStale = true;
        }

        // drive/density are smoothed per block; gains are ramped per sample
        const auto shape = DriveMap::at (smDrive.skip (n));
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
        updateFilters (n);
        if (firstBlock)
        {
            smPre.setCurrentAndTargetValue (smPre.getTargetValue());
            smPost.setCurrentAndTargetValue (smPost.getTargetValue());
            firstBlock = false;
        }

        // ---- base rate: pre-EQ ----
        for (int c = 0; c < nch; ++c)
        {
            auto* x = block.getChannelPointer ((size_t) c);
            auto& s = ch[(size_t) c];
            for (int i = 0; i < n; ++i)
            {
                double v = (double) x[i];
                if (hpfOn) v = s.hpf.process (v);
                v = s.preShelf.process (v);
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
        if (knee2.needs (shape.sharp)) knee2.build (shape.sharp);   // only while Drive moves
        double powOdd = 0.0, powRef = 0.0;   // block energy: actual vs nominal-Odd output stage
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
                const float y = stage (v * g, knee2, shape.asym, shape.k2, evenGain);
                if (oddActive && (i & 3) == 0)
                {
                    // silent reference: the output stage at nominal Odd. Only a loudness estimate
                    // is needed, so every 4th oversampled sample is plenty.
                    const float yRef = stage (v * g * oddInv, knee2, shape.asym, shape.k2, evenGain);
                    powOdd += (double) y * y;
                    powRef += (double) yRef * yRef;
                }
                u[i] = y;
            }
        }
        oversampler->processSamplesDown (block);

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
                double v = s.dc.process ((double) y[i]);
                v = s.postShelf.process (v * g.getNextValue());
                y[i] = (float) v;
            }
        }
        smPost.skip (n);

        // channel strips, then sum (a muted dry path costs nothing)
        auto wetBlock = block.getSubsetChannelBlock (0, (size_t) nch);
        wetStrip.process (wetBlock, params.wet);
        dryStrip.process (dry, params.dry);
        wetBlock.add (dry);

        for (int c = 0; c < nch; ++c)
        {
            auto* y = block.getChannelPointer ((size_t) c);
            auto g = smOut;
            for (int i = 0; i < n; ++i) y[i] *= g.getNextValue();
        }
        smOut.skip (n);
    }

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

    // Smoothed per block; coefficients only recomputed when a value actually moves.
    void updateFilters (int n)
    {
        const float hz = smHpf.skip (n);
        const float preHz = smPreHz.skip (n), preDb = smPreDb.skip (n);
        const float postHz = smPostHz.skip (n), postDb = smPostDb.skip (n);
        const double nyq = 0.45 * sr;

        if (std::abs (hz - lastHpf) > 1.0e-3f)
        {
            lastHpf = hz;
            const bool wasOn = hpfOn;
            hpfOn = hz > 10.5f;   // bottom of the range = off
            for (auto& c : ch)
            {
                if (hpfOn && ! wasOn) c.hpf.reset();   // no stale state when switching back on
                if (hpfOn) c.hpf.setHighPass (juce::jmin ((double) hz, nyq), 0.70710678, sr);
            }
        }
        if (std::abs (preHz - lastPreHz) > 1.0e-2f || std::abs (preDb - lastPreDb) > 1.0e-4f)
        {
            lastPreHz = preHz; lastPreDb = preDb;
            for (auto& c : ch)
                if (std::abs (preDb) < 1.0e-3f) c.preShelf.setIdentity();
                else c.preShelf.setHighShelf (juce::jmin ((double) preHz, nyq), preDb, 0.70710678, sr);
        }
        if (std::abs (postHz - lastPostHz) > 1.0e-2f || std::abs (postDb - lastPostDb) > 1.0e-4f)
        {
            lastPostHz = postHz; lastPostDb = postDb;
            for (auto& c : ch)
                if (std::abs (postDb) < 1.0e-3f) c.postShelf.setIdentity();
                else c.postShelf.setHighShelf (juce::jmin ((double) postHz, nyq), postDb, 0.70710678, sr);
        }
    }

    struct Channel
    {
        Biquad hpf, preShelf, postShelf;   // user EQ
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
private:
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smPre { 1.0f }, smPost { 1.0f }, smOut { 1.0f };
    juce::SmoothedValue<float> smDrive, smDensity, smEven, smOdd;
    juce::SmoothedValue<float> smHpf, smPreHz, smPreDb, smPostHz, smPostDb;
    bool firstBlock = true, hpfOn = false;
    float oddMakeup = 1.0f, oddPowAvg = 0.0f, refPowAvg = 0.0f;
    float lastHpf = -1.0e9f, lastPreHz = -1.0e9f, lastPreDb = -1.0e9f, lastPostHz = -1.0e9f, lastPostDb = -1.0e9f;
    Params params;
    KneeTable knee1, knee2;   // input stage (fixed), output stage (follows Drive)
    bool dryStale = true;     // dry filters skipped while the Dry fader is off
};
