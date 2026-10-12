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

    void reset() { for (auto& m : main) m.reset(); for (auto& a : comp) a.reset(); }
    void snap() { for (auto& st : stages) st.snapNext(); }

    // Once per block, before either path is processed. 'allowed' = this path is audible.
    void plan (const EqSettings& s, bool allowed, int n)
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
            st.plan (s.on && allowed, s.freq[b], s.gain[b], s.q[b], n);
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
    void resetMain() { for (auto& m : main) m.reset(); }
    void resetAllpass() { for (auto& a : comp) a.reset(); }

    // Magnitude response in dB at frequency f (for drawing the curve in the UI).
    static float responseDb (const EqSettings& s, double f, double sampleRate = 48000.0)
    {
        double total = 0.0;
        for (int b = 0; b < 4; ++b)
            if (std::abs (s.gain[b]) >= 0.01f)
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
    std::array<Coherent::Allpass, 4> comp;
};

//==============================================================================
// "Distressor-style" compressor: no threshold control - Input drives the signal into a
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
            smOut.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (s.outputDb));
            reset();
        }

        const int n = (int) block.getNumSamples();
        const int ch = juce::jmin ((int) block.getNumChannels(), nch);
        smIn.setTargetValue (juce::Decibels::decibelsToGain (s.inputDb));
        smOut.setTargetValue (juce::Decibels::decibelsToGain (s.outputDb));

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

            // static curve (soft knee), in dB
            const float xDb = juce::Decibels::gainToDecibels (env, -120.0f);
            const float over = xDb - kThresholdDb;
            float target = 0.0f;   // gain change in dB (<= 0)
            if (slope > 0.0f)
            {
                if (2.0f * over > knee)                         target = -slope * over;
                else if (knee > 0.0f && 2.0f * over > -knee)    target = -slope * (over + 0.5f * knee) * (over + 0.5f * knee) / (2.0f * knee);
            }

            // attack when more reduction is needed, release when less
            grDb = target < grDb ? aAtk * grDb + (1.0f - aAtk) * target
                                 : aRel * grDb + (1.0f - aRel) * target;
            minGain = juce::jmin (minGain, grDb);
            gComp[(size_t) i] = juce::Decibels::decibelsToGain (grDb) * gIn;
            gOut[(size_t) i] = smOut.getNextValue();
            gMix[(size_t) i] = smMix.getNextValue();
        }
        meterGr.store (minGain);
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

private:
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
        float levelDb = 0.0f;   // fader; at or below kOffDb the path is muted
        EqSettings eq;
        CompSettings comp;
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
        first = true;
        stale = true;
    }

    void reset() { eq.reset(); comp.reset(); if (os) os->reset(); eq.snap(); }

    int getLatencySamples() const { return os ? (int) std::round (os->getLatencyInSamples()) : 0; }

    // Is this path audible (fader up, or still fading)? Its EQ only engages while it is.
    bool isActive (const Settings& s) const { return s.levelDb > kOffDb || ! isMutedSteady(); }

    // Step 1, once per block for BOTH strips before either is processed: the EQ plans.
    void planEq (const Settings& s, bool allowed, int n) { eq.plan (s.eq, allowed, n); }

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
            stale = false;
        }

        eq.process (block);
        other.eq.allpassOnOtherPath (block);   // phase-match the other path's EQ
        const bool compOn = comp.analyse (block, s.comp);
        auto up = os->processSamplesUp (block);
        if (compOn) comp.process (up, s.comp);
        os->processSamplesDown (block);

        const int n = (int) block.getNumSamples();
        float peak = 0.0f;
        for (size_t c = 0; c < block.getNumChannels(); ++c)
        {
            auto* x = block.getChannelPointer (c);
            auto g = smLevel;
            for (int i = 0; i < n; ++i) { x[i] *= g.getNextValue(); peak = juce::jmax (peak, std::abs (x[i])); }
        }
        smLevel.skip (n);
        // hold the highest peak until the UI reads it, so no block's peak is missed
        float prev = meterPeak.load();
        while (peak > prev && ! meterPeak.compare_exchange_weak (prev, peak)) {}
    }

    // true when the fader is at Off and not moving (the path outputs silence)
    bool isMutedSteady() const { return ! first && ! smLevel.isSmoothing() && smLevel.getTargetValue() <= 0.0f; }

    // UI thread: returns the peak since the last call and clears it
    float takePeak() { return meterPeak.exchange (0.0f); }
    float getGainReductionDb() const { return comp.getGainReductionDb(); }

private:
    FourBandEQ eq;
    Compressor comp;
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    bool stale = true;
    juce::SmoothedValue<float> smLevel;
    bool first = true;
    std::atomic<float> meterPeak { 0.0f };
};
