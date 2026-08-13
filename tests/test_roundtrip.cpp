// test_roundtrip.cpp — M0 screamer tests.
//
// Claims defended (docs/01, docs/02):
//   [WMO §5.3.1.2] 1500/1900/2300 Hz FM subcarrier decodes to gray
//   [WMO §5.1.3.3] dead-sector edge is a usable per-line sync anchor
//   [ISO §4.2.6]   clock error is measured and corrected (no slant)
//   [WMO §5.1.5]   60/90/120 lpm all decode; rate auto-detected
//
// Every assertion compares against a MEASURED bound, set between
// known-bad (unlocked decode at +100 ppm) and known-good (locked).
#include "../core/demod.hpp"
#include "../core/fax.hpp"
#include "../core/gen.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <random>

namespace {
int failures = 0;

void check(bool ok, const char* what) {
    std::printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}

// x-position of the left edge of the black vertical bar, per line.
// Search spans most of the line (past the pulse/gap, before the porch):
// an unlocked decode's frame sits at an arbitrary constant offset, and
// the bar must still be found. The bar is the first strong black edge
// in this span (the gradient strip starts further right).
double edge_x(const nova::Image& img, int y) {
    const int x0 = img.width / 16;
    const int x1 = 9 * img.width / 10;
    for (int x = x0; x < x1; x++)
        if (img.px[static_cast<size_t>(y) * img.width + x] < 100)
            return x;
    return -1;
}

// Crop `rows` rows starting at row `from` (the image region of a decode:
// 10 start-tone lines + 30 phasing lines = 40 in generated signals).
nova::Image crop_rows(const nova::Image& img, int from, int rows) {
    nova::Image c;
    c.width = img.width;
    c.height = rows;
    c.px.assign(img.px.begin() + static_cast<size_t>(from) * img.width,
                img.px.begin() +
                    static_cast<size_t>(from + rows) * img.width);
    return c;
}

// stddev of the bar edge position across the middle lines = slant/jitter.
// Rows that are mostly black (the pattern's horizontal reference bars)
// have no locatable edge and are skipped.
double edge_stdev(const nova::Image& img) {
    std::vector<double> xs;
    for (int y = img.height / 4; y < 3 * img.height / 4; y++) {
        double row_mean = 0;
        for (int x = 0; x < img.width; x++)
            row_mean += img.px[static_cast<size_t>(y) * img.width + x];
        row_mean /= img.width;
        if (row_mean < 100) continue;  // horizontal-bar row
        const double x = edge_x(img, y);
        if (x >= 0) xs.push_back(x);
    }
    if (xs.size() < 10) return 1e9;
    const double mean =
        std::accumulate(xs.begin(), xs.end(), 0.0) / xs.size();
    double acc = 0;
    for (double x : xs) acc += (x - mean) * (x - mean);
    return std::sqrt(acc / (xs.size() - 1));
}

double mean_abs_diff(const nova::Image& a, const nova::Image& b) {
    const int h = std::min(a.height, b.height);
    const int w = std::min(a.width, b.width);
    double acc = 0;
    long n = 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            acc += std::fabs(
                double(a.px[static_cast<size_t>(y) * a.width + x]) -
                double(b.px[static_cast<size_t>(y) * b.width + x]));
            n++;
        }
    return acc / n;
}

// Expected image in the full-line frame, mirroring gen_fax_signal's
// measured line layout: sync pulse (black) 1.5%, white gap to 3.6%,
// test pattern to 98.4%, black porch to end of line.
nova::Image make_full_ref(int width, int rows) {
    nova::Image pat = nova::gen_test_pattern(width, rows);
    nova::Image ref;
    ref.width = width;
    ref.height = rows;
    ref.px.resize(static_cast<size_t>(width) * rows);
    const int pulse = static_cast<int>(0.015 * width);
    const int pic0 = static_cast<int>(0.036 * width);
    const int pic1 = static_cast<int>(0.984 * width);
    for (int y = 0; y < rows; y++)
        for (int x = 0; x < width; x++) {
            uint8_t v = 0;  // pulse and porch are black
            if (x >= pulse && x < pic0)
                v = 255;  // white gap
            else if (x >= pic0 && x < pic1)
                v = pat.px[static_cast<size_t>(y) * width +
                           (x - pic0) * width / (pic1 - pic0)];
            ref.px[static_cast<size_t>(y) * width + x] = v;
        }
    return ref;
}

// The test pattern's straightness bar is solid black, present on every
// line at the same position, and followed by lighter content — which is
// precisely the shape of the OPTIONAL sync pulse [WMO §5.1.3.3]. The
// decoder anchors on it and reports locks even when the signal carries no
// pulse at all (measured: 629 locks on a white-only generated signal). A
// real white-only chart does not carry such a feature (session 4 library
// measurement: pulse-shape consistency 0.14-0.34 for white-only stations
// against 0.48-0.94 for pulse stations), so for the white-only group the
// bar is greyed: still findable by edge_x (< 100), no longer "dark" to the
// anchor (kDarkLevel = 0.25 of full scale = 64).
// (The standard pattern has two such features: the black straightness bar,
// and the gradient strip, which starts at pure black at a fixed x on every
// line. Both anchored the decoder — 629 "locks" on a pulse-free signal —
// so this pattern carries neither.)
nova::Image white_only_pattern(int width, int rows) {
    nova::Image c;
    c.width = width;
    c.height = rows;
    c.px.assign(static_cast<size_t>(width) * rows, 200);
    const int x0 = width / 6, x1 = width / 6 + width / 36;
    for (int y = 0; y < rows; y++) {
        for (int x = x0; x < x1; x++)
            c.px[static_cast<size_t>(y) * width + x] = 90;  // grey bar
        if (y % 50 < 2)  // horizontal line-count bars: not line-consistent
            for (int x = 0; x < width; x++)
                c.px[static_cast<size_t>(y) * width + x] = 0;
    }
    return c;
}

nova::DecodeResult run(const nova::GenOptions& g, int lines,
                       nova::DecodeOptions d,
                       const nova::Image* content_override = nullptr) {
    nova::Image content =
        content_override ? *content_override
                         : nova::gen_test_pattern(
                               (g.ioc == 288) ? 905 : 1810, lines);
    std::vector<float> sig = nova::gen_fax_signal(content, lines, g);
    std::vector<float> video = nova::fm_demod(sig, g.fs, 1900.0, g.deviation);
    // These tests decode the WHOLE generated recording, control lines
    // included: every crop offset and line-count bound below is written in
    // that frame (`crop_rows(img, 40, ...)` = 10 start-tone lines + 30
    // phasing lines). Segmentation is measured on its own, in [9], where a
    // wrong boundary is the thing under test rather than a silent shift of
    // everything else.
    d.segment = false;
    return nova::decode_fax(video, g.fs, d);
}
}  // namespace

int main() {
    constexpr int kLines = 200;

    std::printf("[1] clean 120/576 round-trip\n");
    {
        nova::GenOptions g;
        nova::DecodeOptions d;  // lpm auto
        nova::DecodeResult r = run(g, kLines, d);
        check(r.lpm == 120, "auto-detected 120 lpm");
        check(std::fabs(r.clock_ppm) < 15, "clock ppm near zero");
        // 200 image lines + 30 phasing + tone regions decoded as lines
        check(r.lines >= 225, "line count plausible");
        // compare a crop inside the image region (skip phasing rows)
        nova::Image ref = make_full_ref(1810, kLines);
        nova::Image crop = crop_rows(r.img, 40, kLines);
        check(mean_abs_diff(crop, ref) < 20, "image content matches (MAD<20)");
        check(edge_stdev(crop) < 1.5, "vertical bar straight (stdev<1.5px)");
    }

    std::printf("[2] +100 ppm clock error: autolock on vs off\n");
    {
        nova::GenOptions g;
        g.ppm = 100;
        nova::DecodeOptions d;
        nova::DecodeResult locked = run(g, kLines, d);
        d.autolock = false;
        nova::DecodeOptions d2 = d;
        nova::DecodeResult plain = run(g, kLines, d2);
        check(std::fabs(locked.clock_ppm - 100) < 15, "clock ppm measured");
        nova::Image lc = crop_rows(locked.img, 40, kLines);
        nova::Image pc = crop_rows(plain.img, 40, kLines);
        const double s_locked = edge_stdev(lc);
        const double s_plain = edge_stdev(pc);
        std::printf("  stdev locked=%.2f px, unlocked=%.2f px\n", s_locked,
                    s_plain);
        check(s_locked < 2.0, "locked decode stays straight");
        check(s_plain > 5.0, "unlocked decode visibly slants (screamer)");
    }

    std::printf("[3] noise RMS 0.1 (SNR ~14 dB): still locks\n");
    {
        nova::GenOptions g;
        g.noise = 0.1;
        nova::DecodeOptions d;
        nova::DecodeResult r = run(g, kLines, d);
        check(edge_stdev(crop_rows(r.img, 40, kLines)) < 3.0,
              "noisy decode stays straight");
    }

    std::printf("[4] 60 lpm / IOC 288 auto-detect + width\n");
    {
        nova::GenOptions g;
        g.lpm = 60;
        g.ioc = 288;
        nova::DecodeOptions d;
        d.ioc = 288;
        nova::DecodeResult r = run(g, 100, d);
        check(r.lpm == 60, "auto-detected 60 lpm");
        check(r.img.width == 905, "IOC 288 width");
    }

    std::printf("[5] 90 lpm round-trip\n");
    {
        nova::GenOptions g;
        g.lpm = 90;
        nova::DecodeOptions d;
        nova::DecodeResult r = run(g, 150, d);
        check(r.lpm == 90, "auto-detected 90 lpm");
        check(edge_stdev(crop_rows(r.img, 40, 150)) < 1.5,
              "90 lpm decode straight");
    }

    std::printf("[6] LF deviation +/-150 Hz [ISO §4.2.2]\n");
    {
        nova::GenOptions g;
        g.deviation = 150;
        nova::DecodeOptions d;
        nova::DecodeResult r = run(g, kLines, d);
        check(edge_stdev(crop_rows(r.img, 40, kLines)) < 2.0,
              "150 Hz deviation decodes straight");
    }

    // The station sends no sync pulse [WMO §5.1.3.3 makes it optional], so
    // there is nothing to lock and the picture rides entirely on the
    // measured clock — the case that has no other screamer, because on a
    // real white-only recording (VMW, NMC, GYA) nobody knows the true
    // clock. Here it is generated, so it is known. Before session 5 the
    // period came from a 200 Hz autocorrelation whose lag step is 10 000
    // ppm; the fold measures it as phase drift across the whole recording.
    std::printf("[7] white-only dead sector at +250 ppm: no locks, straight\n");
    {
        nova::GenOptions g;
        g.dead_pulse = false;
        g.ppm = 250;
        nova::DecodeOptions d;
        const nova::Image content = white_only_pattern(1810, 600);
        nova::DecodeResult r = run(g, 600, d, &content);
        std::printf("  clock=%+.1f ppm locked=%d per_line_sync=%d\n",
                    r.clock_ppm, r.locked_lines, r.per_line_sync ? 1 : 0);
        check(!r.per_line_sync, "white-only style detected");
        check(r.locked_lines == 0, "no locks invented without a pulse");
        check(std::fabs(r.clock_ppm - 250) < 10, "clock measured within 10 ppm");
        check(edge_stdev(crop_rows(r.img, 40, 600)) < 3.0,
              "white-only decode straight on the measured clock alone");
    }

    // Precision, not just correctness: the fold's whole justification is
    // that accuracy comes from the BASELINE, so a long recording must be
    // measured better than a short one, not merely acceptably.
    std::printf("[8] clock precision on a long recording\n");
    {
        nova::GenOptions g;
        g.ppm = -137;
        nova::DecodeOptions d;
        nova::DecodeResult r = run(g, 1200, d);
        std::printf("  clock=%+.2f ppm (true -137)\n", r.clock_ppm);
        check(std::fabs(r.clock_ppm + 137) < 5, "clock within 5 ppm");
    }

    std::printf("[9] segmentation: draw the picture, not the control "
                "signals [WMO §5.2.3, §5.2.5]\n");
    {
        // The generated recording is 10 start-tone lines + 30 phasing lines
        // + kLines of picture + a stop tone. A correct segmentation emits
        // the picture and nothing else, so the check is not "roughly the
        // right number of rows" but "row 0 of the output IS image line 0":
        // the reference is compared from row 0 with no crop offset, and the
        // test pattern's horizontal bars every 50 lines make a slip of even
        // a few rows blow the MAD bound.
        nova::GenOptions g;
        nova::Image content = nova::gen_test_pattern(1810, kLines);
        std::vector<float> sig = nova::gen_fax_signal(content, kLines, g);
        std::vector<float> video =
            nova::fm_demod(sig, g.fs, 1900.0, g.deviation);
        nova::DecodeOptions d;  // segmentation ON — this is the test
        nova::DecodeResult r = nova::decode_fax(video, g.fs, d);
        std::printf("  lines=%d dropped head=%d tail=%d image=%.2f-%.2f s\n",
                    r.lines, r.lines_dropped_head, r.lines_dropped_tail,
                    r.image_t_start, r.image_t_end);
        check(r.segmented, "segmentation applied");
        check(std::abs(r.lines_dropped_head - 40) <= 2,
              "start tone + phasing dropped from the head (40 lines)");
        check(r.lines_dropped_tail > 0, "stop tone dropped from the tail");
        check(std::abs(r.lines - kLines) <= 3, "the picture lines survive");
        const int rows = std::min(r.lines, kLines) - 2;
        nova::Image ref = make_full_ref(1810, rows);
        nova::Image crop = crop_rows(r.img, 0, rows);
        const double mad = mean_abs_diff(crop, ref);
        std::printf("  MAD from row 0 (no crop offset) = %.1f\n", mad);
        check(mad < 20, "row 0 of the output is image line 0");
    }

    std::printf("[10] timebase steps: detected, and not confused with a "
                "clock error\n");
    {
        // Ground truth, which no recording can give: the signal is generated
        // linear, then samples are inserted into it at a known rate. JSC2's
        // measured signature (session 8) is the model — ~21 samples every
        // ~11 lines at 8 kHz.
        constexpr int kIns = 21, kEvery = 11;
        nova::GenOptions g;
        nova::Image content = nova::gen_test_pattern(1810, kLines);
        const std::vector<float> clean = nova::gen_fax_signal(content, kLines, g);
        const size_t line = static_cast<size_t>(g.fs * 60.0 / g.lpm);
        std::vector<float> stepped;
        stepped.reserve(clean.size() + clean.size() / line / kEvery * kIns);
        for (size_t i = 0; i < clean.size(); i++) {
            stepped.push_back(clean[i]);
            // Mid-line, where the picture is: an insertion at the sync edge
            // would be a kinder test than the real fault.
            if (i % (line * kEvery) == line / 2)
                for (int k = 0; k < kIns; k++) stepped.push_back(clean[i]);
        }
        nova::DecodeOptions d;
        d.segment = false;
        auto run_video = [&](const std::vector<float>& s) {
            std::vector<float> v = nova::fm_demod(s, g.fs, 1900.0, g.deviation);
            return nova::decode_fax(v, g.fs, d);
        };
        const nova::DecodeResult lin = run_video(clean);
        const nova::DecodeResult stp = run_video(stepped);
        std::printf("  linear: %s rate=%.1f/1000 nonlin=%.1f smp\n",
                    lin.timebase == nova::Timebase::kSteps ? "STEPS" : "linear",
                    lin.timebase_step_rate, lin.phasing_nonlinearity);
        std::printf("  stepped: %s rate=%.1f/1000 nonlin=%.1f smp\n",
                    stp.timebase == nova::Timebase::kSteps ? "STEPS" : "linear",
                    stp.timebase_step_rate, stp.phasing_nonlinearity);
        check(lin.timebase == nova::Timebase::kLinear,
              "a linear timebase is called linear");
        check(stp.timebase == nova::Timebase::kSteps,
              "inserted samples are detected (screamer)");
        // The reported rate is a FLOOR on the insertion rate, not an
        // estimate of it: this signal inserts once every 11 lines (90.9 per
        // 1000) and the detector reports 36.9, because the ±8-line median
        // that makes a step visible at all is wider than the gap between
        // steps, so neighbouring steps merge into one ramp. Measured the
        // same way, JSC2 reports 74 against session 8's hand count of ~123.
        // Pinned as a band so that both an over-count and a collapse to
        // near-zero fail.
        check(stp.timebase_step_rate > 1000.0 / kEvery * 0.3 &&
                  stp.timebase_step_rate < 1000.0 / kEvery * 1.2,
              "step rate is a floor on the insertion rate, same order");
        // The picture survives but is not untouched: the local median that
        // tracks the steps lags them by a few lines, so every insertion
        // costs a few lines of misalignment. Both halves are pinned — a
        // decoder that started throwing the picture away on these files
        // would fail the upper bound, and one that silently stopped
        // tracking would fail the lower.
        const double sd_lin = edge_stdev(crop_rows(lin.img, 40, kLines));
        const double sd_stp = edge_stdev(crop_rows(stp.img, 40, kLines));
        std::printf("  bar stdev: linear=%.2f px stepped=%.2f px\n", sd_lin,
                    sd_stp);
        check(sd_stp < 5.0, "the picture still comes out (wobble < 5 px)");
        check(sd_stp > 1.0,
              "...but visibly wobbles, which is why the flag exists");

        // The false positive that matters. A clock error IS a linear
        // timebase, and it moves the phasing edge by 1 sample per line at
        // 250 ppm — 30 samples across the interval, three times the
        // non-linearity limit. Measuring the raw spread instead of the
        // residual about the fitted line reads that as steps.
        nova::GenOptions gc;
        gc.ppm = 250;
        nova::DecodeResult fast = run(gc, kLines, d);
        std::printf("  +250 ppm: %s rate=%.1f/1000 nonlin=%.1f smp "
                    "(spread would be ~%.0f)\n",
                    fast.timebase == nova::Timebase::kSteps ? "STEPS"
                                                            : "linear",
                    fast.timebase_step_rate, fast.phasing_nonlinearity,
                    fast.phasing_spread);
        check(fast.timebase == nova::Timebase::kLinear,
              "a +250 ppm clock is not called a stepping timebase");

        // The case the risk register warns about, and the only one where
        // the phasing statistic is the sole witness: a WHITE-ONLY station
        // (VMW, NMC, GYA transmit this way) whose capture chain steps.
        // There is no per-line sync to track, so the picture rides on the
        // measured clock and a step displaces the paper permanently — and
        // until this session nothing in the decoder would have said so.
        // No library recording is both white-only and stepping, so the
        // combination only exists here.
        nova::GenOptions gw;
        gw.dead_pulse = false;
        const std::vector<float> w_clean =
            nova::gen_fax_signal(content, kLines, gw);
        std::vector<float> w_stepped;
        for (size_t i = 0; i < w_clean.size(); i++) {
            w_stepped.push_back(w_clean[i]);
            if (i % (line * kEvery) == line / 2)
                for (int k = 0; k < kIns; k++) w_stepped.push_back(w_clean[i]);
        }
        const nova::DecodeResult wl = run_video(w_clean);
        const nova::DecodeResult ws = run_video(w_stepped);
        std::printf("  white-only: linear=%s stepped=%s (locks %d/%d, "
                    "nonlin %.1f smp, bar stdev %.1f px)\n",
                    wl.timebase == nova::Timebase::kSteps ? "STEPS" : "linear",
                    ws.timebase == nova::Timebase::kSteps ? "STEPS" : "linear",
                    ws.locked_lines, ws.lines, ws.phasing_nonlinearity,
                    edge_stdev(crop_rows(ws.img, 40, kLines)));
        check(ws.locked_lines == 0,
              "white-only: no per-line sync, as designed");
        check(wl.timebase == nova::Timebase::kLinear &&
                  ws.timebase == nova::Timebase::kSteps,
              "white-only: phasing alone convicts a stepping timebase");

        // The false positive the phasing witness is exposed to, found in
        // session 10 the moment GYA 2300Z's faded interval was recovered and
        // handed to it: a NOISY edge is not a MOVED one. The statistic is
        // the spread of the residual about the fitted line, and it was
        // calibrated (session 9) only on intervals whose per-line edge is
        // good to about a sample. GYA 2300Z's is good to ~14, and read 46.2
        // raw — twice JSC2's 25.5, on a recording with no steps in it.
        //
        // What separates them is not the SIZE of the residual but its SHAPE:
        // an inserted sample moves the edge and it stays moved, so the
        // residual is a staircase whose line-to-line differences are near
        // zero; noise is a new draw every line. Same signal as the
        // white-only case above, no insertions, phasing faded instead.
        nova::GenOptions gn;
        gn.dead_pulse = false;
        std::vector<float> noisy = nova::gen_fax_signal(content, kLines, gn);
        const size_t p0 = static_cast<size_t>(5.0 * gn.fs);
        const size_t p1 =
            p0 + static_cast<size_t>(gn.phasing_lines * 0.5 * gn.fs);
        std::mt19937 rng(20260812u);
        std::normal_distribution<float> nd(0.0f, 0.20f);
        for (size_t i = p0; i < std::min(p1, noisy.size()); i++)
            noisy[i] += nd(rng);
        const nova::DecodeResult nz = run_video(noisy);
        std::printf("  faded phasing: %s nonlin=%.1f smp (locks %d)\n",
                    nz.timebase == nova::Timebase::kSteps    ? "STEPS"
                    : nz.timebase == nova::Timebase::kLinear ? "linear"
                                                             : "unknown",
                    nz.phasing_nonlinearity, nz.locked_lines);
        check(nz.phasing_found, "the faded interval is found at all");
        check(nz.timebase != nova::Timebase::kSteps,
              "a noisy phasing edge is not called a stepping timebase");

        // ...and the third way an edge bends: ONE skip. Session 9 settled
        // that a single time-skip is not a stepping timebase and pinned it
        // in the image domain (`fixture_timebase_linear`); the phasing
        // domain measured a spread and so could not tell one jump from
        // fifty. JMH KiwiSDR Himawari is the real case — a ~95-sample jump
        // in the middle of an otherwise textbook phasing interval, which
        // read 96.1 samples off straight and out-scored every genuinely
        // stepping recording in the library. Here it is with ground truth:
        // the same white-only signal, one insertion, inside the phasing.
        std::vector<float> once;
        const size_t at = static_cast<size_t>(12.5 * gn.fs);  // mid-phasing
        for (size_t i = 0; i < w_clean.size(); i++) {
            once.push_back(w_clean[i]);
            if (i == at)
                for (int k = 0; k < kIns; k++) once.push_back(w_clean[i]);
        }
        const nova::DecodeResult one = run_video(once);
        std::printf("  one skip: %s nonlin=%.1f smp steps=%d\n",
                    one.timebase == nova::Timebase::kSteps    ? "STEPS"
                    : one.timebase == nova::Timebase::kLinear ? "linear"
                                                              : "unknown",
                    one.phasing_nonlinearity, one.phasing_steps);
        check(one.phasing_found, "the interval around the skip is found");
        check(one.timebase != nova::Timebase::kSteps,
              "one skip in the phasing edge is not a rate");
    }

    std::printf(failures ? "\n%d FAILURE(S)\n" : "\nall tests passed\n",
                failures);
    return failures ? 1 : 0;
}
