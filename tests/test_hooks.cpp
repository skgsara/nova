// test_hooks.cpp — M4 seam screamers: the core's log, progress and
// cancellation surface (core/hooks.hpp), and the structured error kinds
// that replaced three indistinguishable std::runtime_errors.
//
// Claims defended (docs/03 §"Small core changes", ROADMAP M4):
//   - decode_fax failures are DecodeError with a machine-readable kind;
//   - a null sink costs nothing and changes nothing (every other suite
//     runs with one, so that half is covered there);
//   - the log callback receives the same "dbg:" stream the NOVA_DEBUG
//     variables used to gate;
//   - progress reports every stage in pipeline order, fractions in [0,1];
//   - a cancel request stops the decode promptly, from any stage —
//     including inside detect_tones — as DecodeError{kCancelled}, never
//     as a partial image.
#include "../core/demod.hpp"
#include "../core/fax.hpp"
#include "../core/gen.hpp"
#include "../core/tones.hpp"
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int failures = 0;

void check(bool ok, const char* what) {
    std::printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}

// A short clean signal: enough lines that every stage has real work.
std::vector<float> make_video() {
    nova::GenOptions g;
    const int lines = 120;
    nova::Image content = nova::gen_test_pattern(1810, lines);
    std::vector<float> sig = nova::gen_fax_signal(content, lines, g);
    return nova::fm_demod(sig, g.fs, 1900.0, g.deviation);
}

template <typename F>
nova::DecodeErrorKind thrown_kind(F&& f) {
    try {
        f();
    } catch (const nova::DecodeError& e) {
        return e.kind();
    } catch (...) {
        return static_cast<nova::DecodeErrorKind>(-2);  // wrong type
    }
    return static_cast<nova::DecodeErrorKind>(-1);  // nothing thrown
}

}  // namespace

int main() {
    using nova::DecodeErrorKind;
    std::printf("hooks: structured errors, log, progress, cancellation\n");

    // [1] Error kinds. Each failure mode names itself; a caller no longer
    // parses the message string to tell "empty" from "no signal".
    check(thrown_kind([] { nova::decode_fax({}, 8000, {}); }) ==
              DecodeErrorKind::kEmptyInput,
          "[1a] empty input -> kEmptyInput");
    check(thrown_kind([] {
              nova::decode_fax(std::vector<float>(400, 0.5f), 8000, {});
          }) == DecodeErrorKind::kTooShort,
          "[1b] 400 samples -> kTooShort");
    check(thrown_kind([] {
              nova::decode_fax(std::vector<float>(8000 * 30, 0.5f), 8000, {});
          }) == DecodeErrorKind::kNoSignal,
          "[1c] 30 s of flat video -> kNoSignal");
    // ...and the old catch still works: DecodeError IS-A runtime_error.
    bool caught_as_runtime_error = false;
    try {
        nova::decode_fax({}, 8000, {});
    } catch (const std::runtime_error&) {
        caught_as_runtime_error = true;
    }
    check(caught_as_runtime_error,
          "[1d] DecodeError catches as std::runtime_error");

    const std::vector<float> video = make_video();

    // [2] The log callback receives the debug stream; a decode with a
    // sink decodes exactly as one without.
    {
        std::vector<nova::LogTopic> topics;
        size_t dbg_lines = 0;
        nova::DecodeOptions opt;
        opt.hooks.log = [&](nova::LogTopic t, const std::string& line) {
            topics.push_back(t);
            if (line.rfind("dbg:", 0) == 0) dbg_lines++;
        };
        const nova::DecodeResult with = nova::decode_fax(video, 8000, opt);
        const nova::DecodeResult without = nova::decode_fax(video, 8000, {});
        check(!topics.empty() && dbg_lines > 0,
              "[2a] log sink received dbg: lines");
        check(with.lines == without.lines &&
                  with.locked_lines == without.locked_lines &&
                  with.clock_ppm == without.clock_ppm &&
                  with.img.px == without.img.px,
              "[2b] sink changes nothing: image and metrics identical");
    }

    // [3] Progress: every stage, in pipeline order, fractions in [0,1].
    {
        const char* want[] = {"onset",   "dead-sector", "phasing",
                              "sync-track", "period-fit", "segmentation",
                              "timebase", "change-points", "assembly"};
        std::vector<std::string> entered;
        bool range_ok = true;
        nova::DecodeOptions opt;
        opt.hooks.progress = [&](const char* stage, double frac) {
            // A stage may report 0.0 more than once (driver entry, then
            // its loop's first iteration); count each stage once.
            if (frac == 0.0 &&
                (entered.empty() || entered.back() != stage))
                entered.push_back(stage);
            if (frac < 0.0 || frac > 1.0) range_ok = false;
        };
        nova::decode_fax(video, 8000, opt);
        check(entered.size() == sizeof want / sizeof want[0],
              "[3a] all nine stages reported");
        bool order_ok = entered.size() == 9;
        for (size_t i = 0; order_ok && i < entered.size(); i++)
            order_ok = entered[i] == want[i];
        check(order_ok, "[3b] stages reported in pipeline order");
        check(range_ok, "[3c] all fractions within [0,1]");
    }

    // [4] Cancellation. Immediate, mid-decode, and inside detect_tones:
    // every path ends in kCancelled, never in a partial image.
    {
        nova::DecodeOptions opt;
        opt.hooks.cancel = [] { return true; };
        check(thrown_kind([&] { nova::decode_fax(video, 8000, opt); }) ==
                  DecodeErrorKind::kCancelled,
              "[4a] cancel at stage boundary -> kCancelled");
    }
    {
        int calls = 0;
        nova::DecodeOptions opt;
        opt.hooks.cancel = [&] { return ++calls > 50; };
        check(thrown_kind([&] { nova::decode_fax(video, 8000, opt); }) ==
                  DecodeErrorKind::kCancelled,
              "[4b] cancel mid-decode -> kCancelled");
        check(calls < 10000,
              "[4c] cancel is checked often enough to stop promptly");
    }
    {
        check(thrown_kind([&] {
                  nova::DecodeHooks h;
                  h.cancel = [] { return true; };
                  nova::detect_tones(video, 8000, nova::ToneOptions(), h);
              }) == DecodeErrorKind::kCancelled,
              "[4d] cancel inside detect_tones -> kCancelled");
    }

    std::printf(failures ? "hooks: %d FAILURE(S)\n" : "hooks: all passed\n",
                failures);
    return failures ? 1 : 0;
}
