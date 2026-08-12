// test_fixture.cpp — fixture screamers: real off-air recordings.
//
// Fixture doctrine (SOP P4): each fixture covers something the others
// cannot. Current set:
//
//   test-chart-jmh-kiwisdr-60s.wav — PRIMARY. JMH Tokyo test chart via
//     KiwiSDR 13986.6 kHz (2026-08-12), 80..140 s of the recording:
//     pure image content, no echo, no dropouts. Honest-lock reference.
//   test-chart-jmh-60s.wav — first 60 s of "test chart.m4a": the ONLY
//     fixture with the 300 Hz start tone + full phasing, and a long-path
//     ionospheric echo (~144 ms, found during M0 bring-up). The LPE case.
//     locked_frac is lower: phasing/start-tone lines do not match the
//     picture-line sync template (by design, WMO §5.2.3).
//   himawari-jmh-warp-120s.wav — Himawari full-disk via KiwiSDR, 350..470 s:
//     photo content + a KiwiSDR stream time-skip at 410.5 s (session 3
//     measurement: sync phase jumps ~164 ms = ~595 px). The tracker locks
//     up to the warp, then coasts (wide re-acquisition is registered M2
//     work — this fixture is its screamer-in-waiting).
//   stall-fill-15s.wav — first 15 s of the KiwiSDR test-chart recording:
//     no line structure at all (stream stall-fill / leader tone). The
//     decoder must REFUSE it (session 3: pre-gate this decoded as a
//     confident +96735 ppm garbage image).
//   kyodo-news-jsc1-60lpm-120s.wav — JSC1 (Kyodo News newspaper fax),
//     60..180 s: the library's only 60 lpm signal (session 3 batch
//     survey). 99% honest locks. The 60 lpm screamer.
//
// Bounds are set between measured-known-good and clearly-broken values.
// Usage: nova-test-fixture <path> <lpm> <min_lines> <max_lines>
//                        <clock_lo_ppm> <clock_hi_ppm> <min_locked_frac>
//        nova-test-fixture --expect-reject <path>
#include "../core/demod.hpp"
#include "../core/fax.hpp"
#include "../core/wav.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char** argv) {
    int failures = 0;

    if (argc == 3 && !std::strcmp(argv[1], "--expect-reject")) {
        try {
            nova::Wav w = nova::read_wav(argv[2]);
            std::vector<float> video =
                nova::fm_demod(w.samples, w.sample_rate, 1900.0, 400.0);
            nova::DecodeOptions opt;
            nova::DecodeResult r = nova::decode_fax(video, w.sample_rate, opt);
            std::printf("  FAIL fill decoded as lpm=%d clock=%+.1f ppm\n",
                        r.lpm, r.clock_ppm);
            return 1;
        } catch (const std::exception& e) {
            std::printf("  PASS rejected: %s\n", e.what());
            return 0;
        }
    }

    if (argc != 8) {
        std::fprintf(stderr,
                     "usage: nova-test-fixture <path> <lpm> <min_lines>"
                     " <max_lines> <clock_lo> <clock_hi> <min_locked_frac>\n"
                     "       nova-test-fixture --expect-reject <path>\n");
        return 2;
    }
    const char* path = argv[1];
    const int want_lpm = std::atoi(argv[2]);
    const int min_lines = std::atoi(argv[3]);
    const int max_lines = std::atoi(argv[4]);
    const double clock_lo = std::atof(argv[5]);
    const double clock_hi = std::atof(argv[6]);
    const double min_locked = std::atof(argv[7]);

    try {
        nova::Wav w = nova::read_wav(path);
        std::vector<float> video = nova::fm_demod(w.samples, w.sample_rate,
                                                  1900.0, 400.0);
        nova::DecodeOptions opt;
        nova::DecodeResult r = nova::decode_fax(video, w.sample_rate, opt);
        const double locked_frac =
            r.lines > 0 ? static_cast<double>(r.locked_lines) / r.lines : 0.0;
        std::printf(
            "fixture: lpm=%d clock=%+.1f ppm lines=%d locked=%d (%.2f) "
            "clamped=%d max_step=%.1f\n",
            r.lpm, r.clock_ppm, r.lines, r.locked_lines, locked_frac,
            r.clamped_corrections, r.max_step_px);

        auto check = [&](bool ok, const char* what) {
            std::printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
            if (!ok) failures++;
        };
        check(r.lpm == want_lpm, "expected line rate detected");
        check(r.lines >= min_lines, "decodes enough lines");
        check(r.lines <= max_lines, "not inventing lines");
        check(r.clock_ppm > clock_lo && r.clock_ppm < clock_hi,
              "clock in measured real-recording range");
        check(locked_frac >= min_locked,
              "honest sync-template locks above bound");
        check(r.max_step_px < 100, "no wild line jumps");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "fixture test error: %s\n", e.what());
        return 1;
    }
    std::printf(failures ? "%d FAILURE(S)\n" : "fixture test passed\n",
                failures);
    return failures ? 1 : 0;
}
