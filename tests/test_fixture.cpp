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
//   xsg-fyci-phasing-head-120s.wav — XSG (Shanghai) FYCI, 120..240 s: a
//     phasing interval at 12-42 s into the cut and then the FIRST lines of
//     the picture. Cut session 11 for the one defect that no number the
//     decoder produces can see: the first drawn lines usually do not lock,
//     so the place error reads 0.13 px whether they are drawn in the right
//     place or 88.6 px away, and a single step does not move a 90th
//     percentile. Only the picture shows it, which is why the check here
//     compares the top of the picture against its own body.
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
//                        [--expect-straight-strip <max_px>]
//                        [--expect-straight-porch <max_px>]
//                        [--expect-rigid-rows <max_px>]
//                        [--expect-rows-in-place <max_rows>]
//        nova-test-fixture --expect-reject <path>
#include "../core/demod.hpp"
#include "../core/fax.hpp"
#include "../core/wav.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

// How straight the dead sector's edge is in the DRAWN picture.
//
// The dead sector is a fixed-width band at the start of every line [WMO
// §5.1.3.3], so the column where it ends is a straight vertical line in a
// correctly assembled image — and a crooked one is the first thing an
// operator sees. This walks the finished pixels and shares no code and no
// number with the decoder, which is the point: every other check in this
// suite asks the decoder how it thinks it did. Session 11 exists because
// Sara reviewed the library by eye and found six recordings whose strip
// "zig zags" while every decoder statistic on them read fine.
//
// Roughness, not position, and robust to a seam: the statistic is the 90th
// percentile of the row-to-row MOVE of that edge. A slow rotation or a clock
// walk moves it by a fraction of a pixel per row; one legitimate seam, where
// the recording genuinely lost samples, is a single large move that the 90th
// percentile ignores; a zig-zag moves it on every row, which is the
// complaint. (Measured with deviation-from-a-local-median instead, three
// clean recordings appear to REGRESS in session 11 purely because they
// gained a correct one-line seam — the statistic has to know the difference.)
// Same edge, read row by row. Split out because two different questions are
// asked of it: how much it jitters, and whether the TOP of the picture sits
// on the same page as the body.
std::vector<double> strip_edges(const nova::Image& im) {
    const int span = im.width < 250 ? im.width : 250;
    const int run = 12;    // consecutive light pixels that end the strip
    const int thr = 110;   // of 255; the strip is black, the gap is white
    std::vector<double> e;
    for (int y = 0; y < im.height; y++) {
        int light = 0, edge = -1;
        for (int x = 0; x < span; x++) {
            const uint8_t v = im.px[static_cast<size_t>(y) * im.width + x];
            light = (v > thr) ? light + 1 : 0;
            if (light >= run) {
                edge = x - run + 1;
                break;
            }
        }
        if (edge >= 0) e.push_back(edge);
    }
    return e;
}

double strip_edge_jitter(const nova::Image& im, int* used) {
    const std::vector<double> e = strip_edges(im);
    *used = static_cast<int>(e.size());
    if (e.size() < 40) return -1.0;  // too few rows for a 90th percentile
    std::vector<double> d;
    d.reserve(e.size() - 1);
    for (size_t i = 1; i < e.size(); i++)
        d.push_back(std::fabs(e[i] - e[i - 1]));
    std::sort(d.begin(), d.end());
    return d[static_cast<size_t>(0.90 * (d.size() - 1))];
}

// The same edge, at the OTHER end of the line. The porch at the line's end
// is signal with no picture content behind it, so its left edge stays
// measurable where the dead sector's right edge is crowded by content (on
// HLL 2147Z a coastline sits hard against the gap — which is what polluted
// the pulse template's white window and caused the false locks of session
// 26). The statistic is the MAX row-to-row move, not a percentile: what it
// exists to catch is a jog of two or three rows — a lock that hopped and
// came back — and a 90th percentile is blind to exactly that (measured on
// the fixture: p90 reads 2 px on the picture whose porch jumps 22 px for
// two rows). The last two drawn rows are excluded: a fixture cut
// mid-transmission ends mid-line, and the tail row's porch is ragged in
// every decode (23 px on this fixture, identically before and after the
// session-26 fix).
std::vector<double> porch_edges(const nova::Image& im) {
    const int run = 12, thr = 110;  // same levels as the strip
    std::vector<double> e;
    for (int y = 0; y < im.height - 2; y++) {
        int light = 0, edge = -1;
        for (int x = im.width - 1; x >= 0 && x >= im.width - 250; x--) {
            const uint8_t v = im.px[static_cast<size_t>(y) * im.width + x];
            light = (v > thr) ? light + 1 : 0;
            if (light >= run) {
                edge = x + run;
                break;
            }
        }
        if (edge >= 0) e.push_back(edge);
    }
    return e;
}

double porch_edge_maxmove(const nova::Image& im, int* used) {
    const std::vector<double> e = porch_edges(im);
    *used = static_cast<int>(e.size());
    if (e.size() < 40) return -1.0;
    double mx = 0.0;
    for (size_t i = 1; i < e.size(); i++)
        mx = std::max(mx, std::fabs(e[i] - e[i - 1]));
    return mx;
}

// Is a drawn row RIGID — does it move as one piece?
//
// The dead sector straddles the line boundary [WMO §5.1.3.3], so a drawn
// row carries a black band at both ends, and in a correctly assembled
// picture the two move together: whatever displaces the row displaces all
// of it. When the capture chain inserts samples in the MIDDLE of a line,
// they do not: everything after the insertion point moves and everything
// before it stays. Measured in the session-11 decodes, the two ends of a
// JSC row moved with correlation +0.12 and disagreed by 5-10 px of 1810,
// against 1 px on a recording with a linear timebase.
//
// Statistic: 90th percentile of |move of the left edge − move of the right
// edge|, row to row. A rigid picture reads ~1 px whatever else is wrong
// with it; a stretched one cannot.
double row_rigidity(const nova::Image& im, int* used) {
    const int run = 12, thr = 110;
    std::vector<double> l, r;
    std::vector<int> rows;
    for (int y = 0; y < im.height; y++) {
        int light = 0, le = -1, re = -1;
        const int span = im.width < 250 ? im.width : 250;
        for (int x = 0; x < span; x++) {
            const uint8_t v = im.px[static_cast<size_t>(y) * im.width + x];
            light = (v > thr) ? light + 1 : 0;
            if (light >= run) { le = x - run + 1; break; }
        }
        light = 0;
        for (int x = im.width - 1; x > im.width - 1 - span; x--) {
            const uint8_t v = im.px[static_cast<size_t>(y) * im.width + x];
            light = (v > thr) ? light + 1 : 0;
            if (light >= run) { re = x + run - 1; break; }
        }
        if (le >= 0 && re >= 0) {
            l.push_back(le);
            r.push_back(re);
            rows.push_back(y);
        }
    }
    *used = static_cast<int>(l.size());
    if (l.size() < 40) return -1.0;
    std::vector<double> d;
    for (size_t i = 1; i < l.size(); i++)
        if (rows[i] == rows[i - 1] + 1)
            d.push_back(std::fabs((l[i] - l[i - 1]) - (r[i] - r[i - 1])));
    if (d.size() < 40) return -1.0;
    std::sort(d.begin(), d.end());
    return d[static_cast<size_t>(0.90 * (d.size() - 1))];
}

// How many rows are drawn somewhere else than the picture they belong to.
//
// The dead-sector edge cannot answer this: where a recording drops samples
// the rows around the drop carry no clean edge to measure, so the strip
// test reads the same whether they are placed well or badly (measured: 8
// either way on the fixture below). The picture can. A weather fax moves
// 1/1810 of a page between one line and the next, so a row that matches the
// row above it best at a large horizontal shift is a row in the wrong
// place. Counted over the whole image; a legitimate seam contributes one.
int content_jumps(const nova::Image& im, int limit_px) {
    const int step = 4;
    const int span = 60;  // px either way
    int n = 0;
    for (int y = 1; y < im.height; y++) {
        const uint8_t* above = &im.px[static_cast<size_t>(y - 1) * im.width];
        const uint8_t* here = &im.px[static_cast<size_t>(y) * im.width];
        long best = -1;
        int bestoff = 0;
        for (int off = -span; off <= span; off += 2) {
            long acc = 0;
            for (int x = span; x < im.width - span; x += step)
                acc += std::abs(static_cast<int>(here[x + off]) -
                                static_cast<int>(above[x]));
            if (best < 0 || acc < best) { best = acc; bestoff = off; }
        }
        if (std::abs(bestoff) > limit_px) n++;
    }
    return n;
}

double median_of(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v.empty() ? 0.0 : v[v.size() / 2];
}

// Does the top of the picture sit on the same page as the body?
//
// The first drawn lines are the ones whose smoothing window can still see
// the control signal before them, where the sync template anchors half a
// dead sector away [WMO §5.2.3.1] — and the first lines of a picture often
// do not lock, so nothing in the decoder's own account of itself can catch
// this: the place error is measured on locked lines and reads identically
// either way, and one step does not move a 90th percentile. Only the
// picture shows it. Measured on XSG FYCI with the window left unbounded:
// eight lines at 88.6 px of 1810 from the rest of the chart.
double strip_head_offset(const nova::Image& im) {
    const std::vector<double> e = strip_edges(im);
    if (e.size() < 48) return 0.0;  // nothing to compare a head against
    std::vector<double> head(e.begin(), e.begin() + 8);
    std::vector<double> body(e.begin() + 8, e.begin() + 40);
    return std::fabs(median_of(head) - median_of(body));
}

}  // namespace

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
                     " [--expect-timebase linear|steps]"
                     " [--expect-straight-strip <px>]"
                     " [--expect-straight-porch <px>]\n"
                     "       nova-test-fixture --expect-reject <path>\n");
        return 2;
    }
    bool want_white_only = false, want_phasing_anchor = false;
    bool want_delta = false;
    double delta_lo = 0.0, delta_hi = 0.0;
    const char* want_timebase = nullptr;
    bool want_strip = false;
    double strip_max = 0.0;
    bool want_porch = false;
    double porch_max = 0.0;
    bool want_rigid = false;
    double rigid_max = 0.0;
    bool want_in_place = false;
    int in_place_max = 0;
    int want_phasing_lines = 0;
    bool want_window = false;
    double win_t0 = 0.0, win_t1 = 0.0;
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
                std::strcmp(want_timebase, "steps") &&
                std::strcmp(want_timebase, "noisy")) {
                std::fprintf(stderr,
                             "nova-test-fixture: --expect-timebase takes "
                             "linear|steps|noisy\n");
                return 2;
            }
        } else if (!std::strcmp(argv[i], "--expect-straight-strip") &&
                   i + 1 < argc) {
            want_strip = true;
            strip_max = std::atof(argv[++i]);
        } else if (!std::strcmp(argv[i], "--expect-straight-porch") &&
                   i + 1 < argc) {
            want_porch = true;
            porch_max = std::atof(argv[++i]);
        } else if (!std::strcmp(argv[i], "--expect-rigid-rows") &&
                   i + 1 < argc) {
            want_rigid = true;
            rigid_max = std::atof(argv[++i]);
        } else if (!std::strcmp(argv[i], "--expect-rows-in-place") &&
                   i + 1 < argc) {
            want_in_place = true;
            in_place_max = std::atoi(argv[++i]);
        } else if (!std::strcmp(argv[i], "--expect-phasing-lines") &&
                   i + 1 < argc) {
            want_phasing_lines = std::atoi(argv[++i]);
        } else if (!std::strcmp(argv[i], "--expect-phasing-window") &&
                   i + 2 < argc) {
            want_window = true;
            win_t0 = std::atof(argv[++i]);
            win_t1 = std::atof(argv[++i]);
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
            // must never pass for "measured and fine". Except where being
            // unmeasurable is the claim under test — session 10 added the
            // case where a phasing interval IS found and is too noisy to
            // resolve a step, which is a third answer and not a failure to
            // produce one.
            if (!std::strcmp(want_timebase, "noisy")) {
                std::printf("  phasing witness: nonlin=%.1f noise=%.1f "
                            "steps=%d\n",
                            r.phasing_nonlinearity, r.phasing_roughness,
                            r.phasing_steps);
                check(r.timebase == nova::Timebase::kUnknown &&
                          r.phasing_witness == nova::PhasingWitness::kNoisy,
                      "timebase unmeasurable because the edge is too noisy");
            } else {
                check(steps == !std::strcmp(want_timebase, "steps") &&
                          r.timebase != nova::Timebase::kUnknown,
                      "timebase verdict as expected");
            }
        }

        if (want_strip) {
            int used = 0;
            const double jit = strip_edge_jitter(r.img, &used);
            std::printf("  dead-sector edge: row-to-row p90 %.1f px of %d "
                        "over %d rows; decoder says place rms %.2f px, "
                        "%d seam(s)\n",
                        jit, r.img.width, used, r.place_rms_px, r.seams);
            const double head = strip_head_offset(r.img);
            std::printf("  top of the picture sits %.1f px from its body\n",
                        head);
            check(used >= r.lines / 2,
                  "the strip is found on most rows at all");
            check(jit >= 0.0 && jit <= strip_max,
                  "dead-sector edge straight in the drawn picture");
            // A quarter of a dead sector (4.5% of the line [WMO §5.1.3.3])
            // is 20 px of 1810: below what an operator would call a
            // misaligned top, and eight times under the 88.6 px that a
            // smoothing window reaching into the phasing region produces.
            check(head <= 20.0,
                  "the picture's first lines are on the same page as the "
                  "rest");
            // Two measurements of one thing, sharing no code: the decoder's
            // own place error is computed from sync residuals, this one from
            // pixels. They should agree; if they ever stop, one of them is
            // measuring something else and the picture is the authority.
            check(r.place_rms_px <= strip_max,
                  "...and the decoder's own account of it agrees");
        }

        if (want_porch) {
            int used = 0;
            const double mv = porch_edge_maxmove(r.img, &used);
            std::printf("  porch edge: worst row-to-row move %.1f px over "
                        "%d rows; %d seam(s) followed\n",
                        mv, used, r.seams);
            check(used >= r.lines / 2, "the porch is found on most rows");
            check(mv >= 0.0 && mv <= porch_max,
                  "the porch edge never jogs");
        }

        if (want_in_place) {
            const int out = content_jumps(r.img, 8);
            std::printf("  %d row(s) match the row above best at more than "
                        "8 px away; %d were placed by the picture\n",
                        out, r.picture_placed);
            check(out <= in_place_max,
                  "no band of rows is drawn somewhere else");
        }

        if (want_rigid) {
            int used = 0;
            const double rg = row_rigidity(r.img, &used);
            std::printf("  row rigidity: the two ends of a row disagree by "
                        "%.1f px (p90) over %d rows; %d intra-line break(s)\n",
                        rg, used, r.intra_line_breaks);
            check(rg >= 0.0 && rg <= rigid_max,
                  "a drawn row moves as one piece");
        }

        if (want_window) {
            // WHICH phasing interval, on a recording that carries two. The
            // opening this decoder is entitled to is the FIRST one, and the
            // interval itself has to be phasing rather than a control tone
            // that happens to fit the same template — a 300 Hz start tone
            // is 150 white runs per line at 120 lpm and fits the 5% wedge
            // with a position spread of exactly zero, which is better than
            // any real phasing interval in the library manages.
            std::printf("  phasing %.2f-%.2f s  %d lines  score %.3f\n",
                        r.phasing_t_start, r.phasing_t_end, r.phasing_lines,
                        r.phasing_score);
            check(r.phasing_found, "phasing interval found");
            check(std::fabs(r.phasing_t_start - win_t0) < 1.0 &&
                      std::fabs(r.phasing_t_end - win_t1) < 1.0,
                  "the FIRST opening's phasing, and no tone lines in it");
        }

        if (want_phasing_lines) {
            // A FADED phasing interval, on the one station in the library
            // that has no other source of line phase. Growing runs from
            // consecutive above-threshold lines found nothing here at all
            // (session 10): the interval is real, 40 lines of it, and only
            // 23 of those lines clear the per-line score. The anchor it
            // yields was checked against the drawn picture and against GYA
            // 2324Z, the same station 24 minutes later, whose phasing is
            // unambiguous — the chart's title box lands at the left margin
            // on both, and half a line out with the image anchor.
            std::printf("  phasing %.2f-%.2f s  %d lines  score %.3f  "
                        "from_phasing=%d\n",
                        r.phasing_t_start, r.phasing_t_end, r.phasing_lines,
                        r.phasing_score, r.anchor_from_phasing ? 1 : 0);
            check(r.phasing_found, "faded phasing interval found (screamer)");
            check(r.phasing_lines >= want_phasing_lines,
                  "enough of its faded lines are recovered");
            check(r.anchor_from_phasing,
                  "white-only station phased from it [WMO §5.2.3.4]");
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
