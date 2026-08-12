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
//   gya-weak-white-120s.wav — GYA (Charleville, AU) 2324Z, 180..300 s:
//     WEAK and white-only at once. Nothing locks, so the measured clock is
//     the only thing keeping the picture straight and its bound is the
//     picture screamer. Pins the folded-block period estimate: the coarse
//     autocorrelation reads -51.6 ppm on this window against the fold's
//     -118.4, and the fold reads -117 +/- 1.5 ppm on every window of the
//     recording, so a regression to coarse-only fails the bound.
//   vmw-white-sector-120s.wav — VMW (Australian BOM) 200..320 s: a
//     WHITE-ONLY dead sector, no sync pulse anywhere (session 4). Pins
//     two things at once: the style is detected, and the decoder does NOT
//     manufacture locks it cannot have — two white-sector per-line
//     templates were built, both scored hundreds of "locks" and both made
//     the picture worse, so zero here is the correct answer and this
//     screamer exists to keep it zero.
//
// Bounds are set between measured-known-good and clearly-broken values.
// Usage: nova-test-fixture <path> <lpm> <min_lines> <max_lines>
//                        <clock_lo_ppm> <clock_hi_ppm> <min_locked_frac>
//                        [--expect-white-only]
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

    if (argc != 8 && argc != 9) {
        std::fprintf(stderr,
                     "usage: nova-test-fixture <path> <lpm> <min_lines>"
                     " <max_lines> <clock_lo> <clock_hi> <min_locked_frac>"
                     " [--expect-white-only]\n"
                     "       nova-test-fixture --expect-reject <path>\n");
        return 2;
    }
    const bool want_white_only =
        argc == 9 && !std::strcmp(argv[8], "--expect-white-only");
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
        const bool white_only =
            r.dead_sector == nova::DeadSector::kWhiteOnly;
        std::printf(
            "fixture: lpm=%d clock=%+.1f ppm lines=%d locked=%d (%.2f) "
            "clamped=%d max_step=%.1f dead=%s(%.2f)\n",
            r.lpm, r.clock_ppm, r.lines, r.locked_lines, locked_frac,
            r.clamped_corrections, r.max_step_px,
            white_only ? "white" : "pulse", r.dead_consistency);

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
        if (want_white_only) {
            check(white_only, "white-only dead sector detected");
            check(!r.per_line_sync, "no per-line sync claimed");
            check(r.locked_lines == 0, "no locks invented on a white sector");
        } else {
            check(!white_only, "black sync pulse detected");
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "fixture test error: %s\n", e.what());
        return 1;
    }
    std::printf(failures ? "%d FAILURE(S)\n" : "fixture test passed\n",
                failures);
    return failures ? 1 : 0;
}
