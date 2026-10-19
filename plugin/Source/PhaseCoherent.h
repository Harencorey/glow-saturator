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
// The 12 dB/oct high-pass uses a 1st-order high-pass times a matching 1st-order all-pass as
// the prototype: its pair is the 1st-order magnitude squared (12 dB/oct) with a 2nd-order
// all-pass phase, so it crossfades in and out the same way the 24 dB one does.
// The 36 dB/oct high-pass is two stages in series, a 2nd-order Butterworth at Q = 1 and the
// 1st-order prototype above, at one corner: together a 3rd-order Butterworth, so the pair is
// its square (a Linkwitz-Riley 36 dB/oct), phase-locked the same way.
// The 6 dB/oct high-pass is a plain 1st-order filter. Its square would be 12 dB/oct, so it
// can't use the trick; it runs as an ordinary filter (no all-pass twin on the other path).
// A 1st-order high-pass leads by at most 90 degrees, so a blend never cancels; and Keep lows
// is still exact, because x - HP1(x) is the matching 1st-order low-pass.
//
// Analog equivalent: it's how a Linkwitz-Riley crossover's two halves stay in phase -
// the "missing" phase gets put on the other path instead of being left as a mismatch.
//
// Switching: bands fade in/out by sweeping their Q to 10000x (a bell that narrow is
// effectively gone), and the high-pass crossfades. Both paths follow the same per-32-
// sample plan, so they stay locked even while things move.
namespace Coherent
{
    enum class Kind { peak, lowShelf, highShelf, highPass, highPass12, highPass6, highPass36a, highPass36b };
    inline bool isHighPass (Kind k)
    {
        return k == Kind::highPass || k == Kind::highPass12 || k == Kind::highPass6 || k == Kind::highPass36a || k == Kind::highPass36b;
    }
    // corner shifts: the knob frequency is the -3 dB point of the finished (squared) response
    static constexpr double kLr4Shift = 1.24650, kLr2Shift = 1.55377, kLr6Shift = 1.15829;

    // 1st-order high-pass at fc (bilinear), as a biquad with b2 = a2 = 0
    inline void firstOrderHp (double fc, double sr, double& h0, double& a)
    {
        const double kk = std::tan (juce::MathConstants<double>::pi * juce::jmin (0.45 * sr, fc) / sr);
        h0 = 1.0 / (1.0 + kk);
        a = (kk - 1.0) / (kk + 1.0);
    }
    // 1st-order HP x the 1st-order all-pass with the same pole
    inline void hpTimesAp (double fc, double sr, double* c)
    {
        double h0 = 0, a = 0;
        firstOrderHp (fc, sr, h0, a);
        const double h1 = -h0;
        c[0] = h0 * a; c[1] = h0 + h1 * a; c[2] = h1; c[3] = 2.0 * a; c[4] = a * a;
    }

    struct Coeffs { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };

    inline Coeffs designFull (Kind k, double fc, double gainDb, double q, double sr);

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
            case Kind::highPass12:
            case Kind::highPass36b:
            {
                // 1st-order HP (shifted so the knob is the -3 dB point of the squared result) x the
                // 1st-order all-pass with the same pole
                double c[5];
                hpTimesAp (fc / (k == Kind::highPass12 ? kLr2Shift : kLr6Shift), sr, c);
                return { c[0], c[1], c[2], c[3], c[4] };
            }
            case Kind::highPass36a: f.setHighPass (juce::jmin (0.45 * sr, fc / kLr6Shift), 1.0, sr); break;
            case Kind::highPass6:   return designFull (k, fc, gainDb, q, sr);   // not used phase-locked
        }
        return { f.b0, f.b1, f.b2, f.a1, f.a2 };
    }

    // The ordinary (minimum-phase) filter at the full gain: used whenever phase-locking to the
    // other path isn't needed (it's audibly tighter: e.g. ~1 ms vs ~5 ms of low-end delay for a
    // 100 Hz shelf). RBJ shelves/bells at +g and -g are exact inverses, phase included.
    inline Coeffs designFull (Kind k, double fc, double gainDb, double q, double sr)
    {
        Biquad f;
        fc = juce::jlimit (5.0, 0.45 * sr, fc);
        switch (k)
        {
            case Kind::peak:      f.setPeak (fc, gainDb, q, sr); break;
            case Kind::lowShelf:  f.setLowShelf (fc, gainDb, q, sr); break;
            case Kind::highShelf: f.setHighShelf (fc, gainDb, q, sr); break;
            case Kind::highPass6:
            {
                double h0 = 0, a = 0;
                firstOrderHp (fc, sr, h0, a);
                return { h0, -h0, 0.0, a, 0.0 };
            }
            case Kind::highPass:
            case Kind::highPass12:
            case Kind::highPass36a:
            case Kind::highPass36b: f.setIdentity(); break;   // these high-passes are always phase-locked
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
            Coeffs c, cMin;   // phase-locked prototype; ordinary full-gain filter
            float k0 = 1.0f, k1 = 1.0f;   // 1 = phase-locked, 0 = ordinary; ramps (50 ms) when it changes
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
            dK = (float) (segSec / 0.05);                 // phase-locked <-> ordinary: 50 ms
            snapNext();
        }

        // Next plan() jumps straight to the target (session load, transport reset).
        void snapNext() { snap = true; }

        void setKind (Kind k) { kind = k; }
        Kind getKind() const { return kind; }

        // Build this block's plan. For the high-pass, 'gainDb' is ignored and 'enabled'
        // means the knob is above Off.
        // coherent: the other path is audible, so phase-lock (else use the ordinary filter)
        void plan (bool enabled, float freq, float gainDb, float q, int n, bool coherent = true)
        {
            if (isHighPass (kind)) coherent = kind != Kind::highPass6;   // 6 dB/oct: always the ordinary filter
            numSegs = (n + kSeg - 1) / kSeg;
            const double tf = juce::jlimit (5.0, 0.45 * sr, (double) freq);
            const double tq = juce::jmax (0.1, (double) q);
            const double tg = isHighPass (kind) ? 0.0 : (double) gainDb;
            const bool need = isHighPass (kind) ? enabled : (enabled && std::abs (tg) >= 0.01);

            bool snapped = false;
            if (snap)
            {
                snap = false; snapped = true;
                curF = tf; curQ = tq; curG = tg;
                active = need; e = 0.0f; en = need ? 1.0f : 0.0f; m = need ? 1.0f : 0.0f;
                kc = coherent ? 1.0f : 0.0f;
                dirty = true;
            }

            for (int s = 0; s < numSegs; ++s)
            {
                auto& seg = segs[(size_t) s];
                seg.fresh = snapped && s == 0;   // jumped to a new setting: start from clean state
                seg.k0 = kc;
                kc = coherent ? juce::jmin (1.0f, kc + dK) : juce::jmax (0.0f, kc - dK);
                seg.k1 = kc;
                const double pf = curF, pq = curQ, pg = curG;
                const float pe = e, pen = en;
                // HPF switching off: glide down to the bottom first, crossfade out down there
                const double tfe = (isHighPass (kind) && ! need) ? kHpOffHz : tf;
                curF = std::exp (aGlide * std::log (curF) + (1.0 - aGlide) * std::log (tfe));
                curQ = aGlide * curQ + (1.0 - aGlide) * tq;
                curG = aGlide * curG + (1.0 - aGlide) * tg;
                if (std::abs (curG - tg) < 1.0e-4) curG = tg;
                if (std::abs (curF - tfe) < 1.0e-6 * tfe) curF = tfe;
                if (std::abs (curQ - tq) < 1.0e-6) curQ = tq;

                if (isHighPass (kind))
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
                        const double qEff = curQ * std::pow (10.0, (double) e);
                        coeffs = designHalf (kind, curF, gEff, qEff, sr);
                        coeffsMin = designFull (kind, curF, gEff, qEff, sr);
                        dirty = false;
                    }
                    seg.c = coeffs;
                    seg.cMin = coeffsMin;
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
            if (k == Kind::highPass6) return 0.5 * magDb (designFull (k, fc, gainDb, q, sr), freq, sr);   // magDb is 2x
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
        float e = 0.0f, en = 0.0f, m = 0.0f, kc = 1.0f, dK = 0.02f;   // kc: phase-locked amount
        bool active = false, snap = true, dirty = true;
        Coeffs coeffs, coeffsMin;
    };

    //==========================================================================
    // Runs a Stage's filter on its own path: the prototype then its reversed-numerator twin.
    class Main
    {
    public:
        void reset() { for (auto& c : st) for (auto& f : c) f.reset(); }

        void process (const Stage& stage, juce::dsp::AudioBlock<float>& block)
        {
            const int n = (int) block.getNumSamples();
            const int nch = juce::jmin ((int) block.getNumChannels(), kMaxCh);
            for (int c = 0; c < nch; ++c)
            {
                auto* x = block.getChannelPointer ((size_t) c);
                auto& f = st[(size_t) c];   // [0],[1]: phase-locked pair   [2]: ordinary filter
                for (int s = 0; s < stage.segments(); ++s)
                {
                    const auto& g = stage.seg (s);
                    const int i0 = s * Stage::kSeg, i1 = juce::jmin (n, i0 + Stage::kSeg);
                    if (! g.active || g.fresh) for (auto& b : f) b.reset();
                    if (! g.active) continue;
                    const bool useCoh = g.k0 > 0.0f || g.k1 > 0.0f;
                    const bool useMin = g.k0 < 1.0f || g.k1 < 1.0f;
                    if (useCoh)
                    {
                        load (f[0], g.c.b0, g.c.b1, g.c.b2, g.c.a1, g.c.a2);
                        load (f[1], g.c.b2, g.c.b1, g.c.b0, g.c.a1, g.c.a2);
                    }
                    else { f[0].reset(); f[1].reset(); }
                    if (useMin) load (f[2], g.cMin.b0, g.cMin.b1, g.cMin.b2, g.cMin.a1, g.cMin.a2);
                    else f[2].reset();

                    if (! useMin && g.m0 >= 1.0f && g.m1 >= 1.0f)
                    {
                        for (int i = i0; i < i1; ++i) x[i] = (float) f[1].process (f[0].process ((double) x[i]));
                        continue;
                    }
                    if (! useCoh && g.m0 >= 1.0f && g.m1 >= 1.0f)
                    {
                        for (int i = i0; i < i1; ++i) x[i] = (float) f[2].process ((double) x[i]);
                        continue;
                    }
                    float mm = g.m0, kk = g.k0;
                    const float mStep = (g.m1 - g.m0) / (float) Stage::kSeg, kStep = (g.k1 - g.k0) / (float) Stage::kSeg;
                    for (int i = i0; i < i1; ++i, mm += mStep, kk += kStep)
                    {
                        const double v = x[i];
                        const double yc = useCoh ? f[1].process (f[0].process (v)) : 0.0;
                        const double ym = useMin ? f[2].process (v) : 0.0;
                        const double y = useCoh && useMin ? ym + kk * (yc - ym) : (useCoh ? yc : ym);
                        x[i] = (float) (v + mm * (y - v));
                    }
                }
            }
        }

    private:
        static constexpr int kMaxCh = 2;
        std::array<std::array<Biquad, 3>, kMaxCh> st;
    };

    // Runs a Stage's matching all-pass on the OTHER path (only while the stage is phase-locked;
    // an ordinary filter has no all-pass twin, so then this is a straight wire).
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
                    if (! g.active || g.fresh || (g.k0 <= 0.0f && g.k1 <= 0.0f)) f.reset();
                    if (! g.active || (g.k0 <= 0.0f && g.k1 <= 0.0f)) continue;
                    load (f, g.c.a2, g.c.a1, 1.0, g.c.a1, g.c.a2);
                    if (g.m0 >= 1.0f && g.m1 >= 1.0f && g.k0 >= 1.0f && g.k1 >= 1.0f)
                    {
                        for (int i = i0; i < i1; ++i) x[i] = (float) f.process ((double) x[i]);
                    }
                    else
                    {
                        float mm = g.m0, kk = g.k0;
                        const float mStep = (g.m1 - g.m0) / (float) Stage::kSeg, kStep = (g.k1 - g.k0) / (float) Stage::kSeg;
                        for (int i = i0; i < i1; ++i, mm += mStep, kk += kStep)
                        {
                            const double v = x[i];
                            x[i] = (float) (v + mm * kk * (f.process (v) - v));
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
