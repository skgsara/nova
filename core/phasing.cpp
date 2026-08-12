// phasing.cpp — see phasing.hpp.
#include "phasing.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace nova {
namespace {

double median_of(std::vector<double> x) {
    if (x.empty()) return 0.0;
    std::sort(x.begin(), x.end());
    return x[x.size() / 2];
}

double spread_10_90(std::vector<double> x) {
    if (x.size() < 3) return 0.0;
    std::sort(x.begin(), x.end());
    return x[x.size() - 1 - x.size() / 10] - x[x.size() / 10];
}

// Circular positions wrap, so a run sitting near the line boundary would
// read as a huge spread (some lines at 3, some at period-2) when it is in
// fact tight. Re-centre on the median before measuring anything.
void unwrap_about(std::vector<double>& x, double centre, double period) {
    for (double& v : x) {
        while (v - centre > period / 2.0) v -= period;
        while (centre - v > period / 2.0) v += period;
    }
}

// Best wedge fit on one line: the offset whose white run of `wlen` samples
// stands highest above the rest of the line. Prefix sums make every offset
// O(1), so the whole line is searched rather than a neighbourhood — there
// is no prior phase to search around at this stage.
struct Fit { double pos, score; };
Fit wedge_fit(const std::vector<float>& v, size_t s, size_t plen,
              size_t wlen) {
    if (wlen == 0 || wlen >= plen) return {0.0, -1.0};
    std::vector<double> cum(plen + 1, 0.0);
    for (size_t i = 0; i < plen; i++) cum[i + 1] = cum[i] + v[s + i];
    const double total = cum[plen];
    Fit best{0.0, -2.0};
    for (size_t o = 0; o < plen; o++) {
        double wsum;
        if (o + wlen <= plen)
            wsum = cum[o + wlen] - cum[o];
        else
            wsum = (cum[plen] - cum[o]) + cum[o + wlen - plen];  // wrap
        const double rest = total - wsum;
        const double sc = wsum / static_cast<double>(wlen) -
                          rest / static_cast<double>(plen - wlen);
        if (sc > best.score) best = Fit{static_cast<double>(o), sc};
    }
    return best;
}

}  // namespace

PhasingResult detect_phasing(const std::vector<float>& video, int fs,
                             double period, const PhasingOptions& opt) {
    PhasingResult res;
    const size_t plen = static_cast<size_t>(period);
    if (plen < 16 || video.size() < plen * 2) return res;
    const size_t n_lines = video.size() / plen;
    const bool dbg = std::getenv("NOVA_DEBUG") != nullptr;

    // Both waveforms WMO §5.2.3.2 permits. Whichever fits better per line
    // wins; the answer is reported, not configured.
    const size_t w_asym = static_cast<size_t>(0.05 * period);
    const size_t w_sym = static_cast<size_t>(0.50 * period);

    std::vector<double> pos(n_lines, 0.0), score(n_lines, -1.0);
    std::vector<char> asym(n_lines, 1);
    for (size_t l = 0; l < n_lines; l++) {
        const Fit a = wedge_fit(video, l * plen, plen, w_asym);
        const Fit s = wedge_fit(video, l * plen, plen, w_sym);
        if (a.score >= s.score) {
            pos[l] = a.pos;
            score[l] = a.score;
            asym[l] = 1;
        } else {
            pos[l] = s.pos;
            score[l] = s.score;
            asym[l] = 0;
        }
    }

    // Every run of consecutive phasing-like lines is a candidate, and each
    // is judged on its own merits. Taking the longest run first and
    // testing it afterwards would be wrong: a recording can hold both a
    // 480-line stretch of dark satellite imagery and the real 60-line
    // phasing interval, and the longest is then the false one.
    const size_t max_lines = static_cast<size_t>(opt.max_sec * fs / period);
    size_t i = 0;
    while (i < n_lines) {
        if (score[i] < opt.min_score) { i++; continue; }
        size_t j = i;
        while (j + 1 < n_lines && score[j + 1] >= opt.min_score) j++;
        const size_t len = j - i + 1;

        std::vector<double> p, sc;
        int n_asym = 0;
        for (size_t l = i; l <= j; l++) {
            p.push_back(pos[l]);
            sc.push_back(score[l]);
            n_asym += asym[l];
        }
        unwrap_about(p, median_of(p), period);
        const double med = median_of(p);
        const double sp = spread_10_90(p);
        if (dbg)
            std::fprintf(stderr,
                         "dbg: phasing cand %zu lines @line %zu pos=%.1f "
                         "spread=%.1f (limit %.1f) score=%.3f\n",
                         len, i, med, sp, opt.max_spread_frac * period,
                         median_of(sc));

        // The spread test is what stops a stretch of dark picture lines
        // from passing: they can each score well, but they do not agree on
        // WHERE the white run is, and phasing lines do.
        const bool ok = len >= static_cast<size_t>(opt.min_lines) &&
                        len <= max_lines &&
                        sp <= opt.max_spread_frac * period;
        if (ok && static_cast<int>(len) > res.lines) {
            res.found = true;
            res.t_start = static_cast<double>(i * plen) / fs;
            res.t_end = static_cast<double>((j + 1) * plen) / fs;
            res.lines = static_cast<int>(len);
            res.line_start = std::fmod(med + period, period);
            res.spread = sp;
            res.asymmetric = n_asym * 2 >= static_cast<int>(len);
            res.score = median_of(sc);
        }
        i = j + 1;
    }
    return res;
}

}  // namespace nova
