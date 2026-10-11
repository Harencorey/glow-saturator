#pragma once
#include <cmath>

// RBJ-cookbook biquad, transposed direct form II, double precision.
// Double matters here: some of these filters sit at 1-50 Hz, where float
// coefficients lose accuracy (like a cheap pot at the bottom of its travel).
// Formulas are identical to model/model.py so the C++ matches the Python fit.
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    void reset() noexcept { z1 = z2 = 0; }

    inline double process (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void setIdentity() noexcept { b0 = 1; b1 = b2 = a1 = a2 = 0; }

    void setHighPass (double fc, double q, double sr) noexcept
    {
        const double w = 2.0 * M_PI * fc / sr, al = std::sin (w) / (2.0 * q), c = std::cos (w);
        set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }

    // 1st-order bilinear high-pass (a coupling capacitor), stored in biquad form.
    void setOnePoleHighPass (double fc, double sr) noexcept
    {
        const double k = std::tan (M_PI * fc / sr);
        b0 = 1.0 / (1 + k); b1 = -b0; b2 = 0;
        a1 = (k - 1) / (1 + k); a2 = 0;
    }

    void setPeak (double fc, double gainDb, double q, double sr) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = 2.0 * M_PI * fc / sr, al = std::sin (w) / (2.0 * q), c = std::cos (w);
        set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }

    void setHighShelf (double fc, double gainDb, double q, double sr) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = 2.0 * M_PI * fc / sr, al = std::sin (w) / (2.0 * q), c = std::cos (w);
        const double sA = 2.0 * std::sqrt (A) * al;
        set (A * ((A + 1) + (A - 1) * c + sA), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sA),
             (A + 1) - (A - 1) * c + sA, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sA);
    }

private:
    void set (double nb0, double nb1, double nb2, double na0, double na1, double na2) noexcept
    {
        b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0; a1 = na1 / na0; a2 = na2 / na0;
    }
};
