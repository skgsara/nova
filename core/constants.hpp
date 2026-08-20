// constants.hpp — mathematical constants shared by the core .cpp files.
// Internal to nova-core; not part of the public API. (C++17 has no
// <numbers> header, so kPi lives here rather than being re-declared per
// file — it used to exist five times, byte-identical [audit Pass C,
// C-MAINT-016].)
#pragma once
#include <cmath>

namespace nova {

constexpr double kPi = 3.14159265358979323846;

// Blackman window, u in [-1, 1]. Used by the demodulator's FIR lowpass
// (demod.cpp) and the resampler's windowed sinc (resample.cpp).
inline double blackman(double u) {
    return 0.42 + 0.5 * std::cos(kPi * u) + 0.08 * std::cos(2.0 * kPi * u);
}

}  // namespace nova
