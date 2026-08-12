// demod.hpp — FM subcarrier demodulation (quadrature + phase-difference
// discriminator; ACFax-lineage architecture, float implementation).
#pragma once
#include <vector>

namespace nova {

// Demodulate FM around `center` Hz with `deviation` Hz (black = center-dev,
// white = center+dev). Returns video level in [0,1]: 0 = black, 1 = white.
// iq_lowpass_hz should exceed the deviation (e.g. deviation * 1.5).
std::vector<float> fm_demod(const std::vector<float>& in, int fs,
                            double center = 1900.0, double deviation = 400.0,
                            double iq_lowpass_hz = 650.0);

}  // namespace nova
