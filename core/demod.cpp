// demod.cpp
//
// Signal chain [docs/01 §2]:
//   x[n] -> quadrature mix to baseband at `center` -> FIR lowpass (I and Q)
//        -> inst. frequency via atan2 phase difference per sample
//        -> map [center-dev, center+dev] -> [0, 1] (black .. white)
//
// The atan2 form of the discriminator is inherently amplitude-normalized
// (cf. ACFax's px*qy - py*qx divided by pamp+qamp), so picture contrast does
// not depend on input loudness [ISO §4.2.2 input range].
#include "demod.hpp"
#include "constants.hpp"
#include <cmath>
#include <vector>

namespace nova {
namespace {

// Windowed-sinc lowpass FIR, odd length, unity DC gain.
std::vector<double> make_lowpass(int fs, double cutoff, int taps) {
    std::vector<double> h(taps);
    const int mid = taps / 2;
    double sum = 0.0;
    for (int i = 0; i < taps; i++) {
        const double d = static_cast<double>(i - mid);
        double s;
        if (std::fabs(d) < 1e-9)
            s = 2.0 * cutoff / fs;
        else
            s = std::sin(2.0 * kPi * cutoff / fs * d) / (kPi * d);
        // Blackman window
        const double u = 2.0 * i / (taps - 1) - 1.0;
        const double w = blackman(u);
        h[i] = s * w;
        sum += h[i];
    }
    for (auto& v : h) v /= sum;
    return h;
}

class Fir {
   public:
    explicit Fir(const std::vector<double>& h) : h_(h), buf_(h.size(), 0.0) {}
    double step(double x) {
        buf_[pos_] = x;
        double acc = 0.0;
        size_t j = pos_;
        for (size_t t = 0; t < h_.size(); t++) {
            acc += h_[t] * buf_[j];
            j = (j == 0) ? h_.size() - 1 : j - 1;
        }
        pos_ = (pos_ + 1) % h_.size();
        return acc;
    }

   private:
    std::vector<double> h_;
    std::vector<double> buf_;
    size_t pos_ = 0;
};
}  // namespace

std::vector<float> fm_demod(const std::vector<float>& in, int fs,
                            double center, double deviation,
                            double iq_lowpass_hz) {
    const auto h = make_lowpass(fs, iq_lowpass_hz, 63);
    Fir fi(h), fq(h);
    const double dphi = 2.0 * kPi * center / fs;

    std::vector<float> out(in.size());
    double ph = 0.0;
    double pi = 0.0, pq = 0.0;  // previous filtered I/Q
    for (size_t n = 0; n < in.size(); n++) {
        ph -= dphi;  // e^{-j 2 pi fc t}: positive offset -> positive phase step
        if (ph < -kPi) ph += 2.0 * kPi;
        const double ci = fi.step(in[n] * std::cos(ph));
        const double cq = fq.step(in[n] * std::sin(ph));
        // phase difference z[n] * conj(z[n-1])
        const double re = ci * pi + cq * pq;
        const double im = cq * pi - ci * pq;
        double f = 0.0;
        if (re != 0.0 || im != 0.0)
            f = std::atan2(im, re) * fs / (2.0 * kPi);  // Hz from center
        double v = (f + deviation) / (2.0 * deviation);  // black..white
        v = v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
        out[n] = static_cast<float>(v);
        pi = ci;
        pq = cq;
    }
    return out;
}

}  // namespace nova
