// test_live_tones.cpp — §9 screamer 2 [docs/05]: the streaming tone
// detector (live/tone_stream.hpp) reaches the same verdicts as
// `detect_tones`, as they arrive.
//
// Why M4 owes this test. The live session state machine starts decoding
// on a start tone and ends the transmission on a stop tone [docs/05 §4].
// If the streaming detector invents a tone the batch path would not
// find, the operator watches a picture begin out of noise; if it misses
// one, nothing begins at all. Neither failure is visible in the saved
// image, because the saved image is produced by the batch path — so
// nothing else in the suite can catch it.
//
// Claims defended:
//   - **same kinds, same order, same start times** as `detect_tones`, on
//     every fixture in the library and on generated signals. The
//     tolerance on `t_start` is one hop (§9), but the measured
//     difference is printed, because a run begins at the same FRAME in
//     both paths and so the honest expectation is exactly zero;
//   - **no tone invented and none missed** on the eleven library
//     fixtures that carry no control tone at all — the false-start trap
//     of M3, now in a second implementation;
//   - **the event list does not depend on the block size**, at every
//     size from one sample to 65536. A detector whose verdicts depend on
//     how the audio callback happened to chunk the stream is broken, and
//     this is the same claim `live_preview` will make about pixels;
//   - **the streaming detector commits before the run ends** — the
//     entire point of it — measured as the lead over the batch path's
//     `t_end` and printed per event;
//   - **`t_end` never runs past the batch path's**, which is what
//     "earliest qualifying moment" means arithmetically.
//
// Explicitly NOT claimed, per docs/05 §5: equality of `freq_hz` and
// `purity`. Both are medians, the batch path takes them over the whole
// run and the streaming path over the frames seen when the run
// qualified, and comparing them would be a bug in this test rather than
// in the code. What is checked instead is the claim that actually
// matters about the measured frequency — that both are inside the ±1%
// of [WMO §5.2.6].
//
// The video handed to both paths here is the whole-file `fm_demod`
// output, not the streaming front end. That is deliberate: whether the
// streaming front end produces the same video is `live_demod_equiv`'s
// claim (§9 screamer 1, measured bit-identical), and a test that stacked
// the two would not say which one had broken.
#include "../core/demod.hpp"
#include "../core/gen.hpp"
#include "../core/image.hpp"
#include "../core/tones.hpp"
#include "../core/wav.hpp"
#include "../core/constants.hpp"
#include "../live/tone_stream.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}

// Block sizes a real capture can produce, plus the pathological
// one-sample case that exercises every partial-frame path there is.
const std::size_t kBlocks[] = {1, 7, 333, 1000, 2000, 12345, 65536};

std::vector<nova::ToneEvent> stream_events(const std::vector<float>& video,
                                           int fs, std::size_t block) {
    nova::StreamToneDetector d(fs);
    std::vector<nova::ToneEvent> out;
    for (std::size_t i = 0; i < video.size(); i += block) {
        const std::size_t n = std::min(block, video.size() - i);
        const std::vector<nova::ToneEvent> got = d.push(video.data() + i, n);
        out.insert(out.end(), got.begin(), got.end());
    }
    return out;
}

// Emission order is qualification order; the batch list is sorted by
// start time. Two runs of different kinds can overlap, so these are not
// the same order in general and the comparison is made on start time.
std::vector<nova::ToneEvent> by_start(std::vector<nova::ToneEvent> v) {
    std::sort(v.begin(), v.end(),
              [](const nova::ToneEvent& a, const nova::ToneEvent& b) {
                  return a.t_start < b.t_start;
              });
    return v;
}

bool identical(const std::vector<nova::ToneEvent>& a,
               const std::vector<nova::ToneEvent>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); i++) {
        if (a[i].kind != b[i].kind) return false;
        if (a[i].t_start != b[i].t_start) return false;
        if (a[i].t_end != b[i].t_end) return false;
        if (a[i].freq_hz != b[i].freq_hz) return false;
        if (a[i].purity != b[i].purity) return false;
    }
    return true;
}

double nominal_of(nova::ToneKind k) {
    switch (k) {
        case nova::ToneKind::kStartIOC576: return 300.0;
        case nova::ToneKind::kStartIOC288: return 675.0;
        case nova::ToneKind::kStop:        return 450.0;
    }
    return 0.0;
}

// How much earlier than the batch path's run end the streaming detector
// committed, summed over the events of one signal. Zero would mean the
// streaming detector is not actually early anywhere.
double total_lead = 0.0;
int events_seen = 0;
int events_early = 0;

// The comparison §9 asks for: kinds, order, start times within one hop.
void compare(const char* label, const std::vector<nova::ToneEvent>& batch,
             const std::vector<nova::ToneEvent>& stream_in, double hop_sec) {
    const std::vector<nova::ToneEvent> stream = by_start(stream_in);
    char what[260];

    std::snprintf(what, sizeof what, "%s: %zu event(s), same count as batch",
                  label, batch.size());
    if (batch.size() != stream.size()) {
        std::snprintf(what, sizeof what,
                      "%s: batch found %zu event(s), streaming %zu", label,
                      batch.size(), stream.size());
        check(false, what);
        for (const auto& e : batch)
            std::printf("      batch  %-10s %7.2f-%7.2f s  f=%6.1f\n",
                        nova::tone_name(e.kind), e.t_start, e.t_end, e.freq_hz);
        for (const auto& e : stream)
            std::printf("      stream %-10s %7.2f-%7.2f s  f=%6.1f\n",
                        nova::tone_name(e.kind), e.t_start, e.t_end, e.freq_hz);
        return;
    }
    check(true, what);

    for (std::size_t i = 0; i < batch.size(); i++) {
        const nova::ToneEvent& b = batch[i];
        const nova::ToneEvent& s = stream[i];
        const double dt = std::fabs(s.t_start - b.t_start);
        const double lead = b.t_end - s.t_end;
        total_lead += lead;
        events_seen++;
        if (lead > 0.0) events_early++;

        std::printf("      batch  %-10s %7.2f-%7.2f s  f=%7.2f  purity=%.3f\n",
                    nova::tone_name(b.kind), b.t_start, b.t_end, b.freq_hz,
                    b.purity);
        std::printf("      stream %-10s %7.2f-%7.2f s  f=%7.2f  purity=%.3f"
                    "   (committed %.2f s before the run ended)\n",
                    nova::tone_name(s.kind), s.t_start, s.t_end, s.freq_hz,
                    s.purity, lead);

        std::snprintf(what, sizeof what, "%s: event %zu is %s in both", label,
                      i, nova::tone_name(b.kind));
        check(s.kind == b.kind, what);

        std::snprintf(what, sizeof what,
                      "%s: event %zu starts at the same time (|dt| = %.4f s, "
                      "one hop = %.3f s)",
                      label, i, dt, hop_sec);
        check(dt <= hop_sec + 1e-12, what);

        // "Earliest qualifying moment" [docs/05 §5], arithmetically: the
        // streaming detector decided on a PREFIX of the run, so its end
        // cannot be past the batch path's.
        std::snprintf(what, sizeof what,
                      "%s: event %zu ends no later than the batch run "
                      "(%.2f <= %.2f s)",
                      label, i, s.t_end, b.t_end);
        check(s.t_end <= b.t_end + 1e-12, what);

        // The medians are NOT compared [docs/05 §5]. This is the claim
        // about them that does hold on both paths.
        const double nom = nominal_of(b.kind);
        std::snprintf(what, sizeof what,
                      "%s: event %zu measures %.2f Hz, within the ±1%% of "
                      "WMO §5.2.6 about %.0f",
                      label, i, s.freq_hz, nom);
        check(std::fabs(s.freq_hz - nom) <= 0.01 * nom, what);
    }
}

// Every block size must produce the identical list — same events, same
// numbers, not merely the same verdicts.
void check_block_independence(const char* label,
                              const std::vector<float>& video, int fs) {
    const std::vector<nova::ToneEvent> ref =
        stream_events(video, fs, kBlocks[0]);
    bool all_same = true;
    for (std::size_t i = 1; i < sizeof kBlocks / sizeof kBlocks[0]; i++) {
        const std::vector<nova::ToneEvent> got =
            stream_events(video, fs, kBlocks[i]);
        if (!identical(ref, got)) {
            all_same = false;
            std::printf("      block %zu differs from block %zu: %zu vs %zu "
                        "event(s)\n",
                        kBlocks[i], kBlocks[0], got.size(), ref.size());
        }
    }
    char what[220];
    std::snprintf(what, sizeof what,
                  "%s: identical events at every block size from %zu to %zu",
                  label, kBlocks[0],
                  kBlocks[sizeof kBlocks / sizeof kBlocks[0] - 1]);
    check(all_same, what);
}

// The framing itself, pinned directly rather than inferred from the
// events: `detect_tones` walks s = 0, hop, 2*hop ... while s + n <= size
// [core/tones.cpp], and every claim about identical per-frame purity
// rests on the streaming detector cutting the stream at those same
// samples. Any block size gives the same answer, so one will do.
//
// The expected window and hop are formed HERE, from ToneOptions, by the
// same expressions core/tones.cpp uses — not read back from the
// detector. An earlier version of this check asked the detector for its
// own window and then confirmed the frame count agreed with it, which is
// a test of arithmetic rather than of the code: a one-sample shift of
// the whole grid passed it unharmed (measured, this session).
void check_frame_grid(const char* label, const std::vector<float>& video,
                      int fs) {
    const nova::ToneOptions opt;
    const long long win = static_cast<long long>(
        static_cast<std::size_t>(opt.win_sec * fs));
    const long long hop = static_cast<long long>(
        static_cast<std::size_t>(opt.hop_sec * fs));

    nova::StreamToneDetector d(fs);
    d.push(video);
    const long long n = static_cast<long long>(video.size());
    const long long expect = n < win ? 0 : (n - win) / hop + 1;

    char what[240];
    std::snprintf(what, sizeof what,
                  "%s: window %d and hop %d are detect_tones' own (%lld, "
                  "%lld)",
                  label, d.window_samples(), d.hop_samples(), win, hop);
    check(d.window_samples() == win && d.hop_samples() == hop, what);

    std::snprintf(what, sizeof what,
                  "%s: %lld frames, the same grid detect_tones walks "
                  "(expected %lld)",
                  label, d.frames(), expect);
    check(d.frames() == expect, what);
}

void run_video(const char* label, const std::vector<float>& video, int fs) {
    const nova::ToneOptions opt;
    const std::vector<nova::ToneEvent> batch = nova::detect_tones(video, fs);
    // Any block size would do here, since the next check is that they
    // all produce the identical list.
    const std::vector<nova::ToneEvent> stream = stream_events(video, fs, 2000);
    compare(label, batch, stream, opt.hop_sec);
    check_frame_grid(label, video, fs);
    check_block_independence(label, video, fs);
}

void run_fixture(const std::string& path) {
    const nova::Wav w = nova::read_wav(path);
    const std::string name = path.substr(path.find_last_of('/') + 1);
    std::printf("%s (%d Hz, %.1f s)\n", name.c_str(), w.sample_rate,
                static_cast<double>(w.samples.size()) / w.sample_rate);
    const std::vector<float> video =
        nova::fm_demod(w.samples, w.sample_rate, 1900.0, 400.0);
    run_video(name.c_str(), video, w.sample_rate);
}

// No fixture in the library carries a STOP tone — the six that carry a
// control tone all carry a start tone and nothing else (survey, this
// session), and none carries an IOC 288 opening either [AGENTS.md
// registered gaps]. So `min_stop_sec` and the 675 Hz candidate are
// exercised on generated signals, which is what the generator is for.
void run_generated(int ioc) {
    nova::GenOptions opt;
    opt.fs = 8000;
    opt.lpm = 120;
    opt.ioc = ioc;
    opt.noise = 0.02;
    opt.start_tone = true;
    opt.phasing = true;
    opt.stop_tone = true;
    const nova::Image content = nova::gen_test_pattern(800, 200);
    const std::vector<float> audio = nova::gen_fax_signal(content, 60, opt);
    const std::vector<float> video =
        nova::fm_demod(audio, opt.fs, 1900.0, 400.0);
    char label[80];
    std::snprintf(label, sizeof label, "generated IOC %d (start+phasing+stop)",
                  ioc);
    std::printf("%s (%d Hz, %.1f s)\n", label, opt.fs,
                static_cast<double>(audio.size()) / opt.fs);
    run_video(label, video, opt.fs);
}

// A tone that FADES mid-run, which is the case the gap-bridging rule
// exists for [core/tones.cpp: "HF signals fade mid-tone, and the first
// version of this code split real 5 s stop tones into sub-minimum halves
// and discarded them"] — and which nothing else here reaches. The six
// library fixtures that carry a control tone all carry a clean one, so
// with only those, halving the streaming detector's gap tolerance
// changed no verdict at all and the test noticed nothing (measured,
// this session).
//
// The fade is placed EARLY, inside the first seconds of the tone, on
// purpose. A fade in the middle of a long tone splits the run into a
// first piece that still qualifies on its own, so the reported start
// time does not move and the split is invisible. Placed at 1.0-2.5 s it
// splits the run into a first piece SHORTER than `min_start_sec`, which
// is discarded — so a detector that fails to bridge reports the tone as
// starting 2.5 s late instead of not at all, and the comparison sees it.
void run_generated_faded() {
    nova::GenOptions opt;
    opt.fs = 8000;
    opt.lpm = 120;
    opt.ioc = 576;
    opt.noise = 0.02;
    opt.start_sec = 10.0;  // the upper end of the 5-10 s of [WMO §5.2.2]
    opt.start_tone = true;
    opt.phasing = true;
    opt.stop_tone = true;
    const nova::Image content = nova::gen_test_pattern(800, 200);
    std::vector<float> audio = nova::gen_fax_signal(content, 60, opt);

    // Replace 1.0-2.5 s with noise at the same level: an HF fade leaves
    // the noise floor where the carrier was. Deterministic, because a
    // screamer that only sometimes screams is worse than none.
    unsigned int seed = 20260813u;
    const std::size_t from = static_cast<std::size_t>(1.0 * opt.fs);
    const std::size_t to = static_cast<std::size_t>(2.5 * opt.fs);
    for (std::size_t i = from; i < to && i < audio.size(); i++) {
        seed = seed * 1664525u + 1013904223u;
        const double u = static_cast<double>(seed >> 8) / 16777216.0;
        audio[i] = static_cast<float>(opt.amplitude * (2.0 * u - 1.0));
    }

    const std::vector<float> video =
        nova::fm_demod(audio, opt.fs, 1900.0, 400.0);
    std::printf("generated IOC 576 with a 1.5 s fade inside the start tone "
                "(%d Hz, %.1f s)\n",
                opt.fs, static_cast<double>(audio.size()) / opt.fs);
    run_video("faded start tone", video, opt.fs);

    // The claim that makes this case worth its seconds: the tone is
    // still reported as starting where it started, not after the fade.
    const std::vector<nova::ToneEvent> ev = stream_events(video, opt.fs, 2000);
    bool bridged = false;
    for (const auto& e : ev)
        if (e.kind == nova::ToneKind::kStartIOC576 && e.t_start < 0.5)
            bridged = true;
    check(bridged,
          "faded start tone: the fade is BRIDGED — the tone still starts "
          "at 0 s, not after it");
}

// Two unrelated bursts, which the gap rule must NOT staple into one
// event [core/tones.cpp: "Fraction of a run's span that must be above
// threshold, so the gap rule cannot staple two unrelated bursts into one
// event"]. This is the other half of the run-assembly rule, and the
// mirror image of the faded case above: there the gap had to be bridged,
// here it must not produce an event.
//
// Built in the VIDEO domain rather than as audio, because that is what
// `detect_tones` reads and because the case has to be constructed
// exactly: two half-second bursts of clean 300 Hz separated by 1.5 s of
// noise. The gap is 13 frames, inside the 16 the bridging rule allows,
// and the span reaches the 2 s minimum duration — so duration and
// spread both pass and `min_hot_frac` is the ONLY thing standing
// between this and a start tone reported out of noise. Without that
// test in the streaming path, this file produces an event and the batch
// path does not (measured, this session: dropping it was otherwise
// invisible to every fixture and every generated signal here).
void run_two_bursts() {
    const int fs = 8000;
    const double f = 300.0;  // [WMO §5.2.2]
    std::vector<float> video(static_cast<std::size_t>(6.0 * fs), 0.0f);
    unsigned int seed = 13082026u;
    for (std::size_t i = 0; i < video.size(); i++) {
        const double t = static_cast<double>(i) / fs;
        const bool in_burst = (t >= 0.0 && t < 0.5) || (t >= 2.0 && t < 2.5);
        if (in_burst) {
            // A control tone is a black/white alternation, so a square
            // wave is the real waveform, not a convenience.
            // M_PI is a POSIX extension MSVC does not define; kPi is the
            // project's own constant [core/constants.hpp].
            video[i] = std::sin(2.0 * nova::kPi * f * t) >= 0.0 ? 0.5f : -0.5f;
        } else {
            seed = seed * 1664525u + 1013904223u;
            const double u = static_cast<double>(seed >> 8) / 16777216.0;
            video[i] = static_cast<float>(0.5 * (2.0 * u - 1.0));
        }
    }

    std::printf("two 0.5 s bursts of 300 Hz, 1.5 s apart, noise elsewhere "
                "(%d Hz, %.1f s)\n",
                fs, static_cast<double>(video.size()) / fs);
    run_video("two bursts", video, fs);

    const std::vector<nova::ToneEvent> ev = stream_events(video, fs, 2000);
    check(ev.empty(),
          "two bursts: no start tone stapled out of two unrelated bursts "
          "— min_hot_frac holds in the streaming path too");
}

}  // namespace

int main(int argc, char** argv) {
    std::printf("live_tones: the streaming detector == detect_tones, "
                "as the signal arrives\n");

    for (int i = 1; i < argc; i++) run_fixture(argv[i]);

    run_generated(576);
    run_generated(288);
    run_generated_faded();
    run_two_bursts();

    // The whole reason the streaming detector exists: it decides during
    // the tone, not after it. If this is ever zero the detector has
    // silently become a batch detector with extra steps.
    std::printf("\n%d event(s) compared, %d of them committed early, "
                "%.2f s of total lead\n",
                events_seen, events_early, total_lead);
    check(events_seen > 0, "the comparison saw some events at all");
    check(events_early > 0,
          "the streaming detector commits BEFORE the run ends — the reason "
          "it exists");

    if (failures == 0)
        std::printf("live_tones: all checks passed\n");
    else
        std::printf("live_tones: %d check(s) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
