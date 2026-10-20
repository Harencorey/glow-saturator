#pragma once
#include <juce_dsp/juce_dsp.h>
#include "Biquad.h"
#include "PhaseCoherent.h"

#ifndef GLOW_STRIP_OS_MAXQ
 #define GLOW_STRIP_OS_MAXQ false   // lighter filters: lower latency for the colour stage
#endif

// Per-path processing for the Dry and Wet signals: 4-band EQ -> compressor -> fader.
// Everything here is real-time safe: no allocation after prepare(), no locks.

//==============================================================================
struct EqSettings
{
    bool on = false;
    float freq[4] { 80.0f, 400.0f, 2500.0f, 10000.0f };
    float gain[4] { 0.0f, 0.0f, 0.0f, 0.0f };
    float q[4] { 0.7f, 1.0f, 1.0f, 0.7f };
    bool lowShelf = true;    // band 1: shelf or bell
    bool highShelf = true;   // band 4: shelf or bell
    bool bandOn[4] { true, true, true, true };   // per-band bypass (double-click a point)
};

// 4-band EQ built from phase-coherent bands (see PhaseCoherent.h). The path that owns the
// EQ runs the bands; the other path runs their all-passes (allpassOnOtherPath), so the two
// stay in phase whatever this EQ does.
class FourBandEQ
{
public:
    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        for (int b = 0; b < 4; ++b) stages[(size_t) b].prepare (sr, maxBlock, kindFor (b, true, true));
        reset();
    }

    void reset() { for (auto& m : main) m.reset(); for (auto& a : comp) a.reset(); for (auto& a : comp2) a.reset(); }
    void snap() { for (auto& st : stages) st.snapNext(); }

    // Once per block, before either path is processed. 'allowed' = this path is audible.
    // coherent: phase-lock to the other path (it's audible); otherwise ordinary EQ curves
    void plan (const EqSettings& s, bool allowed, int n, bool coherent = true)
    {
        for (int b = 0; b < 4; ++b)
        {
            auto& st = stages[(size_t) b];
            const auto k = kindFor (b, s.lowShelf, s.highShelf);
            if (k != st.getKind())
            {
                // shelf <-> bell: different filter, so start it fresh (the toggle is a click anyway)
                st.setKind (k);
                st.snapNext();
            }
            st.plan (s.on && allowed && s.bandOn[b], s.freq[b], s.gain[b], s.q[b], n, coherent);
        }
    }

    // own path: the bands
    void process (juce::dsp::AudioBlock<float>& block)
    {
        for (int b = 0; b < 4; ++b)
            if (stages[(size_t) b].busy()) main[(size_t) b].process (stages[(size_t) b], block);
    }

    // the other path: this EQ's all-passes (state lives here, one set per EQ)
    void allpassOnOtherPath (juce::dsp::AudioBlock<float>& block)
    {
        for (int b = 0; b < 4; ++b)
            if (stages[(size_t) b].busy()) comp[(size_t) b].process (stages[(size_t) b], block);
    }
    // a third path (Keep Lows' clean lows, added back to this EQ's path): the same all-passes,
    // with their own state
    void allpassOnKeepPath (juce::dsp::AudioBlock<float>& block)
    {
        for (int b = 0; b < 4; ++b)
            if (stages[(size_t) b].busy()) comp2[(size_t) b].process (stages[(size_t) b], block);
    }
    void resetKeepAllpass() { for (auto& a : comp2) a.reset(); }
    void resetMain() { for (auto& m : main) m.reset(); }
    void resetAllpass() { for (auto& a : comp) a.reset(); }

    // Magnitude response in dB at frequency f (for drawing the curve in the UI).
    static float responseDb (const EqSettings& s, double f, double sampleRate = 48000.0)
    {
        double total = 0.0;
        for (int b = 0; b < 4; ++b)
            if (s.bandOn[b] && std::abs (s.gain[b]) >= 0.01f)
                total += Coherent::Stage::responseDb (kindFor (b, s.lowShelf, s.highShelf), s.freq[b], s.gain[b],
                                                      juce::jmax (0.1f, s.q[b]), f, sampleRate);
        return (float) total;
    }

private:
    static Coherent::Kind kindFor (int b, bool lowShelf, bool highShelf)
    {
        if (b == 0 && lowShelf) return Coherent::Kind::lowShelf;
        if (b == 3 && highShelf) return Coherent::Kind::highShelf;
        return Coherent::Kind::peak;
    }

    double sr = 48000.0;
    std::array<Coherent::Stage, 4> stages;
    std::array<Coherent::Main, 4> main;
    std::array<Coherent::Allpass, 4> comp, comp2;
};

//==============================================================================
// Input-driven compressor: no threshold control - Input drives the signal into a
// fixed threshold, Output sets the level after. Ratio buttons with knee that gets harder
// as the ratio goes up, a "Nuke" brickwall setting, optional detector high-pass (so lows
// don't pump the comp), and two distortion colours.
struct CompSettings
{
    bool on = false;
    float inputDb = 0.0f;     // -12 .. +36 dB into the comp
    float attackMs = 5.0f;    // 0.05 .. 30 ms
    float releaseMs = 200.0f; // 50 .. 3500 ms
    int ratio = 3;            // index into kRatios: 1:1 2 3 4 6 10 20 Nuke
    float outputDb = 0.0f;    // -24 .. +24 dB
    int colour = 0;           // 0 clean, 1 Dist 2 (2nd harmonic), 2 Dist 3 (3rd harmonic)
    bool detectorHpf = false; // 100 Hz high-pass on the detector only
    float mix = 100.0f;       // 0..100 %: parallel blend of the uncompressed signal
    bool autoMakeup = false;  // automatic makeup gain from Input + Ratio (Output trims on top)
};

class Compressor
{
public:
    static constexpr int kNumRatios = 8;
    static inline const char* ratioNames[kNumRatios] { "1:1", "2:1", "3:1", "4:1", "6:1", "10:1", "20:1", "NUKE" };
    static inline const char* colourNames[3] { "CLEAN", "DIST 2", "DIST 3" };

    // osFactor: the strip's oversampling factor (gain, colour and Mix all run oversampled)
    void prepare (double sampleRate, int numChannels, int osFactor, int maxBlock)
    {
        sr = sampleRate;
        nch = juce::jlimit (1, kMaxCh, numChannels);
        factor = juce::jmax (1, osFactor);
        for (auto& f : scHpf) f.setHighPass (100.0, 0.70710678, sr);
        for (auto& f : dcBlock) f.setOnePoleHighPass (5.0, sr * factor);
        gComp.assign ((size_t) maxBlock + 1, 1.0f);
        gOut.assign ((size_t) maxBlock + 1, 1.0f);
        gMix.assign ((size_t) maxBlock + 1, 0.0f);
        smIn.reset (sr, 0.03); smOut.reset (sr, 0.03);
        smMix.reset (sr, 0.02);
        smMix.setCurrentAndTargetValue (0.0f);
        reset();
        active = false;
    }

    void reset()
    {
        for (auto& f : scHpf) f.reset();
        for (auto& f : dcBlock) f.reset();
        grDb = 0.0f;
        env = 0.0f;
        powIn = powOut = 0.0;
        lastComp = lastOut = 1.0f;
        lastMix = smMix.getCurrentValue();
    }

    // Base rate: detector + gain computer. Fills this block's gain curves and returns true if
    // the comp does anything this block (it fades in/out over 20 ms when switched).
    bool analyse (const juce::dsp::AudioBlock<float>& block, const CompSettings& s)
    {
        smMix.setTargetValue (s.on ? juce::jlimit (0.0f, 1.0f, s.mix * 0.01f) : 0.0f);
        if (! s.on && ! smMix.isSmoothing())
        {
            smMix.setCurrentAndTargetValue (0.0f);
            meterGr.store (0.0f);
            active = false;
            return false;
        }
        if (! active)
        {
            // switching on: clean detector, gains straight to their settings, Mix fades in
            active = true;
            smIn.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (s.inputDb));
            reset();
            makeupNow = -s.inputDb;   // first guess until it has measured the material
            smOut.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputGainDb (s)));
        }

        const int n = (int) block.getNumSamples();
        const int ch = juce::jmin ((int) block.getNumChannels(), nch);
        smIn.setTargetValue (juce::Decibels::decibelsToGain (s.inputDb));
        smOut.setTargetValue (juce::Decibels::decibelsToGain (outputGainDb (s)));

        const int r = juce::jlimit (0, kNumRatios - 1, s.ratio);
        const float ratio = kRatio[r];
        const float knee = kKnee[r];
        const float slope = 1.0f - 1.0f / ratio;            // 0 at 1:1, ~1 at Nuke
        const bool nuke = r == kNumRatios - 1;
        // Nuke: brickwall-fast attack regardless of the knob, like slamming the real thing
        const float atkMs = nuke ? juce::jmin (s.attackMs, 0.1f) : s.attackMs;
        const float aAtk = std::exp (-1.0f / (juce::jmax (0.02f, atkMs) * 0.001f * (float) sr));
        const float aRel = std::exp (-1.0f / (juce::jmax (10.0f, s.releaseMs) * 0.001f * (float) sr));
        const float aEnv = std::exp (-1.0f / (0.010f * (float) sr));

        float minGain = 0.0f;
        double sumIn = 0.0, sumOut = 0.0;   // for auto makeup: level before vs after compression
        for (int i = 0; i < n; ++i)
        {
            const float gIn = smIn.getNextValue();

            // stereo-linked detector (peak of all channels, optionally high-passed)
            float det = 0.0f;
            for (int c = 0; c < ch; ++c)
            {
                double d = (double) block.getSample (c, i) * gIn;
                if (s.detectorHpf) d = scHpf[(size_t) c].process (d);
                det = juce::jmax (det, (float) std::abs (d));
            }

            // peak-hold envelope (instant rise, ~10 ms fall) so the gain follows the
            // signal's envelope rather than every individual waveform peak
            env = det > env ? det : env * aEnv;

            const float target = staticCurve (juce::Decibels::gainToDecibels (env, -120.0f), slope, knee);

            // attack when more reduction is needed, release when less
            grDb = target < grDb ? aAtk * grDb + (1.0f - aAtk) * target
                                 : aRel * grDb + (1.0f - aRel) * target;
            minGain = juce::jmin (minGain, grDb);
            gComp[(size_t) i] = juce::Decibels::decibelsToGain (grDb) * gIn;
            for (int c = 0; c < ch; ++c)
            {
                const double xin = block.getSample (c, i);
                sumIn += xin * xin;
                sumOut += xin * xin * (double) gComp[(size_t) i] * gComp[(size_t) i];
            }
            gOut[(size_t) i] = smOut.getNextValue();
            gMix[(size_t) i] = smMix.getNextValue();
        }
        meterGr.store (minGain);

        // Auto makeup, measured: the level going in vs. the level after Input + compression,
        // averaged over ~1.5 s (only while there's signal), so whatever the compressor takes away
        // comes back. Slow on purpose: it matches loudness, it can't pump.
        const double pin = sumIn / juce::jmax (1, n * ch), pout = sumOut / juce::jmax (1, n * ch);
        if (pin > 1.0e-8)
        {
            if (powIn <= 0.0 || powOut <= 0.0) { powIn = pin; powOut = pout; }
            const double a = std::exp (-(double) n / (1.5 * sr));
            powIn = a * powIn + (1.0 - a) * pin;
            powOut = a * powOut + (1.0 - a) * pout;
            if (powOut > 1.0e-20)
                makeupNow = juce::jlimit (-36.0f, 30.0f, (float) (10.0 * std::log10 (powIn / powOut)));
        }
        meterMakeup.store (s.autoMakeup ? makeupNow : 0.0f);
        return true;
    }

    // Oversampled: gain -> colour -> output, blended with the untouched signal by Mix.
    // Both sides of the blend come from the same oversampled signal, so the parallel
    // blend is phase-exact (the gain curves are interpolated up to the oversampled rate).
    void process (juce::dsp::AudioBlock<float>& up, const CompSettings& s)
    {
        const int nu = (int) up.getNumSamples();
        const int n = nu / factor;
        const int ch = juce::jmin ((int) up.getNumChannels(), nch);
        const float inv = 1.0f / (float) factor;
        for (int c = 0; c < ch; ++c)
        {
            auto* x = up.getChannelPointer ((size_t) c);
            auto& dc = dcBlock[(size_t) c];
            float pc = lastComp, po = lastOut, pm = lastMix;
            for (int i = 0; i < n; ++i)
            {
                const float dcmp = (gComp[(size_t) i] - pc) * inv, dout = (gOut[(size_t) i] - po) * inv, dmix = (gMix[(size_t) i] - pm) * inv;
                for (int k = 0; k < factor; ++k)
                {
                    pc += dcmp; po += dout; pm += dmix;
                    const float u = x[i * factor + k];
                    const float y = colourise (u * pc, s.colour, dc) * po;
                    x[i * factor + k] = u + pm * (y - u);
                }
                pc = gComp[(size_t) i]; po = gOut[(size_t) i]; pm = gMix[(size_t) i];
            }
        }
        if (n > 0) { lastComp = gComp[(size_t) n - 1]; lastOut = gOut[(size_t) n - 1]; lastMix = gMix[(size_t) n - 1]; }
    }

    float getGainReductionDb() const { return meterGr.load(); }

    // UI: the makeup gain Auto Makeup is applying right now, in dB (0 when off)
    float getMakeupDb() const { return meterMakeup.load(); }

private:
    float outputGainDb (const CompSettings& s) const
    {
        return s.outputDb + (s.autoMakeup ? makeupNow : 0.0f);
    }

    // static curve (soft knee): gain change in dB (<= 0) for a detector level in dB
    static float staticCurve (float xDb, float slope, float knee)
    {
        const float over = xDb - kThresholdDb;
        if (slope <= 0.0f) return 0.0f;
        if (2.0f * over > knee) return -slope * over;
        if (knee > 0.0f && 2.0f * over > -knee) return -slope * (over + 0.5f * knee) * (over + 0.5f * knee) / (2.0f * knee);
        return 0.0f;
    }

    static constexpr int kMaxCh = 2;
    static constexpr float kThresholdDb = -18.0f;    // fixed: Input sets how hard you hit it
    static constexpr float kRatio[kNumRatios] { 1.0f, 2.0f, 3.0f, 4.0f, 6.0f, 10.0f, 20.0f, 1000.0f };
    static constexpr float kKnee[kNumRatios]  { 0.0f, 12.0f, 10.0f, 8.0f, 6.0f, 4.0f, 2.0f, 0.0f };   // dB

    // Distortion colours. Bounded curves, so they behave at any level:
    //  Dist 2: even-order bend (2nd harmonic). Only the bend is DC-blocked, so the
    //          fundamental passes with zero phase shift (keeps the parallel blend exact).
    //  Dist 3: symmetric soft saturation (3rd harmonic), like a tape/transformer edge
    static inline float colourise (float x, int mode, Biquad& dc) noexcept
    {
        if (mode == 1)
        {
            const float xx = x * x;
            return x + (float) dc.process ((double) (0.35f * xx / (1.0f + xx)));
        }
        if (mode == 2)
            return x / std::sqrt (1.0f + 1.6f * x * x);   // unity small-signal gain
        return x;
    }

    double sr = 48000.0;
    int nch = 2, factor = 4;
    std::array<Biquad, kMaxCh> scHpf, dcBlock;
    std::vector<float> gComp, gOut, gMix;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smIn { 1.0f }, smOut { 1.0f };
    juce::SmoothedValue<float> smMix;
    float grDb = 0.0f, env = 0.0f, lastComp = 1.0f, lastOut = 1.0f, lastMix = 0.0f;
    double powIn = 0.0, powOut = 0.0;
    float makeupNow = 0.0f;
    std::atomic<float> meterMakeup { 0.0f };
    bool active = false;
    std::atomic<float> meterGr { 0.0f };
};

//==============================================================================
// One path (Dry or Wet): EQ (+ the other path's EQ all-passes) -> Comp (with Mix) -> fader,
// plus a post-fader peak meter.
class ChannelStrip
{
public:
    struct Settings
    {
        float levelDb = 0.0f;   // fader; at or below kOffDb the path is switched off
        EqSettings eq;
        CompSettings comp;
        bool mute = false, solo = false;
        bool compFirst = false; // order: false = EQ -> Comp, true = Comp -> EQ
        bool audible = true;    // mute/solo resolved across both paths (set by GlowDSP)
    };
    static constexpr float kOffDb = -60.0f;

    void prepare (double sampleRate, int numChannels, int maxBlock)
    {
        // The comp runs 4x oversampled (2x at 88.2k and up). The oversampler runs on every block
        // (even with the comp off) so the strip's latency never changes and Dry/Wet stay aligned.
        const size_t stages = sampleRate < 60000.0 ? 2 : 1;   // ~192 kHz for the comp
        os = std::make_unique<juce::dsp::Oversampling<float>> ((size_t) juce::jmax (1, numChannels), stages,
                 juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, GLOW_STRIP_OS_MAXQ, true);
        os->initProcessing ((size_t) maxBlock);
        eq.prepare (sampleRate, maxBlock);
        comp.prepare (sampleRate, numChannels, (int) os->getOversamplingFactor(), maxBlock);
        smLevel.reset (sampleRate, 0.05);
        smAudible.reset (sampleRate, 0.01);   // mute/solo: 10 ms, click-free
        swapStep = (float) (1.0 / (0.015 * sampleRate));   // order switch: 15 ms down, 15 ms up
        swapGain = 1.0f;
        first = true;
        stale = true;
    }

    void reset() { eq.reset(); comp.reset(); if (os) os->reset(); eq.snap(); }

    int getLatencySamples() const { return os ? (int) std::round (os->getLatencyInSamples()) : 0; }

    // Is this path audible (fader up, or still fading)? Its EQ only engages while it is.
    bool isActive (const Settings& s) const { return s.levelDb > kOffDb || ! isMutedSteady(); }

    // Step 1, once per block for BOTH strips before either is processed: the EQ plans.
    void planEq (const Settings& s, bool allowed, int n, bool coherent) { eq.plan (s.eq, allowed, n, coherent); }

    // Step 2: the audio. 'other' is the opposite path's strip (for its EQ all-passes).
    void process (juce::dsp::AudioBlock<float>& block, const Settings& s, ChannelStrip& other)
    {
        const float target = s.levelDb <= kOffDb ? 0.0f : juce::Decibels::decibelsToGain (s.levelDb);
        smLevel.setTargetValue (target);
        if (first) { smLevel.setCurrentAndTargetValue (target); first = false; }

        // skip the processing entirely while the path is fully muted
        if (! smLevel.isSmoothing() && target <= 0.0f)   // fully muted
        {
            block.clear();
            stale = true;
            return;
        }
        if (stale)   // coming back on: start from a clean state
        {
            eq.resetMain(); other.eq.resetAllpass(); comp.reset(); if (os) os->reset();
            compFirst = s.compFirst; swapGain = 1.0f;
            stale = false;
        }

        // order switch: fade the path out (15 ms, raised cosine), swap at silence, fade back in
        if (s.compFirst != compFirst && swapGain <= 0.0f) compFirst = s.compFirst;
        const bool swapping = s.compFirst != compFirst || swapGain < 1.0f;

        other.eq.allpassOnOtherPath (block);   // phase-match the other path's EQ
        if (! compFirst) eq.process (block);
        const bool compOn = comp.analyse (block, s.comp);
        auto up = os->processSamplesUp (block);
        if (compOn) comp.process (up, s.comp);
        os->processSamplesDown (block);
        if (compFirst) eq.process (block);

        const int n = (int) block.getNumSamples();
        smAudible.setTargetValue (s.audible ? 1.0f : 0.0f);
        if (firstAudible) { smAudible.setCurrentAndTargetValue (smAudible.getTargetValue()); firstAudible = false; }
        const float swapDir = s.compFirst != compFirst ? -swapStep : swapStep;
        float peak = 0.0f;
        for (size_t c = 0; c < block.getNumChannels(); ++c)
        {
            auto* x = block.getChannelPointer (c);
            auto g = smLevel;
            auto a = smAudible;
            float sw = swapGain;
            if (swapping)
                for (int i = 0; i < n; ++i)
                {
                    sw = juce::jlimit (0.0f, 1.0f, sw + swapDir);
                    const float shaped = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * sw);
                    x[i] *= g.getNextValue() * a.getNextValue() * shaped;
                    peak = juce::jmax (peak, std::abs (x[i]));
                }
            else
                for (int i = 0; i < n; ++i)
                {
                    x[i] *= g.getNextValue() * a.getNextValue();
                    peak = juce::jmax (peak, std::abs (x[i]));
                }
        }
        smLevel.skip (n);
        smAudible.skip (n);
        if (swapping) swapGain = juce::jlimit (0.0f, 1.0f, swapGain + swapDir * (float) n);
        // hold the highest peak until the UI reads it, so no block's peak is missed
        float prev = meterPeak.load();
        while (peak > prev && ! meterPeak.compare_exchange_weak (prev, peak)) {}
    }

    // true when the fader is at Off and not moving (the path outputs silence)
    bool isMutedSteady() const { return ! first && ! smLevel.isSmoothing() && smLevel.getTargetValue() <= 0.0f; }

    // UI thread: returns the peak since the last call and clears it
    float takePeak() { return meterPeak.exchange (0.0f); }
    float getGainReductionDb() const { return comp.getGainReductionDb(); }
    float getMakeupDb() const { return comp.getMakeupDb(); }

private:
    FourBandEQ eq;
    Compressor comp;
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    bool stale = true;
    juce::SmoothedValue<float> smLevel, smAudible;
    bool first = true, firstAudible = true, compFirst = false;
    float swapGain = 1.0f, swapStep = 0.003f;
    std::atomic<float> meterPeak { 0.0f };
};
