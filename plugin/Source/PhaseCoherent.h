#pragma once
#include <juce_dsp/juce_dsp.h>
#include "Biquad.h"

// Phase-coherent EQ: every user filter is built so its phase is EXACTLY that of a plain
// all-pass. The other path runs that all-pass (flat magnitude), so Dry and Wet always
// arrive with identical phase at the sum, whatever either path's EQ is doing.
//
// How: take a normal RBJ bell/shelf/high-pass P = B(z)/A(z), designed at HALF the gain.
// Run it twice: once as is, once with its numerator coefficients reversed (b2,b1,b0).
// The reversed copy has the same magnitude but mirrored phase, so the pair gives
//     |P|^2  (the full gain, a normal-looking bell/shelf)  x  AP  (all-pass from A(z)).
// The other path gets that AP: (a2,a1,1)/(1,a1,a2). Same phase, to rounding error.
// For the high-pass the pair is a Butterworth squared = Linkwitz-Riley 24 dB/oct.
//
// Analog equivalent: it's how a Linkwitz-Riley crossover's two halves stay in phase -
// the "missing" phase gets put on the other path instead of being left as a mismatch.
//
// Switching: bands fade in/out by sweeping their Q to 10000x (a bell that narrow is
// effectively gone), and the high-pass crossfades. Both paths follow the same per-32-
// sample plan, so they stay locked even while things move.
namespace Coherent
{
    enum class Kind { peak, lowShelf, highShelf, highPass };

    struct Coeffs { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };

    // The half-gain prototype P(z).
    inline Coeffs designHalf (Kind k, double fc, double gainDb, double q, double sr)
    {
        Biquad f;
        fc = juce::jlimit (5.0, 0.45 * sr, fc);
        switch (k)
        {
            case Kind::peak:      f.setPeak (fc, 0.5 * gainDb, q, sr); break;
            case Kind::lowShelf:  f.setLowShelf (fc, 0.5 * gainDb, q, sr); break;
            case Kind::highShelf: f.setHighShelf (fc, 0.5 * gainDb, q, sr); break;
            // LR4: -6 dB at its corner; shifted up so the knob frequency is the -3 dB point
            case Kind::highPass:  f.setHighPass (juce::jmin (0.45 * sr, fc / 1.24650), 0.70710678, sr); break;
        }
        return { f.b0, f.b1, f.b2, f.a1, f.a2 };
    }

    // Magnitude of the full filter in dB (= 2x the prototype), for drawing curves.
    inline double magDb (const Coeffs& c, double freq, double sr)
    {
        const std::complex<double> z = std::polar (1.0, -2.0 * juce::MathConstants<double>::pi * freq / sr);
        const auto num = c.b0 + c.b1 * z + c.b2 * z * z;
        const auto den = 1.0 + c.a1 * z + c.a2 * z * z;
        return 40.0 * std::log10 (std::abs (num / den) + 1.0e-12);
    }

    inline void load (Biquad& f, double b0, double b1, double b2, double a1, double a2) noexcept
    {
        f.b0 = b0; f.b1 = b1; f.b2 = b2; f.a1 = a1; f.a2 = a2;
    }

    //==========================================================================
    // One controllable filter: owns the glides and the on/off logic, and produces a plan
    // (coefficients per 32-sample segment) that both paths read.
    class Stage
    {
    public:
        static constexpr int kSeg = 32;

        struct Seg
        {
            Coeffs c;
            bool active = false, fresh = false;   // fresh: filter starts here, clear its state
            float m0 = 1.0f, m1 = 1.0f;   // crossfade amount: filter in (1) or out (0), ramps when switching
        };

        void prepare (double sampleRate, int maxBlock, Kind k)
        {
            sr = sampleRate;
            kind = k;
            segs.assign ((size_t) (maxBlock / kSeg + 2), Seg {});
            numSegs = 0;
            const double segSec = kSeg / sr;
            aGlide = std::exp (-segSec / 0.03);          // ~30 ms glides
            dE = (float) (kMaxE * segSec / 0.03);         // Q sweep over 30 ms
            dEn = (float) (segSec / 0.03);                // enable ramp 30 ms
            dM = (float) (segSec / 0.02);                 // high-pass crossfade 20 ms
            snapNext();
        }

        // Next plan() jumps straight to the target (session load, transport reset).
        void snapNext() { snap = true; }

        void setKind (Kind k) { kind = k; }
        Kind getKind() const { return kind; }

        // Build this block's plan. For the high-pass, 'gainDb' is ignored and 'enabled'
        // means the knob is above Off.
        void plan (bool enabled, float freq, float gainDb, float q, int n)
        {
            numSegs = (n + kSeg - 1) / kSeg;
            const double tf = juce::jlimit (5.0, 0.45 * sr, (double) freq);
            const double tq = juce::jmax (0.1, (double) q);
            const double tg = kind == Kind::highPass ? 0.0 : (double) gainDb;
            const bool need = kind == Kind::highPass ? enabled : (enabled && std::abs (tg) >= 0.01);

            bool snapped = false;
            if (snap)
            {
                snap = false; snapped = true;
                curF = tf; curQ = tq; curG = tg;
                active = need; e = 0.0f; en = need ? 1.0f : 0.0f; m = need ? 1.0f : 0.0f;
                dirty = true;
            }

            for (int s = 0; s < numSegs; ++s)
            {
                auto& seg = segs[(size_t) s];
                seg.fresh = snapped && s == 0;   // jumped to a new setting: start from clean state
                const double pf = curF, pq = curQ, pg = curG;
                const float pe = e, pen = en;
                // HPF switching off: glide down to the bottom first, crossfade out down there
                const double tfe = (kind == Kind::highPass && ! need) ? kHpOffHz : tf;
                curF = std::exp (aGlide * std::log (curF) + (1.0 - aGlide) * std::log (tfe));
                curQ = aGlide * curQ + (1.0 - aGlide) * tq;
                curG = aGlide * curG + (1.0 - aGlide) * tg;
                if (std::abs (curG - tg) < 1.0e-4) curG = tg;
                if (std::abs (curF - tfe) < 1.0e-6 * tfe) curF = tfe;
                if (std::abs (curQ - tq) < 1.0e-6) curQ = tq;

                if (kind == Kind::highPass)
                {
                    // crossfades in/out only at the very bottom of its range (where it's nearly
                    // transparent): switching on, it starts at 10 Hz and glides up to the knob
                    seg.m0 = m;
                    if (need)
                    {
                        if (! active) { active = true; seg.fresh = true; m = 0.0f; seg.m0 = 0.0f; curF = kHpOffHz; }
                        m = juce::jmin (1.0f, m + dM);
                    }
                    else if (active && curF < 1.05 * kHpOffHz) m = juce::jmax (0.0f, m - dM);
                    seg.m1 = m;
                    seg.active = active;
                    if (active && ! need && m <= 0.0f) active = false;   // from the next segment on
                }
                else
                {
                    // fade in: Q sweeps from 10^4 x down to the setting with the band at 0 dB (a pure
                    // all-pass, identical on both paths), THEN the gain fades in. Fade out: the reverse.
                    // (Sequenced because a shelf at huge Q with any gain is a big resonance.)
                    seg.m0 = m;
                    if (need)
                    {
                        if (! active) { active = true; seg.fresh = true; e = kMaxE; en = 0.0f; m = 1.0f; seg.m0 = 1.0f; curF = tf; curQ = tq; }
                        if (m < 1.0f)    m = juce::jmin (1.0f, m + dM);    // was finishing a fade-out
                        else if (e > 0.0f) e = juce::jmax (0.0f, e - dE);
                        else             en = juce::jmin (1.0f, en + dEn);
                    }
                    else if (active)
                    {
                        en = juce::jmax (0.0f, en - dEn);
                        if (std::abs (curG * en) < 0.01)
                        {
                            en = 0.0f;
                            if (e < kMaxE) e = juce::jmin (kMaxE, e + dE);
                            // last step: crossfade out. Even at 10^4 x Q an all-pass still flips
                            // the phase of a tone sitting exactly on its centre, so no hard switch.
                            else m = juce::jmax (0.0f, m - dM);
                        }
                    }
                    seg.m1 = m;
                    gEff = curG * en;
                    seg.active = active;
                    if (active && ! need && m <= 0.0f) active = false;   // from the next segment on
                }

                if (seg.active)
                {
                    if (dirty || seg.fresh || ne (pf, curF) || ne (pq, curQ) || ne (pg, curG) || ne ((double) pe, (double) e) || ne ((double) pen, (double) en))
                    {
                        coeffs = designHalf (kind, curF, gEff, curQ * std::pow (10.0, (double) e), sr);
                        dirty = false;
                    }
                    seg.c = coeffs;
                }
                else dirty = true;
            }
        }

        // True if any segment of this block does something (else both paths skip it).
        bool busy() const
        {
            for (int s = 0; s < numSegs; ++s) if (segs[(size_t) s].active) return true;
            return false;
        }

        // Full-response helper for the UI: magnitude in dB of the settled filter.
        static double responseDb (Kind k, double fc, double gainDb, double q, double freq, double sr)
        {
            return magDb (designHalf (k, fc, gainDb, q, sr), freq, sr);
        }

        int segments() const { return numSegs; }
        const Seg& seg (int s) const { return segs[(size_t) s]; }

    private:
        static constexpr float kMaxE = 4.0f;
        static constexpr double kHpOffHz = 10.0;
        static bool ne (double a, double b) { return a < b || a > b; }   // Q x 10^4 at the ends of the sweep

        double sr = 48000.0;
        Kind kind = Kind::peak;
        std::vector<Seg> segs;
        int numSegs = 0;
        double aGlide = 0.98;
        float dE = 0.1f, dEn = 0.03f, dM = 0.03f;
        double curF = 1000.0, curQ = 0.7, curG = 0.0, gEff = 0.0;
        float e = 0.0f, en = 0.0f, m = 0.0f;
        bool active = false, snap = true, dirty = true;
        Coeffs coeffs;
    };

    //==========================================================================
    // Runs a Stage's filter on its own path: the prototype then its reversed-numerator twin.
    class Main
    {
    public:
        void reset() { for (auto& c : st) { c[0].reset(); c[1].reset(); } }

        void process (const Stage& stage, juce::dsp::AudioBlock<float>& block)
        {
            const int n = (int) block.getNumSamples();
            const int nch = juce::jmin ((int) block.getNumChannels(), kMaxCh);
            for (int c = 0; c < nch; ++c)
            {
                auto* x = block.getChannelPointer ((size_t) c);
                auto& f = st[(size_t) c];
                for (int s = 0; s < stage.segments(); ++s)
                {
                    const auto& g = stage.seg (s);
                    const int i0 = s * Stage::kSeg, i1 = juce::jmin (n, i0 + Stage::kSeg);
                    if (! g.active || g.fresh) { f[0].reset(); f[1].reset(); }
                    if (! g.active) continue;
                    load (f[0], g.c.b0, g.c.b1, g.c.b2, g.c.a1, g.c.a2);
                    load (f[1], g.c.b2, g.c.b1, g.c.b0, g.c.a1, g.c.a2);
                    if (g.m0 >= 1.0f && g.m1 >= 1.0f)
                    {
                        for (int i = i0; i < i1; ++i) x[i] = (float) f[1].process (f[0].process ((double) x[i]));
                    }
                    else
                    {
                        float mm = g.m0;
                        const float step = (g.m1 - g.m0) / (float) Stage::kSeg;
                        for (int i = i0; i < i1; ++i, mm += step)
                        {
                            const double v = x[i];
                            x[i] = (float) (v + mm * (f[1].process (f[0].process (v)) - v));
                        }
                    }
                }
            }
        }

    private:
        static constexpr int kMaxCh = 2;
        std::array<std::array<Biquad, 2>, kMaxCh> st;
    };

    // Runs a Stage's matching all-pass on the OTHER path.
    class Allpass
    {
    public:
        void reset() { for (auto& f : st) f.reset(); }

        void process (const Stage& stage, juce::dsp::AudioBlock<float>& block)
        {
            const int n = (int) block.getNumSamples();
            const int nch = juce::jmin ((int) block.getNumChannels(), kMaxCh);
            for (int c = 0; c < nch; ++c)
            {
                auto* x = block.getChannelPointer ((size_t) c);
                auto& f = st[(size_t) c];
                for (int s = 0; s < stage.segments(); ++s)
                {
                    const auto& g = stage.seg (s);
                    const int i0 = s * Stage::kSeg, i1 = juce::jmin (n, i0 + Stage::kSeg);
                    if (! g.active || g.fresh) f.reset();
                    if (! g.active) continue;
                    load (f, g.c.a2, g.c.a1, 1.0, g.c.a1, g.c.a2);
                    if (g.m0 >= 1.0f && g.m1 >= 1.0f)
                    {
                        for (int i = i0; i < i1; ++i) x[i] = (float) f.process ((double) x[i]);
                    }
                    else
                    {
                        float mm = g.m0;
                        const float step = (g.m1 - g.m0) / (float) Stage::kSeg;
                        for (int i = i0; i < i1; ++i, mm += step)
                        {
                            const double v = x[i];
                            x[i] = (float) (v + mm * (f.process (v) - v));
                        }
                    }
                }
            }
        }

    private:
        static constexpr int kMaxCh = 2;
        std::array<Biquad, kMaxCh> st;
    };
}
