#pragma once
#include <juce_dsp/juce_dsp.h>
#include "Biquad.h"
#include "DriveMap.h"

// The whole signal path, independent of the plugin wrapper so the offline
// render tool (tools/RenderTool.cpp) runs the exact same code the plugin does.
//
//  in ─┬─ input coupling (HPFs) → low bump → presence dip → Tone emphasis
//      │   → [4x oversampled: input tube stage → coupling cap → Drive gain → output clipper]
//      │   → Tone de-emphasis → DC blocker → postGain ─┐
//      └──────────────── dry (latency-matched) ────────┴─ Mix → Output
class GlowDSP
{
public:
    struct Params
    {
        float drive = 25.0f;    // 0..100
        float tone = 0.0f;      // -100..100  (negative = lows saturate more, positive = highs)
        float density = 50.0f;  // 0..100     input-stage heat: 50 = measured, 0 = clean, 100 = +12 dB hotter
        float mix = 100.0f;     // 0..100 %
        float outputDb = 0.0f;  // -24..+12 dB
    };

    void prepare (double sampleRate, int maxBlock, int numChannels)
    {
        sr = sampleRate;
        channels = juce::jmax (1, numChannels);

        oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
            (size_t) channels, kOversampleStages,
            juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
        oversampler->initProcessing ((size_t) maxBlock);

        juce::dsp::ProcessSpec spec { sr, (juce::uint32) maxBlock, (juce::uint32) channels };
        dryWet.prepare (spec);
        dryWet.setMixingRule (juce::dsp::DryWetMixingRule::linear);
        dryWet.setWetLatency ((float) getLatencySamples());

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
            c.emph.setIdentity();
            c.deemph.setIdentity();
        }

        smPre.reset (sr, 0.05);
        smPost.reset (sr, 0.05);
        smOut.reset (sr, 0.05);
        smDrive.reset (sr, 0.05);
        smDensity.reset (sr, 0.05);
        firstBlock = true;
        lastTone = -1000.0f;
        reset();
    }

    void reset()
    {
        if (oversampler) oversampler->reset();
        dryWet.reset();
        for (auto& c : ch)
        {
            c.hp1.reset(); c.hp2.reset(); c.bump.reset(); c.dip.reset();
            c.emph.reset(); c.deemph.reset(); c.dc.reset(); c.inter.reset();
        }
    }

    int getLatencySamples() const
    {
        return oversampler ? (int) std::round (oversampler->getLatencyInSamples()) : 0;
    }

    void setParams (const Params& p)
    {
        params = p;
        smDrive.setTargetValue (p.drive);
        smDensity.setTargetValue (p.density);
        smOut.setTargetValue (juce::Decibels::decibelsToGain (p.outputDb));
        dryWet.setWetMixProportion (p.mix / 100.0f);

        // Tone: shelf emphasis before the clipper, mirrored de-emphasis after.
        // Highs (or lows) hit the clipper harder, then the overall balance is restored.
        if (std::abs (p.tone - lastTone) > 1.0e-4f)
        {
            lastTone = p.tone;
            const double g = 9.0 * (double) p.tone / 100.0; // up to +/-9 dB into the clipper
            for (auto& c : ch)
            {
                if (std::abs (g) < 1e-3) { c.emph.setIdentity(); c.deemph.setIdentity(); }
                else
                {
                    c.emph.setHighShelf (2500.0, g, 0.6, sr);
                    c.deemph.setHighShelf (2500.0, -g, 0.6, sr);
                }
            }
        }
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
            smOut.setCurrentAndTargetValue (smOut.getTargetValue());
            dryWet.reset(); // snaps the mixer's smoothed dry/wet gains to the current Mix
        }

        dryWet.pushDrySamples (block);

        // drive/density are smoothed per block; gains are ramped per sample
        const auto shape = DriveMap::at (smDrive.skip (n));
        // Density scales how hard the input stage is hit: 50 % = as measured,
        // 100 % = +12 dB hotter (more lopsided, more "breathing"), 0 % = clean input stage.
        const float dn = smDensity.skip (n) / 50.0f;
        const float amount = juce::jmax (dn * dn, 1.0e-3f);
        const float k1 = DriveMap::st1.inGain * amount;   // into the input stage
        const float k1inv = 1.0f / k1;                     // and back to unity
        smPre.setTargetValue (juce::Decibels::decibelsToGain (shape.preDb));
        smPost.setTargetValue (juce::Decibels::decibelsToGain (shape.postDb));
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
                v = s.hp1.process (v);
                v = s.hp2.process (v);
                v = s.bump.process (v);
                v = s.dip.process (v);
                v = s.emph.process (v);
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
        for (int c = 0; c < nch; ++c)
        {
            auto* u = up.getChannelPointer ((size_t) c);
            auto& s = ch[(size_t) c];
            float g = g0;
            for (int i = 0; i < nu; ++i, g += gStep)
            {
                // input stage: unity for small signals, bends lopsidedly when hit hard
                const float w = shapeStage (u[i] * k1, DriveMap::st1.sharp, DriveMap::st1.asym, DriveMap::st1.k2);
                // coupling cap: its DC shift is what makes hot passages clip lopsided,
                // and its recovery is the ~half-second "breathing" after loud hits
                const float v = (float) s.inter.process ((double) w) * k1inv;
                // drive into the output stage
                u[i] = shapeStage (v * g, shape.sharp, shape.asym, shape.k2);
            }
        }
        oversampler->processSamplesDown (block);

        // ---- base rate: de-emphasis, DC block, output staging ----
        for (int c = 0; c < nch; ++c)
        {
            auto* y = block.getChannelPointer ((size_t) c);
            auto& s = ch[(size_t) c];
            auto g = smPost;
            for (int i = 0; i < n; ++i)
            {
                double v = s.deemph.process ((double) y[i]);
                v = s.dc.process (v);
                y[i] = (float) (v * g.getNextValue());
            }
        }
        smPost.skip (n);

        dryWet.mixWetSamples (block);

        for (int c = 0; c < nch; ++c)
        {
            auto* y = block.getChannelPointer ((size_t) c);
            auto g = smOut;
            for (int i = 0; i < n; ++i) y[i] *= g.getNextValue();
        }
        smOut.skip (n);
    }

    // One tube stage: bounded even-order bend (k2) then an asymmetric soft clip with
    // knee hardness 'sharp'. Mirrors the shaper in model/model.py exactly.
    static inline float shapeStage (float u, float sharp, float asym, float k2) noexcept
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
    static constexpr size_t kOversampleStages = 2; // 2^2 = 4x

    struct Channel
    {
        Biquad hp1, hp2, bump, dip, emph, deemph, dc;
        Biquad inter; // interstage coupling cap, runs at the oversampled rate
    };

    double sr = 48000;
    int channels = 2;
    std::vector<Channel> ch;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    juce::dsp::DryWetMixer<float> dryWet { 4096 };
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smPre { 1.0f }, smPost { 1.0f }, smOut { 1.0f };
    juce::SmoothedValue<float> smDrive, smDensity;
    bool firstBlock = true;
    float lastTone = -1000.0f;
    Params params;
};
