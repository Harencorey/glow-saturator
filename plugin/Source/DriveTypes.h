#pragma once
#include <cmath>
#include <algorithm>

// The drive types besides TUBE. Each one is a "core" that runs at the oversampled rate on
// the driven signal x (the tube path's front end, coupling cap and Drive gain stay shared).
//
// Every core is exactly unity for small signals: it only ADDS harmonics/compression as the
// level rises, it never changes the clean tone or phase. That's what keeps the Dry/Wet phase
// lock, Keep Lows and the Mix exact whichever type is selected.
//
// Even and Odd mean the same in every type: Even scales the even-symmetric part of the curve
// (each core has a little natural asymmetry, so 50 % = as designed, 0 % = none, 100 % = 3x),
// Odd changes how hard the core is hit (handled by the caller, like Tube). Density is each
// type's own character control.
namespace DriveTypes
{
    enum Type { tube = 0, iron, tape, diode, clip, count };
    inline const char* name (int t)
    {
        static const char* n[] { "TUBE", "IRON", "TAPE", "DIODE", "CLIP" };
        return n[std::clamp (t, 0, (int) count - 1)];
    }

    // Natural asymmetry of each core (ceiling 1 - a on the positive side, 1 + a on the
    // negative side): the source of its even harmonics at Even = 50 %.
    inline constexpr float kAsym[count] { 0.0f, 0.05f, 0.03f, 0.08f, 0.05f };

    // soft curve r / sqrt(1 + r^2) with asymmetric ceilings (cheap: no pow)
    inline float soft2 (float v, float asym) noexcept
    {
        const float c = v >= 0.0f ? 1.0f - asym : 1.0f + asym;
        const float r = v / c;
        return c * r / std::sqrt (1.0f + r * r);
    }

    // Even scaling around any curve f: odd part + e x even part
    template <typename F>
    inline float evenScaled (F&& f, float v, float e) noexcept
    {
        const float a = f (v);
        if (std::abs (e - 1.0f) < 1.0e-6f) return a;
        const float b = f (-v);
        return 0.5f * (a - b) + e * 0.5f * (a + b);
    }

    // asymmetric soft clip read from a knee table (the same shape family as the tube stages)
    template <typename Knee>
    inline float clipKnee (float v, const Knee& knee, float asym) noexcept
    {
        const float c = v >= 0.0f ? 1.0f - asym : 1.0f + asym;
        const float r = v / c;
        const float ar = std::abs (r);
        if (ar < 1.0e-9f) return v;
        if (ar <= 1.0f) return c * r * knee (ar);
        return c * (r > 0.0f ? 1.0f : -1.0f) * knee (1.0f / ar);
    }

    //==========================================================================
    // Hard clipper with a quadratic corner of half-width k (k = 0: brickwall), asymmetric
    // ceilings, and first-order antiderivative antialiasing (ADAA): the output is the average
    // of the curve over each step between samples, which removes most of the aliasing a hard
    // corner makes. Small signals pass as the average of two samples (a half-sample delay at
    // the oversampled rate: 1.3 us, inaudible, no level change below 20 kHz).
    struct HardClip
    {
        // positive side, ceiling c, knee half-width k
        static double fPos (double x, double c, double k) noexcept
        {
            if (k <= 0.0) return std::min (x, c);
            const double a = c - k;
            if (x <= a) return x;
            if (x >= c + k) return c;
            const double d = x - a;
            return x - d * d / (4.0 * k);
        }
        static double FPos (double x, double c, double k) noexcept   // integral of fPos from 0 to x >= 0
        {
            const double a = c - k;
            if (x <= a) return 0.5 * x * x;
            if (k <= 0.0) return 0.5 * c * c + c * (x - c);
            const double b = c + k;
            auto knee = [a, k] (double t) { const double d = t - a; return 0.5 * t * t - d * d * d / (12.0 * k); };
            if (x <= b) return knee (x);
            return knee (b) + c * (x - b);
        }
        // the full curve and its antiderivative (both signs)
        static double f (double x, double cp, double cn, double k) noexcept { return x >= 0.0 ? fPos (x, cp, k) : -fPos (-x, cn, k); }
        static double F (double x, double cp, double cn, double k) noexcept { return x >= 0.0 ? FPos (x, cp, k) : FPos (-x, cn, k); }

        // Even scaling: y = (1+e)/2 f(x) + (e-1)/2 f(-x); its antiderivative uses F(x) and -F(-x)
        static double g (double x, double cp, double cn, double k, double e) noexcept
        {
            return 0.5 * (1.0 + e) * f (x, cp, cn, k) + 0.5 * (e - 1.0) * f (-x, cp, cn, k);
        }
        static double G (double x, double cp, double cn, double k, double e) noexcept
        {
            return 0.5 * (1.0 + e) * F (x, cp, cn, k) - 0.5 * (e - 1.0) * F (-x, cp, cn, k);
        }

        double px = 0.0, pG = 0.0;
        bool fresh = true;
        void reset() { fresh = true; px = pG = 0.0; }

        float process (float xin, float asym, float k, float e) noexcept
        {
            const double x = xin, cp = 1.0 - asym, cn = 1.0 + asym;
            const double Gx = G (x, cp, cn, k, e);
            if (fresh) { fresh = false; px = x; pG = Gx; return (float) g (x, cp, cn, k, e); }
            const double dx = x - px;
            double y;
            if (std::abs (dx) > 1.0e-5) y = (Gx - pG) / dx;
            else y = g (0.5 * (x + px), cp, cn, k, e);
            px = x; pG = Gx;
            return (float) y;
        }
        // stateless version (loudness estimate for the Odd auto-gain)
        static float peek (float x, float asym, float k, float e) noexcept
        {
            return (float) g (x, 1.0 - asym, 1.0 + asym, k, e);
        }
    };

    //==========================================================================
    // Per-channel state of the cores that have memory
    // The filters run on the signal BEFORE the Drive gain (they're linear, so scaling their
    // output by the gain is the same thing), and they run all the time whichever type is
    // selected: a type you switch to is already settled, so the switch is a clean crossfade.
    struct State
    {
        double ironLp = 0.0;     // IRON: core flux (low-passed signal)
        double tapeLp = 0.0;     // TAPE: low band (the highs are what's above it)
        HardClip hard;           // CLIP (antialiasing memory: restarts cleanly when selected)
        void reset() { ironLp = tapeLp = 0.0; hard.reset(); }
        void resetType (int t) { if (t == clip) hard.reset(); }
        void update (const struct Settings& s, float v) noexcept;
    };

    // Per-block settings derived from Density (and the sample rate)
    struct Settings
    {
        float ironA = 0.0f;        // IRON flux low-pass coefficient
        float tapeA = 0.0f;        // TAPE split low-pass coefficient
        float tapeHfDrive = 1.0f;  // TAPE: how much harder the highs hit their own saturation
        float clipKnee = 0.1f;     // CLIP: corner half-width
    };

    inline void State::update (const Settings& s, float v) noexcept
    {
        ironLp += (double) s.ironA * ((double) v - ironLp);
        tapeLp += (double) s.tapeA * ((double) v - tapeLp);
    }

    inline float onePoleCoeff (double fc, double fs) { return (float) (1.0 - std::exp (-2.0 * 3.14159265358979 * fc / fs)); }

    // Density 0..100 -> each type's character. fsOs: the oversampled rate.
    inline Settings settingsFor (float density, double fsOs)
    {
        const float d = std::clamp (density, 0.0f, 100.0f) / 100.0f;
        Settings s;
        // IRON: core size. The flux low-pass corner sets how far up the spectrum the iron
        // saturates: 40 Hz (big core: only the deepest lows) .. 400 Hz (small core: up into
        // the low mids, thicker and grittier). 50 % = ~126 Hz.
        s.ironA = onePoleCoeff (40.0 * std::pow (10.0, (double) d), fsOs);
        // TAPE: tape level. The highs above the split saturate on their own, earlier than the
        // rest (self-erasure: loud highs soften first). Split 12 kHz .. 3 kHz, highs hit
        // 0 .. +12 dB harder. 50 % = 6 kHz, +6 dB.
        s.tapeA = onePoleCoeff (12000.0 * std::pow (0.25, (double) d), fsOs);
        s.tapeHfDrive = std::pow (10.0f, 12.0f * d / 20.0f);
        // CLIP: corner. 0 % = slightly rounded (half-width 0.4), 50 % = 0.1, 100 % = brickwall.
        s.clipKnee = 0.4f * (1.0f - d) * (1.0f - d);
        return s;
    }

    // DIODE knee hardness from Density: germanium (soft, 2.5) .. silicon (hard, 7)
    inline float diodeSharp (float density) { return 2.5f + 4.5f * std::clamp (density, 0.0f, 100.0f) / 100.0f; }

    //==========================================================================
    // One sample of a core: v = the signal before the Drive gain (State::update() already run
    // for this sample), gain = the Drive gain. 'diodeKnee' is the DIODE curve table.
    template <typename Knee>
    inline float process (int type, State& st, const Settings& s, const Knee& diodeKnee, float v, float gain, float o, float e) noexcept
    {
        const float x = v * gain;
        switch (type)
        {
            case iron:
            {
                // transformer: the core saturates on flux (the low-passed signal), so the lows
                // compress and grow harmonics while the highs pass clean. y = x - flux + sat(flux).
                // Odd (o = its gain, already inside 'gain') only pushes the flux harder: the clean
                // part stays at the nominal level, so Odd never turns the highs up.
                const float gn = gain / o, xn = v * gn, phi = (float) st.ironLp * gn;
                return xn - phi + evenScaled ([] (float q) { return soft2 (q, kAsym[iron]); }, phi * o, e) / o;
            }
            case tape:
            {
                // tape: the highs saturate on their own first (self-erasure), then the whole
                // signal goes through a soft, mostly odd saturation
                const float lo = (float) st.tapeLp * gain, hi = x - lo;
                const float hiS = soft2 (hi * s.tapeHfDrive, 0.0f) / s.tapeHfDrive;
                return evenScaled ([] (float q) { return soft2 (q, kAsym[tape]); }, lo + hiS, e);
            }
            case diode:
                return evenScaled ([&diodeKnee] (float q) { return clipKnee (q, diodeKnee, kAsym[diode]); }, x, e);
            case clip:
                return st.hard.process (x, kAsym[clip], s.clipKnee, e);
            default:
                return x;
        }
    }

    // The same core with the Drive gain x scale, without touching the state (the Odd
    // auto-gain's reference).
    template <typename Knee>
    inline float peek (int type, const State& st, const Settings& s, const Knee& diodeKnee, float v, float gain, float o, float scale, float e) noexcept
    {
        const float gs = gain * scale, xs = v * gs;
        switch (type)
        {
            case iron:
            {
                const float os = o * scale, gn = gain / o, xn = v * gn, phi = (float) st.ironLp * gn;
                return xn - phi + evenScaled ([] (float q) { return soft2 (q, kAsym[iron]); }, phi * os, e) / os;
            }
            case tape:
            {
                const float lo = (float) st.tapeLp * gs, hi = xs - lo;
                const float hiS = soft2 (hi * s.tapeHfDrive, 0.0f) / s.tapeHfDrive;
                return evenScaled ([] (float q) { return soft2 (q, kAsym[tape]); }, lo + hiS, e);
            }
            case diode:
                return evenScaled ([&diodeKnee] (float q) { return clipKnee (q, diodeKnee, kAsym[diode]); }, xs, e);
            case clip:
                return HardClip::peek (xs, kAsym[clip], s.clipKnee, e);
            default:
                return xs;
        }
    }
}
