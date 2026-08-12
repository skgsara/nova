// test_fixture.cpp — fixture screamers: real off-air recordings.
//
// Fixture doctrine (SOP P4): each fixture covers something the others
// cannot. Current set:
//
//   test-chart-jmh-kiwisdr-image-60s.wav — PRIMARY. JMH Tokyo test chart
//     via KiwiSDR 13986.6 kHz (2026-08-12), 140..200 s: pure image content
//     (the chart's header block), no control signal anywhere in it, no
//     echo, no dropouts. Honest-lock reference: 117 of 120 lines, max step
//     0.16 px. Cut in session 7 because the fixture below, which had held
//     this role since session 3, turned out not to be pure image at all.
//   test-chart-jmh-kiwisdr-60s.wav — the PHASING BOUNDARY case, 80..140 s.
//     Its header claimed "pure image content" from session 3 until session
//     7 measured it: the parent recording's phasing interval runs
//     72.5-102.5 s, so 45 of this fixture's 120 lines ARE phasing and the
//     rest are the chart's blank top margin. It never was the pure-image
//     reference it was documented as. It is kept, and kept honest, because
//     it covers something no other fixture does: a real phasing->image
//     transition inside one fixture, which is what segmentation has to
//     find. 45 lines dropped from the head, 75 drawn.
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
//   vmw-phasing-image-160s.wav — VMW 2230Z, 55..215 s: start tone, a full
//     30 s phasing interval, and 245 lines of chart after it. The screamer
//     for the phasing line-start anchor [WMO §5.2.3.4], and it asserts the
//     PICTURE, not a number: with the image-derived anchor this recording
//     decoded rotated by 520 px of 1810, the chart's blank right margin
//     wrapped around to the left edge (session 7). The check below —
//     picture content begins one dead sector into the line — reads 4.97%
//     with the phasing anchor and 0.00% without it.
//   xsg-phasing-image-100s.wav — XSG (Shanghai) ASPN, 60..160 s: a PULSE
//     station whose phasing interval is SYMMETRIC 50/50 [WMO §5.2.3.1] —
//     the only one in the library, and until session 8 no fixture covered
//     that waveform's anchor at all. Both anchors exist on it, so it is one
//     of the two two-anchor agreement screamers (--expect-anchor-delta).
//   kyodo-news-jsc2-steps-120s.wav — JSC2, 200..320 s: the NON-LINEAR
//     TIMEBASE case, and the only fixture that isolates the image-domain
//     half of that test. Pure image (JSC2's phasing runs 8-38 s, this cut
//     starts at 200), so the phasing statistic is unavailable by
//     construction and the verdict has to come from the tracked sync
//     residual alone. Session 8 measured this recording's steps by hand —
//     ~21 samples every few lines, still present in the 44.1 kHz original
//     through a separate demodulator — and session 9 found the same fault
//     in all six JSC recordings. 239 lines, 100% locked, +323 ppm: the
//     clock figure is the clock PLUS this window's insertion rate, which
//     is exactly what the flag exists to say (the whole file reads +167).
//
// Bounds are set between measured-known-good and clearly-broken values.
// Usage: nova-test-fixture <path> <lpm> <min_lines> <max_lines>
//                        <clock_lo_ppm> <clock_hi_ppm> <min_locked_frac>
//                        [--expect-white-only] [--expect-phasing-anchor]
//                        [--expect-anchor-delta <lo_smp> <hi_smp>]
//                        [--expect-timebase linear|steps]
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

    if (argc < 8) {
        std::fprintf(stderr,
                     "usage: nova-test-fixture <path> <lpm> <min_lines>"
                     " <max_lines> <clock_lo> <clock_hi> <min_locked_frac>"
                     " [--expect-white-only] [--expect-phasing-anchor]"
                     " [--expect-anchor-delta <lo> <hi>]"
                     " [--expect-timebase linear|steps]\n"
                     "       nova-test-fixture --expect-reject <path>\n");
        return 2;
    }
    bool want_white_only = false, want_phasing_anchor = false;
    bool want_delta = false;
    double delta_lo = 0.0, delta_hi = 0.0;
    const char* want_timebase = nullptr;
    for (int i = 8; i < argc; i++) {
        if (!std::strcmp(argv[i], "--expect-white-only"))
            want_white_only = true;
        else if (!std::strcmp(argv[i], "--expect-phasing-anchor"))
            want_white_only = want_phasing_anchor = true;
        else if (!std::strcmp(argv[i], "--expect-anchor-delta") &&
                 i + 2 < argc) {
            want_delta = true;
            delta_lo = std::atof(argv[++i]);
            delta_hi = std::atof(argv[++i]);
        } else if (!std::strcmp(argv[i], "--expect-timebase") &&
                   i + 1 < argc) {
            want_timebase = argv[++i];
            if (std::strcmp(want_timebase, "linear") &&
                std::strcmp(want_timebase, "steps")) {
                std::fprintf(stderr,
                             "nova-test-fixture: --expect-timebase takes "
                             "linear|steps\n");
                return 2;
            }
        } else {
            std::fprintf(stderr, "nova-test-fixture: bad arg %s\n", argv[i]);
            return 2;
        }
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

        if (want_timebase) {
            const bool steps = r.timebase == nova::Timebase::kSteps;
            std::printf("  timebase=%s steps=%d rate=%.1f/1000 "
                        "phasing_nonlin=%.1f smp\n",
                        steps ? "STEPS"
                              : (r.timebase == nova::Timebase::kLinear
                                     ? "linear"
                                     : "unknown"),
                        r.timebase_step_lines, r.timebase_step_rate,
                        r.phasing_nonlinearity);
            // A verdict of unknown fails either expectation: "not measured"
            // must never pass for "measured and fine".
            check(steps == !std::strcmp(want_timebase, "steps") &&
                      r.timebase != nova::Timebase::kUnknown,
                  "timebase verdict as expected");
        }

        if (want_phasing_anchor) {
            std::printf("  phasing %.2f-%.2f s  anchor delta %+.1f smp  "
                        "from_phasing=%d\n",
                        r.phasing_t_start, r.phasing_t_end,
                        r.phasing_anchor_delta, r.anchor_from_phasing ? 1 : 0);
            check(r.phasing_found, "phasing interval found");
            check(r.anchor_from_phasing,
                  "line start taken from phasing [WMO §5.2.3.4]");

            // The PICTURE check. The dead sector is the one stretch that is
            // the same on every line [WMO §5.1.3.3] and it sits at the line
            // start, so the across-line white run must break within one
            // dead sector of column 0. This is the assertion that fails on
            // the pre-session-7 anchor: it put the start of the chart's
            // blank right margin at column 0 instead, so the run broke at
            // 33.8% and the picture came out rotated by 520 px.
            const nova::Image& im = r.img;
            int first_content = im.width;
            for (int x = 0; x < im.width; x++) {
                int white = 0;
                for (int y = 0; y < im.height; y++)
                    if (im.px[static_cast<size_t>(y) * im.width + x] > 191)
                        white++;
                if (white < 0.9 * im.height) {
                    first_content = x;
                    break;
                }
            }
            const double pct = 100.0 * first_content / im.width;
            std::printf("  picture content begins at column %d of %d "
                        "(%.2f%% of the line)\n",
                        first_content, im.width, pct);
            check(pct >= 2.0 && pct <= 8.0,
                  "picture begins one dead sector into the line");
        }

        // The two-anchor agreement, on a PULSE station (session 8). The
        // phasing white leading edge and the image dead sector are the same
        // feature [WMO §5.2.3.4], found by two detectors that share no code:
        // one fits a wedge over 30 s of control signal, the other counts
        // across-line dark consistency over 120 lines of picture. Nothing
        // else in the suite corroborates either of them on a pulse station —
        // the picture check above runs only where the phasing anchor is USED,
        // which is never here.
        //
        // Measured, whole library, on the eight pulse recordings with a
        // linear timebase: -66.1 to -114.3 samples of 4000, and repeats of
        // one transmitter agree to a few samples (JMH -71.1/-74.1/-75.5/
        // -75.7/-78.4 across two receivers and four cuts; XSG -107.2/-111.5/
        // -114.3). The sign is the black porch: the phasing edge marks dead
        // sector ENTRY, the image anchor is the darkest pulse-width window
        // inside the black run, which starts later. Bands below are ~7x the
        // observed scatter, and still tight enough that a slip of one dead
        // sector (180 smp) or one wedge (200 smp) fails, as does the
        // half-line error session 7 found (~2000 smp).
        //
        // NOT usable on JSC2/JSC3: those two recordings carry ~21-sample
        // timebase steps every few lines (session 8), so a phase measured at
        // one epoch and propagated on one fitted period arrives wrong — they
        // read -234.5 and -54.8 while their local, zero-lever-arm porch is
        // normal. See docs/01 §5.
        if (want_delta) {
            const double period_smp = r.line_period_s * w.sample_rate;
            std::printf("  phasing %.2f-%.2f s  anchor delta %+.1f smp "
                        "(%.2f%% of a line)  from_phasing=%d\n",
                        r.phasing_t_start, r.phasing_t_end,
                        r.phasing_anchor_delta,
                        100.0 * r.phasing_anchor_delta / period_smp,
                        r.anchor_from_phasing ? 1 : 0);
            check(r.phasing_found, "phasing interval found");
            check(!r.anchor_from_phasing,
                  "pulse station keeps its tracked anchor");
            check(r.phasing_anchor_delta >= delta_lo &&
                      r.phasing_anchor_delta <= delta_hi,
                  "phasing and image anchors agree within the black porch");
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "fixture test error: %s\n", e.what());
        return 1;
    }
    std::printf(failures ? "%d FAILURE(S)\n" : "fixture test passed\n",
                failures);
    return failures ? 1 : 0;
}
