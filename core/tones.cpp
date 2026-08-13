// tones.cpp — see tones.hpp for what this measures and why.
#include "tones.hpp"
#include <algorithm>
#include <cmath>

namespace nova {
namespace {
constexpr double kPi = 3.14159265358979323846;

struct Cand {
    ToneKind kind;
    double nominal;
    double min_sec;
};

}  // namespace

double tone_median(std::vector<double> x) {
    if (x.empty()) return 0.0;
    std::sort(x.begin(), x.end());
    return x[x.size() / 2];
}

// 10-90% spread, the weatherfax_pi/KiwiSDR outlier-tolerant width.
double tone_spread_10_90(std::vector<double> x) {
    if (x.size() < 3) return 0.0;
    std::sort(x.begin(), x.end());
    const size_t lo = x.size() / 10;
    const size_t hi = x.size() - 1 - x.size() / 10;
    return x[hi] - x[lo];
}

const char* tone_name(ToneKind k) {
    switch (k) {
        case ToneKind::kStartIOC576: return "start-576";
        case ToneKind::kStartIOC288: return "start-288";
        case ToneKind::kStop:        return "stop";
    }
    return "?";
}

double tone_purity(const std::vector<float>& v, size_t s, size_t n, int fs,
                   double f) {
    if (n < 8 || s + n > v.size()) return 0.0;

    // Hann window. Rectangular leakage is the enemy here: broadband picture
    // content would spill into the tone bin and read as purity it does not
    // have, which is precisely the false positive this whole file exists to
    // avoid.
    double mean = 0.0;
    for (size_t i = 0; i < n; i++) mean += v[s + i];
    mean /= static_cast<double>(n);

    const double w = 2.0 * kPi * f / fs;
    const double coeff = 2.0 * std::cos(w);
    double q1 = 0.0, q2 = 0.0;  // Goertzel state
    double tot = 0.0;           // windowed AC power
    double sw = 0.0, sw2 = 0.0;
    for (size_t i = 0; i < n; i++) {
        const double hw =
            0.5 - 0.5 * std::cos(2.0 * kPi * i / static_cast<double>(n - 1));
        const double x = (v[s + i] - mean) * hw;
        const double q0 = x + coeff * q1 - q2;
        q2 = q1;
        q1 = q0;
        tot += x * x;
        sw += hw;
        sw2 += hw * hw;
    }
    if (tot <= 1e-20) return 0.0;
    const double p = q1 * q1 + q2 * q2 - coeff * q1 * q2;  // |X(f)|^2
    // Normalizer that makes a pure sinusoid at f read exactly 1.0:
    // |X|^2 = (A/2 * sum w)^2 while the windowed power is A^2/2 * sum w^2.
    const double k = (sw * sw) / (2.0 * sw2);
    const double r = p / (tot * k);
    return r < 0.0 ? 0.0 : (r > 1.0 ? 1.0 : r);
}

double tone_purity_band(const std::vector<float>& v, size_t s, size_t n,
                        int fs, double nominal, double tol, double* freq_out) {
    // Probes spaced at half a Hann bin so no in-band tone can fall between
    // two of them.
    const double bin = 2.0 * fs / static_cast<double>(n);
    const double step = bin / 2.0;
    const double hi = nominal * (1.0 + tol);
    double best = 0.0, bf = nominal, pm = 0.0, pp = 0.0;
    double prev = 0.0;
    bool have_prev = false;
    for (double f = nominal * (1.0 - tol); f <= hi + 1e-9; f += step) {
        const double p = tone_purity(v, s, n, fs, f);
        if (p > best) {
            best = p;
            bf = f;
            pm = have_prev ? prev : 0.0;
            pp = 0.0;
        } else if (bf == f - step) {
            pp = p;  // the probe just past the peak
        }
        prev = p;
        have_prev = true;
    }
    // Parabolic interpolation across the peak and its neighbours. Without
    // it the frequency estimate is quantized to the probe grid, and the
    // run-coherence test downstream compares a ±1% spread (3 Hz at 300 Hz)
    // against a grid coarser than itself — so one window landing on the
    // neighbouring probe failed a test finer than its own resolution.
    if (pm > 0.0 && pp > 0.0) {
        const double denom = pm - 2.0 * best + pp;
        if (std::fabs(denom) > 1e-12) {
            double d = 0.5 * (pm - pp) / denom;
            if (d > 0.5) d = 0.5;
            if (d < -0.5) d = -0.5;
            bf += d * step;
        }
    }
    if (freq_out) *freq_out = bf;
    return best;
}

std::vector<ToneEvent> detect_tones(const std::vector<float>& video, int fs,
                                    const ToneOptions& opt,
                                    const DecodeHooks& hooks) {
    std::vector<ToneEvent> out;
    const size_t n = static_cast<size_t>(opt.win_sec * fs);
    const size_t hop = static_cast<size_t>(opt.hop_sec * fs);
    if (n < 8 || hop == 0 || video.size() < n) return out;

    const Cand cands[] = {
        {ToneKind::kStartIOC576, 300.0, opt.min_start_sec},
        {ToneKind::kStartIOC288, 675.0, opt.min_start_sec},
        {ToneKind::kStop,        450.0, opt.min_stop_sec},
    };

    for (const Cand& c : cands) {
        struct W { double purity, freq; };
        std::vector<W> wins;
        for (size_t s = 0; s + n <= video.size(); s += hop) {
            if ((wins.size() & 127) == 0)
                throw_if_cancelled(hooks, "tones");
            W best{0.0, c.nominal};
            best.purity = tone_purity_band(video, s, n, fs, c.nominal,
                                           opt.tol, &best.freq);
            wins.push_back(best);
        }

        // Runs of hot windows, tolerating a dropout of up to max_gap_sec.
        // This is HF: a station fades mid-tone, and the first version of
        // this code (one cold window allowed) split real 5 s stop tones
        // into sub-minimum halves and discarded them — measured on VMW
        // 2230Z, NMC 2204Z and GYA 2300Z, session 6. Content never comes
        // near the threshold (library max 0.16 against 0.35), so the
        // discrimination is done by purity; the run rule only has to
        // survive fading.
        const size_t max_gap =
            static_cast<size_t>(opt.max_gap_sec / opt.hop_sec);
        size_t i = 0;
        while (i < wins.size()) {
            if (wins[i].purity < opt.purity) { i++; continue; }
            size_t j = i, last_hot = i, cold = 0;
            while (j + 1 < wins.size()) {
                if (wins[j + 1].purity >= opt.purity) {
                    last_hot = j + 1;
                    cold = 0;
                } else if (++cold > max_gap) {
                    break;
                }
                j++;
            }

            std::vector<double> fr, pu;
            for (size_t k = i; k <= last_hot; k++)
                if (wins[k].purity >= opt.purity) {
                    fr.push_back(wins[k].freq);
                    pu.push_back(wins[k].purity);
                }
            // Fraction of the run's span that is actually hot. A real tone
            // that fades is still mostly present; a pair of unrelated
            // bursts bridged by the gap rule is not.
            const double hot_frac =
                static_cast<double>(fr.size()) /
                static_cast<double>(last_hot - i + 1);
            const double t0 = static_cast<double>(i * hop) / fs;
            const double t1 =
                static_cast<double>(last_hot * hop + n) / fs;
            const double dur = t1 - t0;
            const double sp = tone_spread_10_90(fr) / c.nominal;
            dlog(hooks, LogTopic::kInfo,
                 "dbg: tone %s run %.2f-%.2fs (%.2fs) f=%.1f "
                 "purity=%.3f spread=%.4f hot=%.2f",
                 tone_name(c.kind), t0, t1, dur, tone_median(fr),
                 tone_median(pu), sp, hot_frac);
            // A real control tone holds ONE frequency. A run assembled out
            // of noise wanders across the search band, so the frequency
            // spread rejects it even when single windows look pure.
            if (dur >= c.min_sec && sp <= opt.max_spread &&
                hot_frac >= opt.min_hot_frac) {
                ToneEvent e;
                e.kind = c.kind;
                e.t_start = t0;
                e.t_end = t1;
                e.freq_hz = tone_median(fr);
                e.purity = tone_median(pu);
                out.push_back(e);
            }
            i = last_hot + 1;
        }
    }

    std::sort(out.begin(), out.end(),
              [](const ToneEvent& a, const ToneEvent& b) {
                  return a.t_start < b.t_start;
              });
    return out;
}

}  // namespace nova
