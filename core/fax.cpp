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
// Line layout measured on the JMH test chart (SESSION-LOG 2026-08-12
// session 3): 7.5 ms sync pulse, 10.5 ms white gap, 474 ms picture,
// then a ~8 ms black porch before the next pulse — the WMO §5.1.3.3
// dead sector is split around the line boundary. The image maps the
// FULL line starting at the sync pulse (576*pi px at IOC 576 is the
// full line, dead sector included; neither WMO nor ISO asks for any
// cropping), so pulse/gap/picture/porch all render truthfully.
constexpr double kPi = 3.14159265358979323846;

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

// Hann-windowed DFT power at f Hz for v[s .. s+wlen), sampling rate
// 200 Hz, window mean removed. Naive O(wlen) per call: used at comb
// teeth and band bins of short windows, never on the full signal.
double tooth_power(const std::vector<float>& v, size_t s, size_t wlen,
                   double mean, double f) {
    const double w = 2.0 * kPi * f / 200.0;
    double cr = 0.0, ci = 0.0;
    for (size_t n = 0; n < wlen; n++) {
        const double h = 0.5 - 0.5 * std::cos(2.0 * kPi * n / (wlen - 1));
        const double x = (v[s + n] - mean) * h;
        cr += x * std::cos(w * static_cast<double>(n));
        ci -= x * std::sin(w * static_cast<double>(n));
    }
    return cr * cr + ci * ci;
}

// Odd-harmonic comb fraction for candidate line rate f0 Hz in window
// [s, s+wlen) of 200 Hz video: energy on f0, 3f0 .. 9f0 as a fraction of
// the 0.5-50 Hz band. Odd teeth only — a 120 lpm comb is a subset of the
// 60 lpm comb, so even teeth cannot discriminate rates.
double comb_score(const std::vector<float>& v, size_t s, size_t wlen,
                  double f0) {
    double mean = 0.0;
    for (size_t n = 0; n < wlen; n++) mean += v[s + n];
    mean /= static_cast<double>(wlen);
    const double res = 200.0 / static_cast<double>(wlen);
    const size_t lo = static_cast<size_t>(std::ceil(0.5 / res));
    const size_t hi =
        std::min(static_cast<size_t>(50.0 / res), wlen / 2);
    double band = 0.0;
    for (size_t b = lo; b <= hi; b++)
        band += tooth_power(v, s, wlen, mean, b * res);
    if (band <= 0.0) return 0.0;
    double comb = 0.0;
    for (int k = 1; k <= 9; k += 2)
        if (k * f0 < 50.0) comb += tooth_power(v, s, wlen, mean, k * f0);
    return comb / band;
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

    // --- 1. signal onset + coarse line rate ------------------------------
    // Real recordings do not start with signal: receivers are started
    // early, stations send leader/tuning tones, and SDR streams stall and
    // replay fill. Autocorrelation over signal-free audio returns a
    // plausible junk period (measured: +96735 ppm on 60 s of stall-fill),
    // and a phase fold over fill anchors the tracker to noise for the
    // whole file (SESSION-LOG 2026-08-12 session 3). So the line comb is
    // located first; everything downstream anchors to it. No comb in any
    // window -> fail loudly instead of decoding noise into a fake image.
    std::vector<float> v200 = to_200hz(video, fs);
    const size_t start200 = std::min(
        static_cast<size_t>(opt.start_sec * 200.0), v200.size());
    if (v200.size() - start200 < 600)
        throw std::runtime_error("decode_fax: recording too short");

    // Gate: relative to the strongest window in the file, with an absolute
    // floor. Measured on the library (session 3): fill <= 0.05 (looped
    // stall-fill up to 0.12), 120 lpm signal 0.10-0.60, 60 lpm newspaper
    // fax 0.07-0.18 — a fixed gate cannot serve both, a relative one can.
    const double kGateFloor = 0.06;
    const size_t kWin = 3000, kHop = 1500;  // 15 s / 7.5 s at 200 Hz
    static const int kRates[] = {60, 90, 120};

    const size_t avail200 = v200.size() - start200;
    const size_t wlen = avail200 >= kWin ? kWin : avail200;
    const size_t hop = avail200 >= kWin ? kHop : 1;
    struct Win {
        size_t s;
        double score[3];  // per kRates entry
    };
    std::vector<Win> wins;
    double file_max = 0.0;
    for (size_t s = start200; s + wlen <= v200.size(); s += hop) {
        Win w{s, {0.0, 0.0, 0.0}};
        for (size_t ri = 0; ri < 3; ri++) {
            if (opt.lpm != 0 && kRates[ri] != opt.lpm) continue;
            w.score[ri] = comb_score(v200, s, wlen, kRates[ri] / 60.0);
            file_max = std::max(file_max, w.score[ri]);
        }
        wins.push_back(w);
        if (std::getenv("NOVA_DEBUG"))
            std::fprintf(stderr,
                         "dbg: comb win@%.1fs 60=%.3f 90=%.3f 120=%.3f\n",
                         s / 200.0, w.score[0], w.score[1], w.score[2]);
        if (wlen == avail200) break;  // short file: single window
    }
    const double gate = std::max(kGateFloor, 0.5 * file_max);

    // Rate rule: the LOWEST rate whose odd teeth clear the gate wins. A
    // true 60 lpm comb has energy at its 1 Hz fundamental; a true 120 lpm
    // signal has nothing at 1/3/5 Hz, so its 60-candidate never clears
    // (measured <= 0.01). The reverse rule would be ambiguous: 120's
    // teeth are a subset of 60's comb.
    auto clearing_rate = [&](const Win& w) -> int {
        for (size_t ri = 0; ri < 3; ri++)
            if (w.score[ri] >= gate) return kRates[ri];
        return 0;
    };

    // Onset = first window of the first pair of consecutive windows that
    // clear the gate on the same rate (isolated clears can be fill
    // artifacts; a real transmission sustains).
    size_t onset200 = 0;
    int lpm = opt.lpm;
    bool have_onset = false;
    for (size_t i = 1; i < wins.size(); i++) {
        const int r0 = clearing_rate(wins[i - 1]);
        const int r1 = clearing_rate(wins[i]);
        if (r0 != 0 && r0 == r1) {
            onset200 = wins[i - 1].s;
            if (lpm == 0) lpm = r0;
            have_onset = true;
            break;
        }
    }
    if (!have_onset && wins.size() == 1 && clearing_rate(wins[0]) != 0) {
        onset200 = wins[0].s;  // short file: one window is all we have
        if (lpm == 0) lpm = clearing_rate(wins[0]);
        have_onset = true;
    }
    if (!have_onset)
        throw std::runtime_error(
            "decode_fax: no fax line comb found (fill or no signal)");

    // Period: autocorrelation over everything from onset to EOF (the
    // onset gate has already excluded fill, the historical bias source).
    // Precision matters for weak signals where pass B cannot engage
    // (e.g. ±150 Hz LF deviation, whose sync template never reaches the
    // lock threshold): a 15 s window is not precise enough on its own.
    const double nom200 = 200.0 * 60.0 / lpm;
    std::vector<float> pwin(v200.begin() + onset200, v200.end());
    const double period_200 =
        best_period(pwin, nom200 * 0.97, nom200 * 1.03);
    if (std::getenv("NOVA_DEBUG"))
        std::fprintf(stderr, "dbg: onset=%.1fs lpm=%d measured=%.3f\n",
                     onset200 / 200.0, lpm, 60.0 * 200.0 / period_200);
    res.lpm = lpm;

    const size_t start = onset200 * static_cast<size_t>(fs) / 200;
    const double period0 = period_200 * fs / 200.0;  // samples/line
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

    // Honest lock metric: lines where the sync template actually matched.
    // (Before session 3 this counted "correction did not jump", which is
    // vacuously true when the tracker coasts through noise.)
    res.locked_lines = 0;
    for (int l = 0; l < n_lines; l++)
        if (sstr[l] >= 0.6) res.locked_lines++;

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
        const int step = std::getenv("NOVA_DEBUG_FULL") ? 1 : 10;
        for (int l = 0; l < n_lines; l += step)
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
                }
            }
            const double step_px =
                have_corr ? std::fabs(corr - prev_corr) / b * width : 0.0;
            res.max_step_px = std::max(res.max_step_px, step_px);
            prev_corr = corr;
            have_corr = true;
            line_start += corr;
        }

        // the whole line, sync pulse to sync pulse — no cropping
        const double pic_start = line_start;
        const double pic_len = b;
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
