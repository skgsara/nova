#include "spectrum.hpp"

#include <algorithm>
#include <cmath>

namespace nova {
namespace {

constexpr double kPi = 3.14159265358979323846;

// Round up to a power of two, which the radix-2 transform below requires.
// Clamped rather than rejected: a caller that asks for 3000 gets 4096 and a
// working strip, where a throw would take the whole window down over a
// display setting.
int round_pow2(int n) {
    int p = 64;
    while (p < n && p < (1 << 20)) p <<= 1;
    return p;
}

}  // namespace

SpectrumAnalyzer::SpectrumAnalyzer(int sample_rate, const SpectrumOptions& opt)
    : fs_(sample_rate > 0 ? sample_rate : 48000),
      opt_(opt),
      columns_(std::max(2, opt.columns)),
      history_(std::max(1, opt.history)),
      fft_size_(round_pow2(std::max(64, opt.fft_size))) {
    hop_ = static_cast<std::size_t>(
        std::max(1L, std::lround(opt_.hop_sec * fs_)));

    fifo_.assign(static_cast<std::size_t>(fft_size_), 0.0f);
    re_.assign(static_cast<std::size_t>(fft_size_), 0.0);
    im_.assign(static_cast<std::size_t>(fft_size_), 0.0);

    // Hann. Its sum is the normalisation that makes a full-scale sine read
    // 0 dB, so it is computed once here and never re-derived.
    window_.assign(static_cast<std::size_t>(fft_size_), 0.0);
    window_sum_ = 0.0;
    for (int i = 0; i < fft_size_; i++) {
        const double w =
            0.5 - 0.5 * std::cos(2.0 * kPi * i / (fft_size_ - 1));
        window_[static_cast<std::size_t>(i)] = w;
        window_sum_ += w;
    }

    // Bit-reversal permutation, precomputed for the same reason the window
    // is: it depends only on the size.
    bitrev_.assign(static_cast<std::size_t>(fft_size_), 0);
    int bits = 0;
    while ((1 << bits) < fft_size_) bits++;
    for (int i = 0; i < fft_size_; i++) {
        int r = 0;
        for (int b = 0; b < bits; b++)
            if (i & (1 << b)) r |= 1 << (bits - 1 - b);
        bitrev_[static_cast<std::size_t>(i)] = r;
    }

    ring_.assign(static_cast<std::size_t>(history_) *
                     static_cast<std::size_t>(columns_),
                 0.0f);
    latest_.clear();
}

double spectrum_column_hz(const SpectrumOptions& opt, int col) {
    const int cols = std::max(2, opt.columns);
    const double df = (opt.f_hi - opt.f_lo) / cols;
    return opt.f_lo + (col + 0.5) * df;
}

int spectrum_hz_column(const SpectrumOptions& opt, double hz) {
    if (hz < opt.f_lo || hz >= opt.f_hi) return -1;
    const int cols = std::max(2, opt.columns);
    const double df = (opt.f_hi - opt.f_lo) / cols;
    const int c = static_cast<int>(std::floor((hz - opt.f_lo) / df));
    return c < 0 ? -1 : (c >= cols ? -1 : c);
}

const float* SpectrumAnalyzer::row(int r) const {
    if (r < 0 || r >= rows_filled()) return nullptr;
    int idx = head_ - 1 - r;
    while (idx < 0) idx += history_;
    idx %= history_;
    return &ring_[static_cast<std::size_t>(idx) *
                  static_cast<std::size_t>(columns_)];
}

int SpectrumAnalyzer::push(const float* x, std::size_t n) {
    if (!x || n == 0) return 0;
    const std::size_t N = static_cast<std::size_t>(fft_size_);
    int produced = 0;

    for (std::size_t i = 0; i < n; i++) {
        // The FIFO is a ring: `have_ % N` is where the next sample goes, so
        // admitting a sample is O(1) and a 1024-frame block does not shift
        // 4096 floats.
        fifo_[have_ % N] = x[i];
        have_++;
        fill_++;
        if (fill_ >= hop_ && have_ >= N) {
            transform_window();
            fill_ = 0;
            produced++;
        }
    }
    return produced;
}

void SpectrumAnalyzer::transform_window() {
    const std::size_t N = static_cast<std::size_t>(fft_size_);
    // Oldest sample of the window first. `have_` counts every sample ever
    // admitted, so the window's start is exactly `have_ - N`.
    const std::size_t start = have_ - N;
    for (std::size_t i = 0; i < N; i++) {
        const double v = fifo_[(start + i) % N] * window_[i];
        re_[static_cast<std::size_t>(bitrev_[i])] = v;
        im_[static_cast<std::size_t>(bitrev_[i])] = 0.0;
    }

    // Iterative radix-2 Cooley-Tukey, decimation in time. Written here
    // rather than taken from a library because nova-live is dependency-free
    // by decision [CMakeLists on nova-live] and this is forty lines.
    for (std::size_t len = 2; len <= N; len <<= 1) {
        const double ang = -2.0 * kPi / len;
        const double wr = std::cos(ang), wi = std::sin(ang);
        for (std::size_t i = 0; i < N; i += len) {
            double cr = 1.0, ci = 0.0;
            for (std::size_t j = 0; j < len / 2; j++) {
                const std::size_t a = i + j, b = i + j + len / 2;
                const double tr = re_[b] * cr - im_[b] * ci;
                const double ti = re_[b] * ci + im_[b] * cr;
                re_[b] = re_[a] - tr;
                im_[b] = im_[a] - ti;
                re_[a] += tr;
                im_[a] += ti;
                const double ncr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = ncr;
            }
        }
    }

    // One column at a time, by linear interpolation between the two FFT
    // bins either side of the column's centre frequency.
    //
    // Why interpolate rather than take the loudest bin in the column: at
    // 48 kHz and 4096 points the bins (11.7 Hz) are COARSER than the
    // columns (8.6 Hz), so a nearest-bin rule would draw each bin as a
    // plateau two or three columns wide and the position of the peak would
    // be ambiguous by exactly that much. That ambiguity is not a display
    // nicety — it is the difference between a marker line you can null a
    // mistune against and one you cannot. Interpolation gives a single
    // well-defined maximum within about half a bin of the true frequency.
    latest_.assign(static_cast<std::size_t>(columns_), 0.0f);
    const double bin_hz = static_cast<double>(fs_) / fft_size_;
    const double span_db = opt_.top_db - opt_.floor_db;
    for (int c = 0; c < columns_; c++) {
        const double b = column_hz(c) / bin_hz;
        double mag = 0.0;
        if (b >= 0.0 && b < fft_size_ / 2.0 - 1.0) {
            const std::size_t b0 = static_cast<std::size_t>(b);
            const double frac = b - b0;
            const double m0 = std::hypot(re_[b0], im_[b0]);
            const double m1 = std::hypot(re_[b0 + 1], im_[b0 + 1]);
            mag = m0 + (m1 - m0) * frac;
        }
        // 2/window_sum makes a full-scale sine at a bin centre read 1.0,
        // hence 0 dB — so the top of the scale means "as loud as this sound
        // card can report" and not an arbitrary reference.
        const double amp = 2.0 * mag / window_sum_;
        const double db = amp > 1e-12 ? 20.0 * std::log10(amp) : -240.0;
        const double f = span_db > 0.0 ? (db - opt_.floor_db) / span_db : 0.0;
        latest_[static_cast<std::size_t>(c)] =
            static_cast<float>(std::min(1.0, std::max(0.0, f)));
    }

    std::copy(latest_.begin(), latest_.end(),
              ring_.begin() + static_cast<std::ptrdiff_t>(head_) * columns_);
    head_ = (head_ + 1) % history_;
    // Saturates rather than counting: `rows_` only ever answers "how much
    // of the ring is real", and a counter that runs for a year and wraps
    // would answer it with a negative number.
    if (rows_ < history_) rows_++;
}

}  // namespace nova
