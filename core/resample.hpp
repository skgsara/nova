// resample.hpp — windowed-sinc resampling between arbitrary rates.
#pragma once
#include <vector>

namespace nova {

// Bandlimited resample. quality set by number of zero crossings per side;
// the default of 16 is a tuned quality constant (kernel width per side),
// not a standards value.
std::vector<float> resample(const std::vector<float>& in, int fs_in, int fs_out,
                            int zero_crossings = 16);

// Ratio form (e.g. 1.0001 = +100 ppm clock error). ratio = out_rate/in_rate.
std::vector<float> resample_ratio(const std::vector<float>& in, double ratio,
                                  int zero_crossings = 16);

}  // namespace nova
