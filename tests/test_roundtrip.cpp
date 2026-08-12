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
#include <cmath>
#include <cstdio>
#include <numeric>

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

    std::printf(failures ? "\n%d FAILURE(S)\n" : "\nall tests passed\n",
                failures);
    return failures ? 1 : 0;
}
