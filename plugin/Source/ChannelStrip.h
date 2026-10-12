#pragma once
#include <juce_dsp/juce_dsp.h>
#include "Biquad.h"

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

class FourBandEQ
{
public:
    void prepare (double sampleRate, int numChannels)
    {
        sr = sampleRate;
        nch = juce::jlimit (1, kMaxCh, numChannels);
        for (auto& b : bands) for (auto& f : b) { f.setIdentity(); f.reset(); }
        last = {};
        last.freq[0] = -1.0f;   // force a coefficient update on the first block
        wasOn = false;
    }

    void reset() { for (auto& b : bands) for (auto& f : b) f.reset(); }

    // Settings glide to their targets (~30 ms) so dragging bands never zippers; coefficients
    // are recomputed per block only while something is actually moving.
    void process (juce::dsp::AudioBlock<float>& block, const EqSettings& target)
    {
        if (! target.on) { wasOn = false; return; }
        if (! wasOn) { reset(); wasOn = true; cur = target; last.freq[0] = -1.0f; }   // start clean, no glide
        const float a = std::exp (-(float) block.getNumSamples() / (0.03f * (float) sr));
        for (int b = 0; b < 4; ++b)
        {
            cur.freq[b] = std::exp (a * std::log (cur.freq[b]) + (1.0f - a) * std::log (target.freq[b]));   // glide in octaves
            cur.gain[b] = a * cur.gain[b] + (1.0f - a) * target.gain[b];
            cur.q[b] = a * cur.q[b] + (1.0f - a) * target.q[b];
        }
        cur.on = target.on; cur.lowShelf = target.lowShelf; cur.highShelf = target.highShelf;
        const EqSettings& s = cur;
        if (changed (s)) { updateCoefficients (s); last = s; }

        const int n = (int) block.getNumSamples();
        const int ch = juce::jmin ((int) block.getNumChannels(), nch);
        for (int c = 0; c < ch; ++c)
        {
            auto* x = block.getChannelPointer ((size_t) c);
            auto& f = bands[(size_t) c];
            for (int i = 0; i < n; ++i)
            {
                double v = x[i];
                v = f[0].process (v); v = f[1].process (v); v = f[2].process (v); v = f[3].process (v);
                x[i] = (float) v;
            }
        }
    }

    // Magnitude response in dB at frequency f (for drawing the curve in the UI).
    static float responseDb (const EqSettings& s, double f, double sampleRate = 48000.0)
    {
        double total = 0.0;
        for (int b = 0; b < 4; ++b)
        {
            Biquad q;
            design (q, s, b, sampleRate);
            total += magDb (q, f, sampleRate);
        }
        return (float) total;
    }

private:
    static constexpr int kMaxCh = 2;

    static void design (Biquad& f, const EqSettings& s, int b, double sr)
    {
        const double fc = juce::jlimit (10.0, 0.45 * sr, (double) s.freq[b]);
        const double g = s.gain[b], q = juce::jmax (0.1f, s.q[b]);
        if (std::abs (g) < 1.0e-3) { f.setIdentity(); return; }
        if (b == 0 && s.lowShelf)       f.setLowShelf (fc, g, q, sr);
        else if (b == 3 && s.highShelf) f.setHighShelf (fc, g, q, sr);
        else                            f.setPeak (fc, g, q, sr);
    }

    static double magDb (const Biquad& f, double freq, double sr)
    {
        const std::complex<double> z = std::polar (1.0, -2.0 * juce::MathConstants<double>::pi * freq / sr);
        const auto num = f.b0 + f.b1 * z + f.b2 * z * z;
        const auto den = 1.0 + f.a1 * z + f.a2 * z * z;
        return 20.0 * std::log10 (std::abs (num / den) + 1.0e-12);
    }

    bool changed (const EqSettings& s) const
    {
        for (int b = 0; b < 4; ++b)
            if (std::abs (s.freq[b] - last.freq[b]) > 1.0e-4f || std::abs (s.gain[b] - last.gain[b]) > 1.0e-5f
                || std::abs (s.q[b] - last.q[b]) > 1.0e-5f) return true;
        return s.lowShelf != last.lowShelf || s.highShelf != last.highShelf;
    }

    void updateCoefficients (const EqSettings& s)
    {
        for (int b = 0; b < 4; ++b)
        {
            Biquad proto;
            design (proto, s, b, sr);
            for (int c = 0; c < nch; ++c)
            {
                auto& f = bands[(size_t) c][(size_t) b];
                f.b0 = proto.b0; f.b1 = proto.b1; f.b2 = proto.b2; f.a1 = proto.a1; f.a2 = proto.a2;  // keep state: no clicks
            }
        }
    }

    double sr = 48000.0;
    int nch = 2;
    std::array<std::array<Biquad, 4>, kMaxCh> bands;
    EqSettings last, cur;
    bool wasOn = false;
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
};

class Compressor
{
public:
    static constexpr int kNumRatios = 8;
    static inline const char* ratioNames[kNumRatios] { "1:1", "2:1", "3:1", "4:1", "6:1", "10:1", "20:1", "NUKE" };
    static inline const char* colourNames[3] { "CLEAN", "DIST 2", "DIST 3" };

    // osRate: rate the distortion colour runs at (the strip oversamples it)
    void prepare (double sampleRate, int numChannels, double osRate)
    {
        sr = sampleRate;
        nch = juce::jlimit (1, kMaxCh, numChannels);
        for (auto& f : scHpf) { f.setHighPass (100.0, 0.70710678, sr); f.reset(); }
        for (auto& f : dcBlock) { f.setOnePoleHighPass (5.0, osRate); f.reset(); }
        grDb = 0.0f;
        smIn.reset (sr, 0.03); smOut.reset (sr, 0.03);
        first = true;
    }

    void reset()
    {
        for (auto& f : scHpf) f.reset();
        for (auto& f : dcBlock) f.reset();
        grDb = 0.0f;
        env = 0.0f;
    }

    // Step 1 (base rate): input gain + gain reduction. The distortion colour (step 2) runs
    // oversampled in the strip, then step 3 applies the Output gain.
    void processGain (juce::dsp::AudioBlock<float>& block, const CompSettings& s)
    {
        if (! s.on) { meterGr.store (0.0f); wasOn = false; return; }
        if (! wasOn) { reset(); wasOn = true; first = true; }

        const int n = (int) block.getNumSamples();
        const int ch = juce::jmin ((int) block.getNumChannels(), nch);

        smIn.setTargetValue (juce::Decibels::decibelsToGain (s.inputDb));
        smOut.setTargetValue (juce::Decibels::decibelsToGain (s.outputDb));
        if (first) { smIn.setCurrentAndTargetValue (smIn.getTargetValue()); smOut.setCurrentAndTargetValue (smOut.getTargetValue()); first = false; }

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
            const float g = juce::Decibels::decibelsToGain (grDb) * gIn;

            for (int c = 0; c < ch; ++c)
                block.setSample (c, i, block.getSample (c, i) * g);
        }
        meterGr.store (minGain);
    }

    // Step 2 (oversampled): distortion colour
    void processColour (juce::dsp::AudioBlock<float>& up, const CompSettings& s)
    {
        if (! s.on || s.colour == 0) return;
        const int ch = juce::jmin ((int) up.getNumChannels(), nch);
        for (int c = 0; c < ch; ++c)
        {
            auto* x = up.getChannelPointer ((size_t) c);
            for (size_t i = 0; i < up.getNumSamples(); ++i)
                x[i] = colourise (x[i], s.colour, dcBlock[(size_t) c]);
        }
    }

    // Step 3 (base rate): Output gain
    void processOutput (juce::dsp::AudioBlock<float>& block, const CompSettings& s)
    {
        if (! s.on) return;
        const int n = (int) block.getNumSamples();
        const int ch = juce::jmin ((int) block.getNumChannels(), nch);
        for (int c = 0; c < ch; ++c)
        {
            auto* x = block.getChannelPointer ((size_t) c);
            auto g = smOut;
            for (int i = 0; i < n; ++i) x[i] *= g.getNextValue();
        }
        smOut.skip (n);
    }

    float getGainReductionDb() const { return meterGr.load(); }

private:
    static constexpr int kMaxCh = 2;
    static constexpr float kThresholdDb = -18.0f;    // fixed: Input sets how hard you hit it
    static constexpr float kRatio[kNumRatios] { 1.0f, 2.0f, 3.0f, 4.0f, 6.0f, 10.0f, 20.0f, 1000.0f };
    static constexpr float kKnee[kNumRatios]  { 0.0f, 12.0f, 10.0f, 8.0f, 6.0f, 4.0f, 2.0f, 0.0f };   // dB

    // Distortion colours. Bounded curves, so they behave at any level:
    //  Dist 2: even-order bend (2nd harmonic), DC from the bend removed
    //  Dist 3: symmetric soft saturation (3rd harmonic), like a tape/transformer edge
    static inline float colourise (float x, int mode, Biquad& dc) noexcept
    {
        if (mode == 1)
        {
            const float xx = x * x;
            const float y = x + 0.35f * xx / (1.0f + xx);
            return (float) dc.process ((double) y);
        }
        if (mode == 2)
            return x / std::sqrt (1.0f + 1.6f * x * x);   // unity small-signal gain
        return x;
    }

    double sr = 48000.0;
    int nch = 2;
    std::array<Biquad, kMaxCh> scHpf, dcBlock;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smIn { 1.0f }, smOut { 1.0f };
    float grDb = 0.0f, env = 0.0f;
    bool first = true, wasOn = false;
    std::atomic<float> meterGr { 0.0f };
};

//==============================================================================
// One path (Dry or Wet): EQ -> Comp -> fader, plus a post-fader peak meter.
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
        // The comp's distortion colour runs 4x oversampled. The oversampler runs on every block
        // (even with the comp off) so the strip's latency never changes and Dry/Wet stay aligned.
        const size_t stages = sampleRate < 60000.0 ? 2 : 1;   // ~192 kHz for the colour stage
        os = std::make_unique<juce::dsp::Oversampling<float>> ((size_t) juce::jmax (1, numChannels), stages,
                 juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, GLOW_STRIP_OS_MAXQ, true);
        os->initProcessing ((size_t) maxBlock);
        eq.prepare (sampleRate, numChannels);
        comp.prepare (sampleRate, numChannels, sampleRate * (double) os->getOversamplingFactor());
        smLevel.reset (sampleRate, 0.05);
        first = true;
        stale = true;
    }

    void reset() { eq.reset(); comp.reset(); if (os) os->reset(); }

    int getLatencySamples() const { return os ? (int) std::round (os->getLatencyInSamples()) : 0; }

    void process (juce::dsp::AudioBlock<float>& block, const Settings& s)
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
        if (stale) { reset(); stale = false; }   // coming back on: start from a clean state

        eq.process (block, s.eq);
        comp.processGain (block, s.comp);
        auto up = os->processSamplesUp (block);
        comp.processColour (up, s.comp);
        os->processSamplesDown (block);
        comp.processOutput (block, s.comp);

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
