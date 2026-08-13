// test_live_equiv.cpp — §9 screamer 1 [docs/05]: the block-with-overlap
// streaming front end (live/stream.hpp) agrees with the whole-file path
// over a fixture, at every block size in a set.
//
// Why this is the first screamer M4's DSP owes. The live preview and the
// saved image are produced by two different call patterns over the same
// two functions — `resample` and `fm_demod` in `core/` — and if those
// patterns disagree, the picture changes when the transmission ends for
// a reason that has nothing to do with the design [docs/05 §2.2]. The
// whole point of block-with-overlap was to avoid a stateful entry point
// in `core/`; this is the test that says whether the overlap is enough.
//
// Claims defended:
//   - the streaming output has the SAME COUNT as the batch output, at
//     every block size, for both the resampler and the demodulator —
//     a sample-count drift would slant the whole picture;
//   - values agree to within kTol, which is far below one 8-bit grey
//     level (1/255 = 3.9e-3), so no pixel of the preview can differ
//     from the saved image because of the block structure;
//   - the demod overlap is a MEASURED property of the 63-tap filter,
//     not a guess: the error falls to zero at overlap 62 — one less
//     than the tap count predicts, because the Blackman window's
//     endpoint taps carry no weight — is still 8e-6 at 61, is worse
//     than a grey level at 32 and below, and the shipped
//     kDemodOverlap = 64 keeps two samples of margin;
//   - the resampler's block quantisation holds at real capture rates
//     (44100 and 48000 into 8000), where a boundary that is not a whole
//     number of output periods would shift the rest of the stream.
//
// Exact bit equality is NOT claimed, and the reason is arithmetic
// rather than design: the demodulator's mixing oscillator restarts at
// phase zero on every segment — a constant rotation, which cancels in
// the phase-difference discriminator in real arithmetic but not in the
// last bits of a double — and the resampler forms its output positions
// from segment-local indices. The measured numbers are printed, so a
// regression that widens them is visible even while it passes.
#include "../core/demod.hpp"
#include "../core/gen.hpp"
#include "../core/image.hpp"
#include "../core/resample.hpp"
#include "../core/wav.hpp"
#include "../live/stream.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}

// One 8-bit grey level is 1/255 = 3.9e-3. The tolerance is three orders
// of magnitude below that: anything this small cannot reach a pixel, and
// anything larger wants explaining.
const double kTol = 1e-6;

struct Diff {
    bool same_count = false;
    std::size_t n_batch = 0;
    std::size_t n_stream = 0;
    double max_abs = 0.0;
    std::size_t argmax = 0;
};

Diff compare(const std::vector<float>& batch, const std::vector<float>& stream) {
    Diff d;
    d.n_batch = batch.size();
    d.n_stream = stream.size();
    d.same_count = batch.size() == stream.size();
    const std::size_t n = std::min(batch.size(), stream.size());
    for (std::size_t i = 0; i < n; i++) {
        const double e = std::fabs(static_cast<double>(batch[i]) -
                                   static_cast<double>(stream[i]));
        if (e > d.max_abs) {
            d.max_abs = e;
            d.argmax = i;
        }
    }
    return d;
}

// The block sizes a real capture can produce: RtAudio buffer sizes,
// awkward primes, and the pathological one-sample-at-a-time case that
// exercises every partial-block path there is.
const std::size_t kBlocks[] = {1, 2, 7, 63, 64, 65, 256, 441, 1024, 4096, 44100};

// Feed a whole signal through the streaming demod in fixed-size blocks.
std::vector<float> stream_demod(const std::vector<float>& in, int fs,
                                std::size_t block, double deviation,
                                int overlap = nova::kDemodOverlap) {
    nova::StreamDemod d(fs, 1900.0, deviation, 650.0, overlap);
    std::vector<float> out;
    for (std::size_t i = 0; i < in.size(); i += block) {
        const std::size_t n = std::min(block, in.size() - i);
        const std::vector<float> got = d.push(in.data() + i, n);
        out.insert(out.end(), got.begin(), got.end());
    }
    return out;
}

// ... and through the streaming resampler, ending with the flush that
// emits the tail.
std::vector<float> stream_resample(const std::vector<float>& in, int fs_in,
                                   int fs_out, std::size_t block) {
    nova::StreamResampler r(fs_in, fs_out);
    std::vector<float> out;
    for (std::size_t i = 0; i < in.size(); i += block) {
        const std::size_t n = std::min(block, in.size() - i);
        const std::vector<float> got = r.push(in.data() + i, n);
        out.insert(out.end(), got.begin(), got.end());
    }
    const std::vector<float> tail = r.flush();
    out.insert(out.end(), tail.begin(), tail.end());
    return out;
}

void report(const char* label, const Diff& d) {
    char what[220];
    std::snprintf(what, sizeof what, "%s: %zu samples, same count", label,
                  d.n_batch);
    if (!d.same_count) {
        std::snprintf(what, sizeof what,
                      "%s: batch %zu samples, streaming %zu", label, d.n_batch,
                      d.n_stream);
        check(false, what);
        return;
    }
    check(true, what);
    std::snprintf(what, sizeof what, "%s: max |diff| %.3e at sample %zu (< %.0e)",
                  label, d.max_abs, d.argmax, kTol);
    check(d.max_abs < kTol, what);
}

// --- the demodulator, over a real recording ---------------------------------
void test_demod_fixture(const std::string& path) {
    const nova::Wav w = nova::read_wav(path);
    const std::string name = path.substr(path.find_last_of('/') + 1);
    std::printf("demod equivalence on %s (%d Hz, %zu samples)\n", name.c_str(),
                w.sample_rate, w.samples.size());

    const std::vector<float> batch =
        nova::fm_demod(w.samples, w.sample_rate, 1900.0, 400.0);

    for (const std::size_t block : kBlocks) {
        // A one-sample-at-a-time run over a whole fixture is 480000
        // calls into a 64-sample demod; the small blocks are exercised
        // over the opening seconds instead, which is where the cold
        // filter and the partial blocks both live.
        const bool small = block < 256;
        const std::size_t n =
            small ? std::min<std::size_t>(w.samples.size(),
                                          static_cast<std::size_t>(
                                              w.sample_rate * 2))
                  : w.samples.size();
        const std::vector<float> in(w.samples.begin(), w.samples.begin() + n);
        const std::vector<float> b(batch.begin(), batch.begin() + n);
        const std::vector<float> s =
            stream_demod(in, w.sample_rate, block, 400.0);
        char label[160];
        std::snprintf(label, sizeof label, "%s demod, block %zu%s", name.c_str(),
                      block, small ? " (2 s)" : "");
        report(label, compare(b, s));
    }
}

// --- the resampler and the demodulator, at real capture rates ---------------
// The fixtures are already at 8 kHz, so they cannot exercise a resampler
// at all. This generates an on-spec signal at the rate a sound card
// actually produces [docs/01 §3-4] and puts the whole front end through
// it: resample to 8 kHz, then demodulate — the exact chain of
// cli/nova-decode.cpp, and the exact chain thread 2 will run.
void test_front_end(int fs_in) {
    nova::GenOptions opt;
    opt.fs = fs_in;
    opt.lpm = 120;
    opt.ioc = 576;
    opt.noise = 0.02;
    opt.start_tone = false;
    opt.phasing = false;
    opt.stop_tone = false;
    const nova::Image content = nova::gen_test_pattern(800, 200);
    const std::vector<float> audio = nova::gen_fax_signal(content, 20, opt);
    std::printf("front end equivalence at %d Hz (%zu samples)\n", fs_in,
                audio.size());

    const std::vector<float> batch_8k = nova::resample(audio, fs_in, 8000);
    const std::vector<float> batch_video =
        nova::fm_demod(batch_8k, 8000, 1900.0, 400.0);

    for (const std::size_t block : kBlocks) {
        const bool small = block < 256;
        const std::size_t n =
            small ? std::min<std::size_t>(audio.size(),
                                          static_cast<std::size_t>(fs_in * 2))
                  : audio.size();
        const std::vector<float> in(audio.begin(), audio.begin() + n);

        // The batch answer for this slice, computed the same way the
        // whole file would be: a prefix of the stream is a stream.
        const std::vector<float> b8 = nova::resample(in, fs_in, 8000);
        const std::vector<float> bvid =
            nova::fm_demod(b8, 8000, 1900.0, 400.0);

        const std::vector<float> s8 = stream_resample(in, fs_in, 8000, block);
        char label[160];
        std::snprintf(label, sizeof label, "%d Hz resample, block %zu%s", fs_in,
                      block, small ? " (2 s)" : "");
        report(label, compare(b8, s8));

        // And the two stages composed, which is what thread 2 runs: the
        // resampler's output blocks are whatever they are, and the demod
        // must not care.
        nova::StreamResampler r(fs_in, 8000);
        nova::StreamDemod d(8000, 1900.0, 400.0);
        std::vector<float> svid;
        for (std::size_t i = 0; i < in.size(); i += block) {
            const std::size_t take = std::min(block, in.size() - i);
            const std::vector<float> mid = r.push(in.data() + i, take);
            const std::vector<float> vid = d.push(mid);
            svid.insert(svid.end(), vid.begin(), vid.end());
        }
        const std::vector<float> tail_mid = r.flush();
        const std::vector<float> tail_vid = d.push(tail_mid);
        svid.insert(svid.end(), tail_vid.begin(), tail_vid.end());

        std::snprintf(label, sizeof label, "%d Hz front end, block %zu%s", fs_in,
                      block, small ? " (2 s)" : "");
        report(label, compare(bvid, svid));
    }
}

// --- the overlap is measured, not guessed -----------------------------------
// docs/05 §2.2: "the overlap length is a measured property of the filter,
// and equality with a whole-file demod is a screamer". This is that
// measurement. The I/Q lowpass is 63 taps, so the filter is fully fed at
// 62 samples of history and the phase-difference discriminator needs one
// fed sample before the first one it emits: 63.
void test_overlap_is_measured(const std::string& path) {
    const nova::Wav w = nova::read_wav(path);
    const std::size_t n =
        std::min<std::size_t>(w.samples.size(),
                              static_cast<std::size_t>(w.sample_rate * 4));
    const std::vector<float> in(w.samples.begin(), w.samples.begin() + n);
    const std::vector<float> batch =
        nova::fm_demod(in, w.sample_rate, 1900.0, 400.0);

    std::printf("demod overlap sweep (63 taps, Blackman-windowed)\n");
    for (const int ov : {0, 1, 16, 32, 61, 62, 63, 64, 96, 128}) {
        const std::vector<float> s =
            stream_demod(in, w.sample_rate, 1000, 400.0, ov);
        const Diff d = compare(batch, s);
        std::printf("    overlap %3d: max |diff| %.3e\n", ov, d.max_abs);

        char what[200];
        if (ov >= nova::kDemodOverlapMeasured) {
            std::snprintf(what, sizeof what, "overlap %d (>= %d) agrees: %.3e",
                          ov, nova::kDemodOverlapMeasured, d.max_abs);
            check(d.max_abs < kTol, what);
        } else if (ov == nova::kDemodOverlapMeasured - 1) {
            // The boundary itself. Without this the "62" in
            // live/stream.hpp is an arbitrary number the next person is
            // free to shorten: one sample less must measurably fail.
            std::snprintf(what, sizeof what,
                          "overlap %d is NOT enough (%.3e, over the %.0e "
                          "tolerance) — 62 is a boundary, not a guess",
                          ov, d.max_abs, kTol);
            check(d.max_abs > kTol, what);
        } else if (ov <= 32) {
            std::snprintf(what, sizeof what,
                          "overlap %d is wrong by more than a grey level "
                          "(%.3e > %.1e)",
                          ov, d.max_abs, 1.0 / 255.0);
            check(d.max_abs > 1.0 / 255.0, what);
        }
    }
    char what[160];
    std::snprintf(what, sizeof what,
                  "the shipped kDemodOverlap (%d) is above the measured "
                  "requirement (%d)",
                  nova::kDemodOverlap, nova::kDemodOverlapMeasured);
    check(nova::kDemodOverlap >= nova::kDemodOverlapMeasured, what);
}

}  // namespace

int main(int argc, char** argv) {
    std::printf("live_demod_equiv: streaming front end == whole-file path\n");

    for (int i = 1; i < argc; i++) test_demod_fixture(argv[i]);
    if (argc > 1) test_overlap_is_measured(argv[1]);

    test_front_end(44100);
    test_front_end(48000);

    if (failures == 0)
        std::printf("live_demod_equiv: all checks passed\n");
    else
        std::printf("live_demod_equiv: %d check(s) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
