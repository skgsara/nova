// resample.cpp — windowed-sinc interpolator, direct (non-polyphase) form.
// Adequate for offline decode of recordings; not optimized for realtime.
#include "resample.hpp"
#include "constants.hpp"
#include <cmath>
#include <algorithm>

namespace nova {
namespace {
// Denial-of-service bounds, not DSP limits — see resample_ratio.
constexpr double kMinRatio = 1.0 / 1024.0;
constexpr double kMaxRatio = 64.0;
}

std::vector<float> resample(const std::vector<float>& in, int fs_in, int fs_out,
                            int zero_crossings) {
    if (fs_in <= 0 || fs_out <= 0)
        throw std::invalid_argument("resample: bad rate");
    if (fs_in == fs_out) return in;
    return resample_ratio(in, static_cast<double>(fs_out) / fs_in,
                          zero_crossings);
}

std::vector<float> resample_ratio(const std::vector<float>& in, double ratio,
                                  int zero_crossings) {
    if (ratio <= 0.0) throw std::invalid_argument("resample_ratio: bad ratio");
    // out_n below is in.size() * ratio, so an unbounded ratio turns a small
    // input into an arbitrarily large allocation AND an arbitrarily long
    // convolution — the cost is not the memory alone. Real use spans about
    // 0.04 (192 kHz -> 8 kHz) to 6.0 (8 kHz -> 48 kHz); these bounds sit far
    // outside that and exist only to refuse the absurd case rather than
    // attempt it [audit Pass D, D-PERF-002]. read_wav rejects the rates that
    // reach here from a file; this guard covers every other caller.
    if (ratio < kMinRatio || ratio > kMaxRatio)
        throw std::invalid_argument("resample_ratio: ratio out of range");
    if (in.empty()) return {};
    if (ratio == 1.0) return in;

    const size_t out_n = static_cast<size_t>(in.size() * ratio);
    std::vector<float> out(out_n);

    // Cutoff at the lower Nyquist; when downsampling, scale the sinc.
    const double fc = (ratio < 1.0) ? ratio : 1.0;  // fraction of fs_in Nyq
    const int zc = zero_crossings;

    for (size_t i = 0; i < out_n; i++) {
        const double t = static_cast<double>(i) / ratio;  // input position
        const long center = static_cast<long>(t);
        double sum = 0.0, norm = 0.0;
        for (long k = center - zc; k <= center + zc; k++) {
            if (k < 0 || k >= static_cast<long>(in.size())) continue;
            const double d = t - static_cast<double>(k);
            // windowed sinc: sinc(fc*d) * Blackman window over +/- zc/fc
            const double bw = std::min<double>(zc / fc, zc * 2.0);
            double w = 0.0;
            const double u = d / bw;  // -1..1 across window
            if (u > -1.0 && u < 1.0)
                w = blackman(u);
            double s = 0.0;
            const double x = fc * d;
            if (std::fabs(x) < 1e-9)
                s = fc;
            else
                s = fc * std::sin(kPi * x) / (kPi * x);
            const double g = s * w;
            sum += g * in[static_cast<size_t>(k)];
            norm += g;
        }
        out[i] = (norm != 0.0) ? static_cast<float>(sum / norm)
                               : static_cast<float>(sum);
    }
    return out;
}

}  // namespace nova
