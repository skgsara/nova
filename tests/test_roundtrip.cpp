// test_roundtrip.cpp — M0 screamer tests.
//
// Claims defended (docs/01, docs/02):
//   [WMO §5.3.1.2] 1500/1900/2300 Hz FM subcarrier decodes to gray
//   [WMO §5.1.3.3] dead-sector edge is a usable per-line sync anchor,
//                  across its whole tolerance range
//   [ISO §4.2.6]   clock error is measured and corrected (no slant)
//   [WMO §5.1.5]   60/90/120 lpm all decode; rate auto-detected
//   [ISO §5.4.1]   all six {288,576} x {60,90,120} combinations decode,
//                  rate AND IOC auto-selected
//   [ISO §4.2.2]   input level 0.5..0.005 of full scale decodes identically
//   [WMO §5.4.3]   gray scale is linear (eight-band step chart)
//   [WMO §5.2.2]   a 10 s start tone is segmented to the same picture
//                  boundary as a 5 s one
//
// Every assertion compares against a MEASURED bound, set between
// known-bad (unlocked decode at +100 ppm) and known-good (locked).
#include "../core/demod.hpp"
#include "../core/fax.hpp"
#include "../core/gen.hpp"
#include "../core/resample.hpp"
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

    // [ISO §5.4.1] the full rate/IOC matrix a receiver must accept: IOC 576
    // and 288 crossed with 60/90/120 lpm. Both selectors are left on auto —
    // lpm from the line comb, IOC from the 300/675 Hz start tone
    // [ISO §4.2.3, §4.2.5] — so every leg also pins the auto-selection.
    // Subsumes the old [4] (60 lpm / IOC 288) and [5] (90 lpm) groups.
    std::printf("[4] ISO §5.4.1 matrix: {288,576} x {60,90,120}, all auto\n");
    {
        for (int ioc : {288, 576}) {
            for (int lpm : {60, 90, 120}) {
                nova::GenOptions g;
                g.lpm = lpm;
                g.ioc = ioc;
                nova::DecodeOptions d;  // lpm AND ioc auto: that is the test
                nova::DecodeResult r = run(g, 120, d);
                // Head = 5 s start tone + 30 phasing lines, in lines at
                // this lpm; crop two lines inside it so the tone/phasing
                // boundary row cannot leak into the straightness measure.
                const int head = static_cast<int>(5.0 * lpm / 60) + 30;
                const double sd = edge_stdev(crop_rows(r.img, head + 2, 110));
                std::printf("  IOC %d / %3d lpm: got IOC %d, %d lpm, width "
                            "%d, clock %+.1f ppm, bar stdev %.2f px\n",
                            ioc, lpm, r.ioc, r.lpm, r.img.width, r.clock_ppm,
                            sd);
                char what[96];
                std::snprintf(what, sizeof what, "auto-selected %d lpm", lpm);
                check(r.lpm == lpm, what);
                std::snprintf(what, sizeof what, "auto-selected IOC %d", ioc);
                check(r.ioc == ioc, what);
                std::snprintf(what, sizeof what, "IOC %d line width", ioc);
                check(r.img.width == (ioc == 288 ? 905 : 1810), what);
                check(sd < 1.5, "decode straight (stdev<1.5px)");
                // 90 lpm reads -62 ppm on a zero-ppm signal, and the fault
                // is the GENERATOR's: 8000*60/90 = 5333.33 samples truncates
                // to 5333, so the generated line is genuinely 62.5 ppm
                // short (measured -62.4, both IOCs; 60 and 120 lpm divide
                // 8000 exactly and read 0.0). Pin the generated truth, not
                // the intended one.
                const double clock_true = (lpm == 90) ? -62.5 : 0.0;
                check(std::fabs(r.clock_ppm - clock_true) < 15,
                      "clock near the generated truth");
            }
        }
    }

    std::printf("[6] LF deviation +/-150 Hz [ISO §4.2.2]\n");
    {
        nova::GenOptions g;
        g.deviation = 150;
        nova::DecodeOptions d;
        nova::DecodeResult r = run(g, kLines, d);
        check(edge_stdev(crop_rows(r.img, 40, kLines)) < 2.0,
              "150 Hz deviation decodes straight");
        check(r.timebase == nova::Timebase::kLinear,
              "a clean LF signal is not called a stepping timebase");
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
        std::printf("  place error: linear=%.2f px (%d seams) "
                    "stepped=%.2f px (%d seams)\n",
                    lin.place_rms_px, lin.seams, stp.place_rms_px, stp.seams);

        // The same insertions at the line BOUNDARY rather than mid-line.
        // I expected this to be the easy case and the mid-line one to be
        // permanently limited — the sync template of the line the samples
        // land in straddles them, so it can only read something between the
        // old level and the new one. Measured, the two are the same to a
        // third of a pixel (0.66 and 0.28), which says the ambiguity costs
        // one line and nothing else. Kept as a second ground-truth case:
        // where the samples go in is a property no real recording lets us
        // choose, so both are worth pinning.
        std::vector<float> at_edge;
        at_edge.reserve(stepped.size());
        for (size_t i = 0; i < clean.size(); i++) {
            at_edge.push_back(clean[i]);
            if (i % (line * kEvery) == 0)
                for (int k = 0; k < kIns; k++) at_edge.push_back(clean[i]);
        }
        const nova::DecodeResult edge = run_video(at_edge);
        std::printf("  same insertions at the line boundary: place %.2f px, "
                    "bar stdev %.2f px (%d seams)\n",
                    edge.place_rms_px,
                    edge_stdev(crop_rows(edge.img, 40, kLines)), edge.seams);
        check(edge.timebase == nova::Timebase::kSteps,
              "boundary insertions are detected too");
        check(edge.place_rms_px < 1.0,
              "...and corrected: the picture is drawn where the signal is");
        // Session 9 pinned this both ways — "the picture still comes out"
        // AND "...but visibly wobbles, which is why the flag exists" — and
        // the second half was true of a decoder that only REPORTED a
        // stepping timebase. Session 11 repairs it, so the assertion that
        // wobble exists is now the wrong claim and is replaced rather than
        // relaxed: 2.19 px of bar scatter before, 0.00 after, on the one
        // signal whose ground truth is known. The flag still exists for what
        // it always meant: clock_ppm and the anchor delta on a stepping
        // recording are not comparable with a clean one's.
        check(sd_stp < 1.0, "the stepping picture is drawn STRAIGHT now");
        check(stp.place_rms_px < 1.0,
              "...and the decoder's own account of it agrees");
        check(stp.timebase == nova::Timebase::kSteps,
              "...while still reporting that the recording steps");

        // The 60 lpm form of the same fault, with JSC1's signature: ~17
        // samples every ~3 lines (313 per 1000 measured on the recording).
        // Session 12 exists because JSC1 and JSC5 kept reading 5 px of
        // row-rigidity after session 11b while the four 120 lpm files went
        // to 1, and "60 lpm" was a suspect, not a measurement. Measured
        // here against ground truth: at one step per three lines the steps
        // are too dense to separate (the ±kSegHalf windows straddle steps
        // on both sides, so change points mostly do not fire), the rows are
        // drawn on the fitted ramp to 1.1 px rms / 3.2 px worst, and the
        // rigidity statistic reads the step SIZE itself — a correctly drawn
        // picture of this recording genuinely has rows whose two ends
        // disagree, and 17 samples is 3.8 px at 60 lpm. The library decode
        // reads the same 5.0 px this provably-good decode reads, so JSC1
        // and JSC5 are as good as the signal allows. What is pinned is the
        // placement, not the rigidity: the rigidity number at 60 lpm is a
        // property of the statistic, not of the decoder.
        {
            nova::GenOptions g6;
            g6.lpm = 60;
            nova::Image c6 = nova::gen_test_pattern(1810, kLines);
            const std::vector<float> clean6 =
                nova::gen_fax_signal(c6, kLines, g6);
            const size_t line6 =
                static_cast<size_t>(g6.fs * 60.0 / g6.lpm);
            constexpr int kIns6 = 17, kEvery6 = 3;
            std::vector<float> stp6;
            stp6.reserve(clean6.size() +
                         clean6.size() / line6 / kEvery6 * kIns6);
            for (size_t i = 0; i < clean6.size(); i++) {
                stp6.push_back(clean6[i]);
                if (i % (line6 * kEvery6) == line6 / 2)
                    for (int q = 0; q < kIns6; q++) stp6.push_back(clean6[i]);
            }
            std::vector<float> v6 =
                nova::fm_demod(stp6, g6.fs, 1900.0, g6.deviation);
            const nova::DecodeResult s6 = nova::decode_fax(v6, g6.fs, d);
            const double sd6 = edge_stdev(crop_rows(s6.img, 40, kLines));
            std::printf("  60 lpm, JSC1's step density: place %.2f px, bar "
                        "stdev %.2f px (%d seams)\n",
                        s6.place_rms_px, sd6, s6.seams);
            check(s6.place_rms_px < 1.5,
                  "dense 60 lpm steps are still placed where the signal is");
            // Not < 1.0 like the 120 lpm case: the ramp residual IS half a
            // step (17 samples = 3.8 px peak at 60 lpm), so the bar edge
            // wobbles ~1.2 px around straight. 1.24 measured; pinned with
            // headroom at 1.5, which an actual slant would blow through.
            check(sd6 < 1.5, "...and the picture is drawn straight");
        }

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

    // [ISO §4.2.2, WMO §5.3.3] receiver level handling: the same
    // transmission at 0.5, 0.05 and 0.005 of full scale — two orders of
    // magnitude of input level. Measured: the decoder is level-blind across
    // the whole span (the FM subcarrier carries its information in
    // frequency, not amplitude), so these are the clean-decode bounds, not
    // loosened-for-weak-signal ones: all three levels measured IDENTICAL
    // (179 locks, clock +0.0 ppm, bar stdev 0.00 px).
    std::printf("[11] input amplitude 0.5 / 0.05 / 0.005 [ISO §4.2.2, WMO "
                "§5.3.3]\n");
    {
        for (double amp : {0.5, 0.05, 0.005}) {
            nova::GenOptions g;
            g.amplitude = amp;
            nova::DecodeOptions d;
            nova::DecodeResult r = run(g, 150, d);
            const double sd = edge_stdev(crop_rows(r.img, 42, 140));
            std::printf("  amplitude %.3f: %d locks, clock %+.1f ppm, bar "
                        "stdev %.2f px\n",
                        amp, r.locked_lines, r.clock_ppm, sd);
            check(r.lpm == 120, "rate still auto-detected");
            check(std::fabs(r.clock_ppm) < 15, "clock still measured");
            check(r.locked_lines > 150,
                  "per-line sync still locks (179 measured at every level)");
            check(sd < 1.5, "picture still straight");
        }
    }

    // [WMO §5.4.3] gray scale linearity: an eight-band step chart, black to
    // white in equal 1/7 steps, must come back monotonic, with plausible
    // endpoints and roughly even spacing. Measured on the current decoder:
    // every band within 1 LSB of the generated level (0/36/72/109/145/182/
    // 218/255 in, the same out). The bounds below are that truth with room
    // around it — a step outside [15,60] is either a lost band or a
    // doubled one, and the endpoints pin black and white themselves.
    std::printf("[12] gray linearity: eight-band step chart [WMO §5.4.3]\n");
    {
        constexpr int kBands = 8;
        nova::Image bands;
        bands.width = 1810;
        bands.height = 100;
        bands.px.assign(static_cast<size_t>(1810) * 100, 0);
        for (int b = 0; b < kBands; b++)
            for (int y = 0; y < bands.height; y++)
                for (int x = b * 1810 / kBands; x < (b + 1) * 1810 / kBands;
                     x++)
                    bands.px[static_cast<size_t>(y) * 1810 + x] =
                        static_cast<uint8_t>(255 * b / (kBands - 1));
        nova::GenOptions g;
        nova::DecodeOptions d;
        nova::DecodeResult r = run(g, 100, d, &bands);
        nova::Image crop = crop_rows(r.img, 40, 100);
        const int W = crop.width;
        const int pic0 = static_cast<int>(0.036 * W);
        const int pic1 = static_cast<int>(0.984 * W);
        double level[kBands];
        std::printf("  recovered band levels:");
        for (int b = 0; b < kBands; b++) {
            const int cx = pic0 + static_cast<int>((b + 0.5) / kBands *
                                                   (pic1 - pic0));
            double acc = 0;
            int n = 0;
            for (int y = 20; y < 80; y++)
                for (int x = cx - 5; x <= cx + 5; x++) {
                    acc += crop.px[static_cast<size_t>(y) * W + x];
                    n++;
                }
            level[b] = acc / n;
            std::printf(" %.0f", level[b]);
        }
        std::printf(" (generated 0 36 73 109 146 182 219 255)\n");
        check(level[0] < 40, "black band is black (0.0 measured)");
        check(level[kBands - 1] > 215, "white band is white (255 measured)");
        bool steps_even = true;
        for (int b = 1; b < kBands; b++) {
            const double step = level[b] - level[b - 1];
            if (step < 15 || step > 60) steps_even = false;
        }
        check(steps_even,
              "levels monotonic, steps roughly even (36-37 measured)");
    }

    // [WMO §5.2.2] the start tone may run 5-10 s; [9] pins the 5 s form,
    // this pins the 10 s one. The boundary that matters is WHERE the
    // picture starts: 10 s of tone + 15 s of phasing = 25.0 s, and the
    // decoder puts it at exactly 25.00 (row 0 of the output is image line
    // 0, MAD 15.4 measured). lines_dropped_head reads 36, NOT the naive 50:
    // it is counted from the comb onset, and a long pure tone holds no
    // line comb, so the onset gate only fires 7.5 s in (with the 5 s tone
    // the onset lands at 0.0 s and the same boundary reads head=40, [9]).
    // The "~50 lines" form of this assertion failed on a decode whose
    // picture is provably right; the time boundary is the honest pin.
    std::printf("[13] 10 s start tone: segmentation boundary [WMO §5.2.2, "
                "§5.2.3]\n");
    {
        constexpr int kLines13 = 200;
        nova::GenOptions g;
        g.start_sec = 10.0;
        nova::Image content = nova::gen_test_pattern(1810, kLines13);
        std::vector<float> sig = nova::gen_fax_signal(content, kLines13, g);
        std::vector<float> video =
            nova::fm_demod(sig, g.fs, 1900.0, g.deviation);
        nova::DecodeOptions d;  // segmentation ON — this is the test
        nova::DecodeResult r = nova::decode_fax(video, g.fs, d);
        std::printf("  lines=%d dropped head=%d tail=%d image=%.2f-%.2f s\n",
                    r.lines, r.lines_dropped_head, r.lines_dropped_tail,
                    r.image_t_start, r.image_t_end);
        check(r.segmented, "segmentation applied");
        check(std::fabs(r.image_t_start - 25.0) <= 1.0,
              "picture starts at the tone+phasing boundary (25.0 s)");
        check(r.lines_dropped_head >= 30,
              "at least the phasing interval is dropped from the head");
        check(std::abs(r.lines - kLines13) <= 3, "the picture lines survive");
        const int rows = std::min(r.lines, kLines13) - 2;
        nova::Image ref = make_full_ref(1810, rows);
        nova::Image crop = crop_rows(r.img, 0, rows);
        const double mad = mean_abs_diff(crop, ref);
        std::printf("  MAD from row 0 (no crop offset) = %.1f\n", mad);
        check(mad < 20, "row 0 of the output is image line 0");
    }

    // Live captures arrive at the sound card's rate, not 8 kHz: generate at
    // 44100, resample to 8000 with core/resample.hpp, demod and decode
    // there. Same bounds as the native-8k clean round-trip [1]; measured
    // essentially identical (149 locks, clock +0.02 ppm, bar stdev 0.00).
    std::printf("[14] 44.1 kHz capture resampled to 8 kHz decodes clean\n");
    {
        constexpr int kLines14 = 120;
        nova::GenOptions g;
        g.fs = 44100;
        nova::Image content = nova::gen_test_pattern(1810, kLines14);
        std::vector<float> sig44 = nova::gen_fax_signal(content, kLines14, g);
        std::vector<float> sig8 = nova::resample(sig44, 44100, 8000);
        std::vector<float> video =
            nova::fm_demod(sig8, 8000, 1900.0, g.deviation);
        nova::DecodeOptions d;
        d.segment = false;
        nova::DecodeResult r = nova::decode_fax(video, 8000, d);
        nova::Image crop = crop_rows(r.img, 40, kLines14);
        const double mad = mean_abs_diff(crop, make_full_ref(1810, kLines14));
        const double sd = edge_stdev(crop);
        std::printf("  %d locks, clock %+.2f ppm, MAD %.1f, bar stdev "
                    "%.2f px\n",
                    r.locked_lines, r.clock_ppm, mad, sd);
        check(r.lpm == 120 && r.ioc == 576,
              "rate and IOC auto-selected at 8 kHz");
        check(std::fabs(r.clock_ppm) < 15,
              "clock near zero through the resample");
        check(mad < 20, "image content matches (MAD<20)");
        check(sd < 1.5, "picture straight");
    }

    // [WMO §5.1.3.3] tolerance edges of the dead sector: the sector itself
    // is 4.5% ± 0.5% of a line, and the black pulse may fill up to half of
    // it. Generated at both sector edges crossed with a narrow (1.0%) pulse
    // and a pulse exactly half of that sector — the four permitted corners.
    // Measured on all four: phasing found, pulse style detected, 173-179
    // locks of 220 drawn lines, bar stdev 0.00 px, place rms <= 0.01 px.
    std::printf("[15] dead-sector tolerance edges [WMO §5.1.3.3]\n");
    {
        for (double dead : {0.040, 0.050})
            for (double pulse : {0.010, dead * 0.5}) {
                nova::GenOptions g;
                g.dead_frac = dead;
                g.pulse_frac = pulse;
                nova::DecodeOptions d;
                nova::DecodeResult r = run(g, 150, d);
                const double sd = edge_stdev(crop_rows(r.img, 42, 140));
                std::printf("  dead %.1f%% pulse %.2f%%: phasing %s, %s "
                            "style, %d/%d locks, bar stdev %.2f px\n",
                            dead * 100, pulse * 100,
                            r.phasing_found ? "found" : "MISSING",
                            r.per_line_sync ? "pulse" : "white-only",
                            r.locked_lines, r.lines, sd);
                check(r.phasing_found, "phasing found");
                check(r.per_line_sync, "pulse style detected");
                check(r.locked_lines > r.lines / 2,
                      "most drawn lines lock (173-179/220 measured)");
                check(sd < 1.5, "picture straight");
            }
    }

    // [16] exists because audit Pass A built this case by hand and found
    // nothing in the suite covered it. A white-only station (VMW, NMC, GYA)
    // carries no per-line sync, so its phasing interval is the ONLY place
    // its line phase exists. Lose that interval — fading does exactly this,
    // which is why gya-faded-phasing is in the library at all — and the
    // decode still returns 0, still writes a picture, and every status
    // field still reads much as it did. Measured on the real recording with
    // its phasing region silenced, the dead sector moved from column 1495
    // to column 404 of 1810.
    //
    // Generated rather than cut from a recording, deliberately: the
    // recordings are not redistributed, and a check this specific should
    // not be one of the 30 suites that vanish from a clean clone.
    std::printf("[16] no phase reference at all: white-only AND no phasing "
                "[audit Pass A]\n");
    {
        // The healthy white-only case first, so the flag is shown to be
        // about the DIFFERENCE and not merely about white-only stations —
        // otherwise it would fire on VMW every day and mean nothing.
        nova::GenOptions g;
        g.dead_pulse = false;   // white-only: no per-line sync exists
        g.phasing = true;       // ...but the phasing interval is present
        nova::DecodeOptions d;
        nova::DecodeResult ok = run(g, kLines, d);
        check(!ok.per_line_sync, "white-only station has no per-line sync");
        check(ok.phasing_found, "healthy white-only: phasing found");
        check(!ok.no_phase_reference,
              "healthy white-only is NOT flagged (it has a phase reference)");

        // Now take the phasing away, which is all that separates them.
        nova::GenOptions g2 = g;
        g2.phasing = false;
        nova::DecodeResult bad = run(g2, kLines, d);
        check(!bad.per_line_sync, "still white-only");
        check(!bad.phasing_found, "and now no phasing interval");
        check(bad.no_phase_reference,
              "no phase reference anywhere IS flagged");

        // An operator hint IS a phase reference, so the flag must clear.
        nova::DecodeOptions d3 = d;
        d3.phase_anchor_hint = 0.5;
        nova::DecodeResult hinted = run(g2, kLines, d3);
        check(!hinted.no_phase_reference,
              "an operator PHASE hint clears the flag");
    }

    std::printf(failures ? "\n%d FAILURE(S)\n" : "\nall tests passed\n",
                failures);
    return failures ? 1 : 0;
}
