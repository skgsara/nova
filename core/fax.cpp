// fax.cpp
#include "fax.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace nova {
namespace {

constexpr double kDeadFrac = 0.045;   // dead sector, of line [WMO §5.1.3.3]
constexpr double kPulseFrac = 0.0225; // sync pulse <= half the dead sector

// Mean-downsample video to ~200 Hz for cheap autocorrelation.
std::vector<float> to_200hz(const std::vector<float>& v, int fs) {
    const int block = std::max(1, fs / 200);
    std::vector<float> out;
    out.reserve(v.size() / block + 1);
    for (size_t i = 0; i + block <= v.size(); i += block) {
        float acc = 0;
        for (int j = 0; j < block; j++) acc += v[i + j];
        out.push_back(acc / block);
    }
    return out;
}

// Best lag in [lo, hi] by biased autocorrelation; parabolic refine.
double best_period(const std::vector<float>& v, double lo, double hi) {
    const size_t n = v.size();
    const double mean = std::accumulate(v.begin(), v.end(), 0.0) / n;
    double best = -1e300;
    long best_lag = static_cast<long>(lo);
    std::vector<double> ac(static_cast<size_t>(hi) + 2, 0.0);
    for (long lag = static_cast<long>(lo); lag <= static_cast<long>(hi);
         lag++) {
        double s = 0.0;
        const size_t m = n - static_cast<size_t>(lag);
        for (size_t i = 0; i < m; i++)
            s += (v[i] - mean) * (v[i + lag] - mean);
        s /= m;
        ac[static_cast<size_t>(lag)] = s;
        if (s > best) {
            best = s;
            best_lag = lag;
        }
    }
    const double y0 = ac[static_cast<size_t>(best_lag) - 1];
    const double y1 = ac[static_cast<size_t>(best_lag)];
    const double y2 = ac[static_cast<size_t>(best_lag) + 1];
    const double denom = y0 - 2.0 * y1 + y2;
    const double d = (denom != 0.0) ? 0.5 * (y0 - y2) / denom : 0.0;
    return best_lag + d;
}

inline float lerp_at(const std::vector<float>& v, double pos) {
    if (pos < 0.0) return v.front();
    if (pos >= v.size() - 1) return v.back();
    const size_t i = static_cast<size_t>(pos);
    const double f = pos - i;
    return static_cast<float>(v[i] * (1.0 - f) + v[i + 1] * f);
}

// Sync-template score [WMO §5.1.3.3]: the dead sector holds a black pulse
// (<= half the dead sector) followed by white. Content-independent: unlike
// edge strength, this cannot lock onto picture content, because the pulse
// is black->white in every image line regardless of what precedes it.
// Returns score in roughly [-1, 1]; ~1.0 = clean sync at p.
double sync_score(const std::vector<float>& v, double p, double pulse,
                  double white) {
    double dark = 0.0, bright = 0.0;
    int nd = 0, nb = 0;
    for (double x = p; x < p + pulse; x += 1.0, nd++)
        dark += lerp_at(v, x);
    for (double x = p + pulse; x < p + pulse + white; x += 1.0, nb++)
        bright += lerp_at(v, x);
    if (nd == 0 || nb == 0) return -1.0;
    return bright / nb - dark / nd;
}

// Best template position in [lo, hi], parabolic sub-sample refinement.
double best_sync(const std::vector<float>& v, double lo, double hi,
                 double pulse, double* score) {
    double best_s = -1e300, best_p = lo;
    for (double p = lo; p < hi; p += 1.0) {
        const double s = sync_score(v, p, pulse, pulse);
        if (s > best_s) {
            best_s = s;
            best_p = p;
        }
    }
    const double sm = sync_score(v, best_p - 1, pulse, pulse);
    const double sp = sync_score(v, best_p + 1, pulse, pulse);
    const double denom = sm - 2.0 * best_s + sp;
    if (denom != 0.0) best_p += 0.5 * (sm - sp) / denom;
    if (score) *score = best_s;
    return best_p;
}

double median(std::vector<double> x) {
    if (x.empty()) return 0.0;
    std::nth_element(x.begin(), x.begin() + x.size() / 2, x.end());
    return x[x.size() / 2];
}

}  // namespace

DecodeResult decode_fax(const std::vector<float>& video, int fs,
                        const DecodeOptions& opt) {
    if (video.empty()) throw std::invalid_argument("decode_fax: empty input");
    DecodeResult res;

    const size_t start = static_cast<size_t>(opt.start_sec * fs);
    if (start >= video.size())
        throw std::invalid_argument("decode_fax: start beyond end");

    // --- 1. coarse line rate (200 Hz autocorr, snap to standard) ---------
    std::vector<float> v200 = to_200hz(video, fs);
    if (v200.size() < 600)
        throw std::runtime_error("decode_fax: recording too short");
    double period_200;
    if (opt.lpm == 0) {
        period_200 = best_period(v200, 200.0 * 60 / 127, 200.0 * 60 / 57);
    } else {
        const double nom = 200.0 * 60 / opt.lpm;
        period_200 = best_period(v200, nom * 0.97, nom * 1.03);
    }
    const double measured_lpm = 60.0 * 200.0 / period_200;
    static const int kRates[] = {60, 90, 120};
    int lpm = opt.lpm;
    if (lpm == 0) {
        lpm = kRates[0];
        for (int r : kRates)
            if (std::fabs(r - measured_lpm) < std::fabs(lpm - measured_lpm))
                lpm = r;
    }
    res.lpm = lpm;

    const double period0 = period_200 * fs / 200.0;  // coarse, samples/line
    const double nominal = fs * 60.0 / lpm;
    const double pulse = kPulseFrac * period0;
    const double search = opt.search_frac * period0;

    const size_t avail = video.size() - start;
    int n_lines = static_cast<int>(avail / period0);
    if (opt.max_lines > 0) n_lines = std::min(n_lines, opt.max_lines);
    if (n_lines < 4) throw std::runtime_error("decode_fax: too few lines");

    // --- 2. coarse phase: fold-average, max-contrast 4.5% window ---------
    const int fold_lines = std::min(40, n_lines);
    const int plen = static_cast<int>(period0);
    std::vector<double> fold(plen, 0.0);
    for (int l = 0; l < fold_lines; l++)
        for (int i = 0; i < plen; i++)
            fold[i] += lerp_at(video, start + l * period0 + i);
    for (auto& x : fold) x /= fold_lines;
    double best_c = -1.0, dead_start0 = 0.0;
    const int w = std::max(2, static_cast<int>(kDeadFrac * plen));
    for (int i = 0; i < plen; i++) {
        double mn = 1e300, mx = -1e300;
        for (int j = 0; j < w; j++) {
            const double s = fold[(i + j) % plen];
            mn = std::min(mn, s);
            mx = std::max(mx, s);
        }
        if (mx - mn > best_c) {
            best_c = mx - mn;
            dead_start0 = i;
        }
    }

    // --- 3. pass A: sequential sync tracking -------------------------------
    // Walk line to line: each search window is centred on the previous
    // lock + coarse period, so sound-card drift can never walk the sync
    // out of the window (the failure mode of a fixed coarse grid).
    // Lines with no real sync match are coasted (prediction only).
    std::vector<double> spos(n_lines), sstr(n_lines);
    {
        const double wide = 0.05 * period0;
        // must exceed the phasing<->image regime offset (~half a dead
        // sector = 0.0225 lines) or the tracker falls off the grid at the
        // phasing->image boundary and coasts to EOF
        const double narrow = 0.03 * period0;
        double pred = start + dead_start0;
        spos[0] = best_sync(video, std::max<double>(1.0, pred - wide),
                            std::min<double>(video.size() - 2 * pulse - 2,
                                             pred + wide),
                            pulse, &sstr[0]);
        double last_good = spos[0];
        long last_good_l = 0;
        if (sstr[0] < 0.6) last_good = pred;  // coast from coarse
        for (int l = 1; l < n_lines; l++) {
            const double c = last_good + (l - last_good_l) * period0;
            const double lo = std::max<double>(1.0, c - narrow);
            const double hi = std::min<double>(
                video.size() - 2 * pulse - 2, c + narrow);
            spos[l] = best_sync(video, lo, hi, pulse, &sstr[l]);
            if (sstr[l] >= 0.6) {
                last_good = spos[l];
                last_good_l = l;
            }
        }
    }

    // --- 4. pass B: robust period/phase from median slope ------------------
    // Least squares bends here by design of the signal: phasing lines and
    // image lines anchor the template ~half a dead sector apart (the wedge
    // is the mirror of the pulse), so a single fitted line through both
    // regimes tilts the slope by tens of ppm. Consecutive-line slopes are
    // regime-pure except at the one boundary, so the MEDIAN slope is the
    // true line period. Intercept likewise from the median residual.
    double a = start + dead_start0, b = period0;
    {
        std::vector<double> slopes;
        for (int l = 1; l < n_lines; l++) {
            for (int j = std::max(0, l - 10); j < l; j++) {
                if (sstr[j] < 0.6 || sstr[l] < 0.6) continue;
                const double s = (spos[l] - spos[j]) / (l - j);
                if (std::fabs(s - period0) < 0.02 * period0)
                    slopes.push_back(s);
            }
        }
        if (!slopes.empty()) {
            b = median(slopes);
            std::vector<double> intercepts;
            for (int l = 0; l < n_lines; l++)
                if (sstr[l] >= 0.6) intercepts.push_back(spos[l] - b * l);
            a = median(intercepts);
        }
    }
    res.line_period_s = b / fs;
    res.clock_ppm = (b / nominal - 1.0) * 1e6;

    if (!opt.autolock) {
        // no clock correction at all: coarse phase, nominal period
        a = start + dead_start0;
        b = nominal;
    }

    if (std::getenv("NOVA_DEBUG")) {
        std::fprintf(stderr,
                     "dbg: dead_start0=%.1f a=%.1f b=%.4f n_lines=%d\n",
                     dead_start0, a, b, n_lines);
        for (int l = 0; l < n_lines; l += 10)
            std::fprintf(stderr, "dbg: l=%3d spos=%.1f sstr=%.2f resid=%+.1f\n",
                         l, spos[l], sstr[l], spos[l] - (a + b * l));
    }

    // --- 5. assembly: fit + local-median residual --------------------------
    // The template anchor is the sync-pulse start in image lines; in the
    // phasing region the best template match sits ~half a dead sector
    // earlier (phasing is white-wedge-then-black, the mirror image).
    // The local-median correction tracks that region-constant offset;
    // the clamp lets the phasing->image transition through in one step
    // while rejecting wild jumps.
    const int width = (opt.ioc == 288) ? 905 : 1810;
    Image img;
    img.width = width;
    img.height = n_lines;
    img.px.resize(static_cast<size_t>(width) * n_lines);

    const double corr_clamp = 0.03 * b;
    double prev_corr = 0.0;
    bool have_corr = false;
    const int med_rad = 8;
    for (int l = 0; l < n_lines; l++) {
        double line_start = a + b * l;  // sync-anchor position
        if (opt.autolock) {
            std::vector<double> r;
            for (int k = std::max(0, l - med_rad);
                 k <= std::min(n_lines - 1, l + med_rad); k++) {
                const double rk = spos[k] - (a + b * k);
                if (sstr[k] > 0.6 && std::fabs(rk) < 2.0 * search)
                    r.push_back(rk);
            }
            double corr = r.empty() ? (have_corr ? prev_corr : 0.0)
                                    : median(r);
            if (have_corr) {
                if (corr > prev_corr + corr_clamp) {
                    corr = prev_corr + corr_clamp;
                    res.clamped_corrections++;
                } else if (corr < prev_corr - corr_clamp) {
                    corr = prev_corr - corr_clamp;
                    res.clamped_corrections++;
                } else {
                    res.locked_lines++;
                }
            }
            const double step_px =
                have_corr ? std::fabs(corr - prev_corr) / b * width : 0.0;
            res.max_step_px = std::max(res.max_step_px, step_px);
            prev_corr = corr;
            have_corr = true;
            line_start += corr;
        }

        // picture sector follows the dead sector [WMO §5.1.1, §5.1.3.3]
        const double pic_start = line_start + kDeadFrac * b;
        const double pic_len = b * (1.0 - kDeadFrac);
        for (int j = 0; j < width; j++) {
            const double pos = pic_start + pic_len * j / width;
            const float v = lerp_at(video, pos);
            img.px[static_cast<size_t>(l) * width + j] =
                static_cast<uint8_t>(std::lround(v * 255.0f));
        }
    }

    res.img = std::move(img);
    res.lines = n_lines;
    return res;
}

}  // namespace nova
