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
        // Where the next candidate starts, fixed before the trim below can
        // move `j` — otherwise the trimmed-off tail is rescanned as a run
        // of its own.
        const size_t next = j + 1;

        std::vector<double> p, sc;
        for (size_t l = i; l <= j; l++) {
            p.push_back(pos[l]);
            sc.push_back(score[l]);
        }
        unwrap_about(p, median_of(p), period);

        // Trim the ENDS back to lines that agree on where the white is.
        // The run was grown on score alone, and score alone lets a dark
        // picture line adjacent to the interval join it: the 10-90% spread
        // is a robust statistic, so up to a tenth of the run can disagree
        // wildly and the spread never shows it. Measured (session 7): the
        // last line of VMW 2230Z's "60-line" interval sits 718 samples off
        // the median, and the generated pattern's first two picture rows
        // sit 256 off — both were being counted as phasing, and both moved
        // the t_end that segmentation cuts the picture on.
        //
        // Ends only. A dropout in the MIDDLE is HF fading, not a boundary,
        // and the median is what makes that harmless.
        const double tol = opt.max_spread_frac * period;
        {
            const double m0 = median_of(p);
            size_t lo = 0, hi = p.size();
            while (lo < hi && std::fabs(p[lo] - m0) > tol) lo++;
            while (hi > lo && std::fabs(p[hi - 1] - m0) > tol) hi--;
            if (lo > 0 || hi < p.size()) {
                p = std::vector<double>(p.begin() + lo, p.begin() + hi);
                sc = std::vector<double>(sc.begin() + lo, sc.begin() + hi);
                j = i + hi - 1;
                i = i + lo;
            }
        }
        if (p.empty()) { i = next; continue; }
        const size_t len = p.size();

        // Non-linearity of the timebase across the run, in samples.
        //
        // `spread` alone cannot answer that question. The per-line positions
        // are measured in windows of the TRUNCATED period, so a clock that is
        // off by even 90 ppm walks the edge 0.66 samples per line and 40
        // samples across a 60-line interval — which is most of what `spread`
        // reports on a perfectly linear recording (measured: FAXSignal, whose
        // clock is exactly nominal, reads 1.0 where every -86 ppm recording
        // reads 25-43). Removing the best straight line first leaves the part
        // no constant clock can explain.
        //
        // Robust slope, session 5's lesson applied to a 60-line baseline:
        // pairs half the run apart, median over them, so one bad line moves
        // nothing and the quantization of a single position is divided by 30
        // rather than by 1.
        double nonlin = 0.0;
        if (len >= 8) {
            const size_t k = len / 2;
            std::vector<double> sl;
            for (size_t m = 0; m + k < len; m++)
                sl.push_back((p[m + k] - p[m]) / static_cast<double>(k));
            const double slope = median_of(sl);
            std::vector<double> icpt;
            for (size_t m = 0; m < len; m++)
                icpt.push_back(p[m] - slope * static_cast<double>(m));
            const double c = median_of(icpt);
            std::vector<double> resid;
            for (size_t m = 0; m < len; m++)
                resid.push_back(p[m] - (c + slope * static_cast<double>(m)));
            nonlin = spread_10_90(resid);
        }
        int n_asym = 0;
        for (size_t l = i; l <= j; l++) n_asym += asym[l];
        const double med = median_of(p);
        const double sp = spread_10_90(p);

        // Absolute anchor at the middle of the run. Each line's white edge
        // sits at l*plen + pos[l]; successive edges are one FRACTIONAL
        // period apart, so referring every line back to the middle one and
        // taking the median removes the integer-grid slip instead of
        // averaging over it. Without this the anchor carries up to
        // (period - plen) * lines/2 of error — 20 samples on a 60-line
        // interval at a typical -85 ppm clock, growing with run length.
        // An INTEGER line: the reference has to be a line that exists.
        // (i+j)/2 as a real number is a half-line whenever the run has an
        // even number of lines — which the 30 s phasing interval usually
        // does — and every such anchor came out exactly half a period off.
        // Odd-length runs looked perfect throughout, which is precisely why
        // this needed the picture to catch it and not the numbers.
        const double l_mid = static_cast<double>(i + (j - i) / 2);
        std::vector<double> abs_est;
        abs_est.reserve(len);
        for (size_t k = 0; k < len; k++) {
            const double l = static_cast<double>(i + k);
            abs_est.push_back(l * plen + p[k] - (l - l_mid) * period);
        }
        unwrap_about(abs_est, median_of(abs_est), period);
        const double anchor = median_of(abs_est);
        if (dbg) {
            std::fprintf(stderr,
                         "dbg: phasing cand %zu lines @line %zu pos=%.1f "
                         "spread=%.1f (limit %.1f) score=%.3f\n",
                         len, i, med, sp, opt.max_spread_frac * period,
                         median_of(sc));
            for (size_t k = 0; k < p.size() && std::getenv("NOVA_DEBUG_FULL");
                 k++)
                if (k < 3 || k + 3 >= p.size())
                    std::fprintf(stderr,
                                 "dbg:   line %zu pos=%.1f (%+.1f) score=%.3f\n",
                                 i + k, p[k], p[k] - med, sc[k]);
        }

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
            res.anchor = anchor;
            res.spread = sp;
            res.nonlinearity = nonlin;
            res.asymmetric = n_asym * 2 >= static_cast<int>(len);
            res.score = median_of(sc);
        }
        i = next;
    }
    return res;
}

}  // namespace nova
