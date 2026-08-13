// stream.cpp — see stream.hpp for the contract and for why this wraps
// the batch functions instead of replacing them.
#include "stream.hpp"

#include "../core/demod.hpp"
#include "../core/resample.hpp"

#include <algorithm>
#include <stdexcept>

namespace nova {
namespace {

long long gcd_ll(long long a, long long b) {
    while (b != 0) {
        const long long t = a % b;
        a = b;
        b = t;
    }
    return a;
}

// A step small enough to keep latency low and large enough that the
// context resampled on either side of it is not most of the work. At
// 44100 the block is already 441 samples; at 48000 it is 6, and 43 of
// them are taken at a time.
constexpr long long kMinStepSamples = 256;

}  // namespace

StreamResampler::StreamResampler(int fs_in, int fs_out, int zero_crossings)
    : fs_in_(fs_in), fs_out_(fs_out), zc_(zero_crossings) {
    if (fs_in <= 0 || fs_out <= 0)
        throw std::invalid_argument("StreamResampler: bad rate");
    // The identical expression core/resample.cpp forms, so that the two
    // paths divide by the same double.
    ratio_ = static_cast<double>(fs_out) / fs_in;
    passthrough_ = (fs_in == fs_out);

    const long long g = gcd_ll(fs_in, fs_out);
    q_ = fs_in / g;
    p_ = fs_out / g;
    // Context is rounded up to a whole number of blocks, so a segment
    // always starts on an exact output boundary and the local output
    // index maps to the absolute one by a plain integer offset.
    ctx_ = ((zc_ + q_ - 1) / q_) * q_;
    if (ctx_ < q_) ctx_ = q_;
    step_ = q_ * std::max<long long>(1, (kMinStepSamples + q_ - 1) / q_);
}

std::vector<float> StreamResampler::push(const float* in, std::size_t n) {
    if (passthrough_) {
        total_in_ += static_cast<long long>(n);
        consumed_ = total_in_;
        out_emitted_ += static_cast<long long>(n);
        return std::vector<float>(in, in + n);
    }
    buf_.insert(buf_.end(), in, in + n);
    total_in_ += static_cast<long long>(n);
    return drain(false);
}

std::vector<float> StreamResampler::flush() {
    if (passthrough_) return {};
    return drain(true);
}

std::vector<float> StreamResampler::drain(bool final_flush) {
    std::vector<float> out;
    while (true) {
        long long want_in;
        long long seg_end;
        if (!final_flush) {
            // A step is emitted only when its whole lookahead has
            // arrived: the kernel is centred, so the last output of this
            // step reads input beyond it.
            if (total_in_ < consumed_ + step_ + ctx_) break;
            want_in = step_;
            seg_end = consumed_ + step_ + ctx_;
        } else {
            if (consumed_ >= total_in_) break;
            want_in = total_in_ - consumed_;
            seg_end = total_in_;
        }

        const long long left = std::min<long long>(ctx_, consumed_);
        const long long seg_start = consumed_ - left;
        const std::vector<float> seg(
            buf_.begin() + static_cast<std::size_t>(seg_start - buf_start_),
            buf_.begin() + static_cast<std::size_t>(seg_end - buf_start_));

        const std::vector<float> r = resample_ratio(seg, ratio_, zc_);

        // The segment starts `left` input samples before the region we
        // are emitting, and `left` is a whole number of blocks, so the
        // first output we want sits at exactly this local index.
        const long long offset = left / q_ * p_;
        long long count;
        if (!final_flush) {
            count = want_in / q_ * p_;
        } else {
            // The batch call's total is floor(total_in * ratio) formed
            // by the same cast, and the last partial block is whatever
            // is left of it.
            const long long target =
                static_cast<long long>(static_cast<std::size_t>(
                    static_cast<double>(total_in_) * ratio_));
            count = target - out_emitted_;
        }
        if (count < 0) count = 0;
        const long long avail = static_cast<long long>(r.size()) - offset;
        if (count > avail) count = std::max<long long>(0, avail);

        out.insert(out.end(), r.begin() + static_cast<std::size_t>(offset),
                   r.begin() + static_cast<std::size_t>(offset + count));
        out_emitted_ += count;
        consumed_ += want_in;

        // Drop input that no later segment can reach back to.
        const long long keep_from = std::max<long long>(0, consumed_ - ctx_);
        if (keep_from > buf_start_) {
            buf_.erase(buf_.begin(),
                       buf_.begin() +
                           static_cast<std::size_t>(keep_from - buf_start_));
            buf_start_ = keep_from;
        }
    }
    return out;
}

StreamDemod::StreamDemod(int fs, double center, double deviation,
                         double iq_lowpass_hz, int overlap)
    : fs_(fs),
      center_(center),
      deviation_(deviation),
      lowpass_(iq_lowpass_hz),
      overlap_(overlap) {
    if (overlap_ < 0) throw std::invalid_argument("StreamDemod: bad overlap");
}

std::vector<float> StreamDemod::push(const float* in, std::size_t n) {
    if (n == 0) return {};
    std::vector<float> seg;
    seg.reserve(hist_.size() + n);
    seg.insert(seg.end(), hist_.begin(), hist_.end());
    seg.insert(seg.end(), in, in + n);

    const std::vector<float> v = fm_demod(seg, fs_, center_, deviation_,
                                          lowpass_);
    // The first hist_.size() outputs were emitted by the previous call;
    // here they are only the warm-up that makes the rest correct. On the
    // first call there is no history, so the stream's own opening
    // samples are produced exactly as the batch produces them, filter
    // cold and all.
    std::vector<float> out(v.begin() + static_cast<std::ptrdiff_t>(hist_.size()),
                           v.end());

    const std::size_t keep =
        std::min<std::size_t>(static_cast<std::size_t>(overlap_), seg.size());
    hist_.assign(seg.end() - static_cast<std::ptrdiff_t>(keep), seg.end());
    return out;
}

}  // namespace nova
