// stream.hpp — the live audio front end: resample and demodulate a
// growing stream so that the result agrees with the whole-file path
// [docs/05 §2.2].
//
// Why this exists at all. `resample` and `fm_demod` in `core/` are pure
// batch functions over a vector, and M4 decided not to give them a
// stateful entry point — the live preview and the saved image must not
// differ for a reason that has nothing to do with the design, and the
// cheapest way to guarantee that is to keep calling the same code.
// These classes are the block-with-overlap wrappers §2.2 recommends:
// each call re-runs the batch function over a segment prefixed with
// enough real history to be warmed up, and discards the warm-up.
// `core/` is unchanged.
//
// What "agrees" means, measured rather than assumed [see
// `tests/test_live_equiv.cpp`]: identical output COUNT, and values
// identical to within a tolerance far below one 8-bit grey level
// (1/255 = 3.9e-3). The measured numbers, which came out better than
// this file first predicted:
//
//   - the demodulator is **bit-identical** — 0.0 difference on every
//     sample of every block size tried, on a real recording and on
//     generated signals at 44100 and 48000 Hz. The mixing oscillator
//     does restart at phase zero on each segment, and that is a
//     constant rotation which cancels in the phase-difference
//     discriminator; it survives in the last bits of the doubles, and
//     then vanishes when the result is rounded to `float`. Bit equality
//     is therefore *observed*, not guaranteed by construction: a value
//     landing exactly on a float rounding boundary could still differ
//     by one ulp, which is why the screamer asserts a tolerance;
//   - the resampler agrees to **5e-13**, because its output positions
//     are formed from segment-local indices. That is ten orders of
//     magnitude below a grey level, and it disappears entirely by the
//     time the video has been demodulated.
//
// Threading: these are thread-2 objects [docs/05 §2]. They allocate, so
// they must never be called from the RtAudio callback.
#pragma once

#include <cstddef>
#include <vector>

namespace nova {

// The demodulator's warm-up, in samples, for the 63-tap I/Q lowpass in
// `core/demod.cpp`.
//
// **Measured, and the arithmetic argument got it wrong by one.** From
// the taps alone the requirement looks like 63: the FIR needs 62 samples
// of history to be fully fed, and the phase-difference discriminator
// needs one fully-fed sample before the first one it emits. The sweep in
// `tests/test_live_equiv.cpp` says the error reaches zero at **62**,
// and the reason is the window rather than the tap count — the Blackman
// window is zero at its endpoints, so `h[0]` and `h[62]` carry no
// weight and the filter's effective support is two taps shorter than
// its length. 61 is not enough: it leaves 8e-6, small but above the
// screamer's tolerance, which is exactly the boundary being pinned.
//
// 64 ships, for two samples of margin over a measurement that depends
// on the window's endpoints being *exactly* zero.
constexpr int kDemodOverlap = 64;

// What the sweep measured, kept next to the constant it justifies so
// that shortening one without re-running the other is visibly wrong.
constexpr int kDemodOverlapMeasured = 62;

// Resampling front end. Feed it whatever the sound card produces; it
// emits samples at fs_out, identical in count and (to the stated
// tolerance) in value to `resample(whole_stream, fs_in, fs_out)`.
//
// It consumes input in whole blocks of `q` samples, where p/q is the
// reduced ratio fs_out/fs_in — 441 input samples per 80 output at
// 44100 Hz, 6 per 1 at 48000. That is not an optimisation: it is what
// makes every emitted sample land on the same output position the batch
// call would have chosen, because a block boundary that is not a whole
// number of output periods would shift the whole rest of the stream.
class StreamResampler {
public:
    StreamResampler(int fs_in, int fs_out, int zero_crossings = 16);

    // Feed input; returns whatever output became available. Output lags
    // input by one context window plus one step, because the resampling
    // kernel is centred and needs samples from the future.
    std::vector<float> push(const float* in, std::size_t n);
    std::vector<float> push(const std::vector<float>& in) {
        return push(in.data(), in.size());
    }

    // End of stream: emits the tail, including the final samples whose
    // kernel is truncated by the end of the input — which is exactly
    // what the batch call does there, so the two agree at the edge.
    std::vector<float> flush();

    int fs_in() const { return fs_in_; }
    int fs_out() const { return fs_out_; }
    // Input samples of history and lookahead held around each segment.
    long long context() const { return ctx_; }
    // Input samples consumed per emitted step.
    long long step() const { return step_; }
    long long produced() const { return out_emitted_; }

private:
    std::vector<float> drain(bool final_flush);

    int fs_in_;
    int fs_out_;
    int zc_;
    double ratio_ = 1.0;
    bool passthrough_ = false;
    long long p_ = 1;     // output samples per q_ input samples
    long long q_ = 1;     // the input block that lands on an exact boundary
    long long ctx_ = 0;   // history/lookahead, a whole number of q_
    long long step_ = 1;  // input consumed per emitted step, a multiple of q_

    std::vector<float> buf_;    // buffered input, absolute [buf_start_, total_in_)
    long long buf_start_ = 0;
    long long consumed_ = 0;    // absolute input index already turned into output
    long long total_in_ = 0;
    long long out_emitted_ = 0;
};

// FM demodulation over a growing stream. Unlike the resampler this has
// no latency: the filter is causal, so every input sample can be
// demodulated as soon as it arrives, given `overlap` samples of history.
class StreamDemod {
public:
    // 1900 Hz: the WEFAX audio subcarrier centre frequency [WMO §5.5.1].
    explicit StreamDemod(int fs, double center = 1900.0,
                         double deviation = 400.0,
                         double iq_lowpass_hz = 650.0,
                         int overlap = kDemodOverlap);

    std::vector<float> push(const float* in, std::size_t n);
    std::vector<float> push(const std::vector<float>& in) {
        return push(in.data(), in.size());
    }

    int overlap() const { return overlap_; }

private:
    int fs_;
    double center_;
    double deviation_;
    double lowpass_;
    int overlap_;
    std::vector<float> hist_;
};

}  // namespace nova
