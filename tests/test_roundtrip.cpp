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
double edge_x(const nova::Image& img, int y) {
    const int x0 = img.width / 6 - 100;
    for (int x = x0; x < img.width / 6 + 100; x++)
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

nova::DecodeResult run(const nova::GenOptions& g, int lines,
                       nova::DecodeOptions d) {
    nova::Image content = nova::gen_test_pattern(
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
        nova::Image ref = nova::gen_test_pattern(1810, kLines);
        // compare a crop inside the image region (skip phasing rows)
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

    std::printf(failures ? "\n%d FAILURE(S)\n" : "\nall tests passed\n",
                failures);
    return failures ? 1 : 0;
}
