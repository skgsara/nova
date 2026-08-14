// phasing.cpp — see phasing.hpp.
#include "phasing.hpp"
#include <algorithm>
#include <cmath>

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

// Half-width, in LINES, of the local median applied to the per-line phasing
// residual before its spread is called non-linearity. The image-domain half
// of the same test has always smoothed this way and for the same reason
// (fax.cpp §4d, `kMedRad`): a difference between two neighbouring lines is
// mostly measurement noise, while an inserted sample is PERSISTENT and
// survives a median. The phasing half went unsmoothed until session 10
// because every interval it had ever seen was a strong one, where the
// per-line edge is good to about a sample. GYA 2300Z is the first faded
// interval to reach it and reads 46.2 samples raw — worse than the stepping
// JSC files — from noise alone. Radius chosen by library sweep, see
// SESSION-LOG session 10.
constexpr int kNonlinMedRad = 3;

// A move of the smoothed residual bigger than this counts as one persistent
// step. Same 2 samples as the image domain's `kStepSec` (fax.cpp), and for
// the same reason: the quantity is inserted or dropped SAMPLES in a capture
// chain, which knows nothing about the line rate.
constexpr double kStepSmp = 2.0;

// Local median of a per-member series, over members whose LINE numbers are
// within `rad` lines. Line numbers, not member indices: a faded run has
// gaps, and a window of members would reach further across the recording
// wherever the signal was worst — which is exactly where it must not.
std::vector<double> smooth_local(const std::vector<double>& x,
                                 const std::vector<size_t>& line, int rad) {
    std::vector<double> out;
    out.reserve(x.size());
    for (size_t m = 0; m < x.size(); m++) {
        std::vector<double> w;
        for (size_t k = 0; k < x.size(); k++) {
            const long d = static_cast<long>(line[k]) -
                           static_cast<long>(line[m]);
            if (d >= -rad && d <= rad) w.push_back(x[k]);
        }
        std::sort(w.begin(), w.end());
        out.push_back(w[w.size() / 2]);
    }
    return out;
}

// Distance between two positions on a line, the short way round. Positions
// are a residue modulo the period, so 10 and period-10 are 20 apart, not
// period-20.
double circ_dist(double a, double b, double period) {
    double d = std::fabs(a - b);
    while (d > period / 2.0) d = std::fabs(d - period);
    return d;
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
    auto score_at = [&](size_t o) {
        double wsum;
        if (o + wlen <= plen)
            wsum = cum[o + wlen] - cum[o];
        else
            wsum = (cum[plen] - cum[o]) + cum[o + wlen - plen];  // wrap
        const double rest = total - wsum;
        return wsum / static_cast<double>(wlen) -
               rest / static_cast<double>(plen - wlen);
    };
    Fit best{0.0, -2.0};
    for (size_t o = 0; o < plen; o++) {
        const double sc = score_at(o);
        if (sc > best.score) best = Fit{static_cast<double>(o), sc};
    }
    // A clean wedge can leave a run of adjacent offsets with the SAME
    // score; taking the first turns the plateau's own width into fake
    // per-line steps. A ±150 Hz synthetic exposes this: its positions
    // snapped between two levels 10 samples apart and a linear recording
    // was convicted as a stepping timebase. The edge estimate is the
    // plateau's centre, not whichever end the scan reached first. If the
    // whole line ties (a solid white line), keep the first offset: there
    // is no edge to centre.
    size_t lo = static_cast<size_t>(best.pos);
    size_t hi = lo;
    size_t span = 1;
    constexpr double kTie = 1e-12;
    while (span < plen) {
        const size_t n = (lo + plen - 1) % plen;
        if (std::fabs(score_at(n) - best.score) > kTie) break;
        lo = n;
        span++;
    }
    while (span < plen) {
        const size_t n = (hi + 1) % plen;
        if (std::fabs(score_at(n) - best.score) > kTie) break;
        hi = n;
        span++;
    }
    if (span < plen)
        best.pos = static_cast<double>((lo + span / 2) % plen);
    return best;
}

// Refine a coarse wedge position to the leading edge of the white run
// [WMO §5.2.3.4]. The wedge fit is the right detector, but its maximum is
// a plateau whose position can move with the FM carrier phase: on a clean
// ±150 Hz synthetic it alternated by 10 samples line to line, which the
// timebase test then read as persistent steps. The 50% crossing of the
// same edge stays put (measured 30.5 samples on every line). The wedge
// answer still decides which feature is phasing; this only locates its
// edge more honestly.
double refine_leading_edge(const std::vector<float>& v, size_t s,
                           size_t plen, double approx) {
    const long c = static_cast<long>(approx);
    const long rad = static_cast<long>(plen / 20);  // ±5% of a line
    auto at = [&](long off) {
        long k = (c + off) % static_cast<long>(plen);
        if (k < 0) k += static_cast<long>(plen);
        return static_cast<double>(v[s + static_cast<size_t>(k)]);
    };
    double lo = at(-rad), hi = lo;
    for (long k = -rad; k <= rad; k++) {
        const double x = at(k);
        lo = std::min(lo, x);
        hi = std::max(hi, x);
    }
    if (hi - lo < 0.1) return approx;  // no edge the crossing can name
    const double mid = (lo + hi) / 2.0;
    double best_off = approx - c;
    double best_abs = static_cast<double>(rad) + 1.0;
    for (long k = -rad; k < rad; k++) {
        const double x0 = at(k), x1 = at(k + 1);
        if (x0 < mid && x1 >= mid && x1 > x0) {
            const double off = k + (mid - x0) / (x1 - x0);
            if (std::fabs(off) < best_abs) {
                best_abs = std::fabs(off);
                best_off = off;
            }
        }
    }
    if (best_abs > rad) return approx;
    double out = std::fmod(c + best_off, static_cast<double>(plen));
    if (out < 0.0) out += plen;
    return out;
}

}  // namespace

PhasingResult detect_phasing(const std::vector<float>& video, int fs,
                             double period, const PhasingOptions& opt,
                             const DecodeHooks& hooks) {
    PhasingResult res;
    const size_t plen = static_cast<size_t>(period);
    if (plen < 16 || video.size() < plen * 2) return res;
    const size_t n_lines = video.size() / plen;

    // Both waveforms WMO §5.2.3.2 permits. Whichever fits better per line
    // wins; the answer is reported, not configured.
    const size_t w_asym = static_cast<size_t>(0.05 * period);
    const size_t w_sym = static_cast<size_t>(0.50 * period);

    std::vector<double> pos(n_lines, 0.0), score(n_lines, -1.0);
    std::vector<char> asym(n_lines, 1);
    for (size_t l = 0; l < n_lines; l++) {
        if ((l & 63) == 0) throw_if_cancelled(hooks, "phasing");
        const Fit a = wedge_fit(video, l * plen, plen, w_asym);
        const Fit s = wedge_fit(video, l * plen, plen, w_sym);
        if (a.score >= s.score) {
            pos[l] = refine_leading_edge(video, l * plen, plen, a.pos);
            score[l] = a.score;
            asym[l] = 1;
        } else {
            pos[l] = refine_leading_edge(video, l * plen, plen, s.pos);
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
    const double tol0 = opt.max_spread_frac * period;
    size_t i = 0;
    while (i < n_lines) {
        if (score[i] < opt.min_score) { i++; continue; }

        // Grow the run from this seed. A line joins if it scores well OR if
        // it puts the white where the run already agrees the white is —
        // either witness is enough, and on a faded signal only the second
        // one survives. Every member is one of the two, so a line that is
        // merely NEAR in time cannot enter and pollute the spread test that
        // judges the run afterwards; the run simply ends after `max_gap`
        // consecutive non-members.
        //
        // The comparison is against the running median of the members so
        // far, which starts as the seed's own position: the first few
        // members are therefore judged against a single line, and the
        // reference gets more robust as the run grows. Judging them against
        // the whole run's median instead would need the run first.
        std::vector<double> p, sc;
        std::vector<size_t> mem;
        p.push_back(pos[i]);
        sc.push_back(score[i]);
        mem.push_back(i);
        double med_run = pos[i];
        size_t last = i;
        int gap = 0;
        for (size_t l = i + 1; l < n_lines; l++) {
            const bool strong = score[l] >= opt.min_score;
            const bool aligned = circ_dist(pos[l], med_run, period) <= tol0;
            // The lease is renewed by SCORE alone. An aligned weak line is
            // taken into the run but does not extend how much further the
            // run may reach without fresh evidence — see `max_gap`.
            if (strong)
                gap = 0;
            else if (++gap > opt.max_gap)
                break;
            if (!strong && !aligned) continue;
            std::vector<double> q = p;
            q.push_back(pos[l]);
            unwrap_about(q, med_run, period);
            p = q;
            sc.push_back(score[l]);
            mem.push_back(l);
            med_run = median_of(p);
            last = l;
        }
        // Where the next candidate starts, fixed before the trim below can
        // move `last` — otherwise the trimmed-off tail is rescanned as a run
        // of its own.
        const size_t next = last + 1;

        // A run must END on a line that is phasing on its own evidence, not
        // on one that merely agrees with where the run has been. Carrying on
        // position is there to cross a fade INSIDE the interval; at the end
        // of the interval there is nothing on the far side to reconnect to,
        // so the same rule extrapolates instead of bridging.
        //
        // On a WHITE-ONLY station that is not a subtlety, it is the whole
        // interval: the dead sector is white and sits at the same place in
        // the line as the phasing wedge [WMO §5.2.3.4], so every image line
        // agrees with the phasing position for the rest of the recording.
        // Measured on the generated white-only signal (session 10), the run
        // grew from 30 phasing lines to 230 and ran off the end of the
        // picture, and the duration cap then threw the whole thing away —
        // which is how a screamer that had passed since session 9 started
        // failing. Score cannot be asked to draw this boundary either: those
        // image lines score 0.332 and GYA 2300Z's faintest REAL phasing line
        // scores 0.371.
        while (!mem.empty() && sc.back() < opt.min_score) {
            mem.pop_back();
            sc.pop_back();
            p.pop_back();
        }
        if (mem.empty()) { i = next; continue; }
        size_t j = mem.back();
        i = mem.front();
        unwrap_about(p, median_of(p), period);

        // Trim the ENDS back to lines that agree on where the white is.
        // A run is seeded and extended on score too, and score alone lets a
        // dark picture line adjacent to the interval join it: the 10-90% spread
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
                mem = std::vector<size_t>(mem.begin() + lo, mem.begin() + hi);
                if (!mem.empty()) {
                    j = mem.back();
                    i = mem.front();
                }
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
        //
        // The x axis is the LINE NUMBER, not the member index. The two are
        // the same only when every line in the span is a member; a faded run
        // has gaps, and dividing by the wrong baseline would report a slope
        // — and therefore a residual — that no clock explains.
        double nonlin = 0.0;
        int n_steps = 0;
        double meas_period = 0.0;
        std::vector<double> resid, smoothed;
        if (len >= 8) {
            const size_t k = len / 2;
            std::vector<double> sl;
            for (size_t m = 0; m + k < len; m++)
                sl.push_back((p[m + k] - p[m]) /
                             static_cast<double>(mem[m + k] - mem[m]));
            const double slope = median_of(sl);
            // The white edge drifts by (true period - truncated period)
            // per line inside its window, so the slope IS the rate
            // measurement the live path seeds from [PhasingResult::period].
            meas_period = plen + slope;
            std::vector<double> icpt;
            for (size_t m = 0; m < len; m++)
                icpt.push_back(p[m] - slope * static_cast<double>(mem[m]));
            const double c = median_of(icpt);
            for (size_t m = 0; m < len; m++)
                resid.push_back(p[m] -
                                (c + slope * static_cast<double>(mem[m])));
            smoothed = smooth_local(resid, mem, kNonlinMedRad);
            nonlin = spread_10_90(smoothed);
            // Persistent moves, counted the way the image domain counts them
            // (fax.cpp §4d): a transition of more than `kStepSmp` between
            // ADJACENT lines of the smoothed residual. Adjacent only — across
            // a gap the two lines are several lines apart and the clock walk
            // between them is not a step.
            for (size_t m = 1; m < smoothed.size(); m++)
                if (mem[m] == mem[m - 1] + 1 &&
                    std::fabs(smoothed[m] - smoothed[m - 1]) > kStepSmp)
                    n_steps++;
        }
        int n_asym = 0;
        for (size_t m = 0; m < len; m++) n_asym += asym[mem[m]];
        const double med = median_of(p);
        const double sp = spread_10_90(p);

        // Line-to-line roughness of the residual: how much of its spread is
        // measurement noise rather than anything persistent.
        double rough = 0.0;
        {
            std::vector<double> d;
            for (size_t m = 1; m < resid.size(); m++)
                if (mem[m] == mem[m - 1] + 1)
                    d.push_back(std::fabs(resid[m] - resid[m - 1]));
            if (!d.empty()) rough = median_of(d);
        }

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
        // The reference is the MEDIAN MEMBER, which is a line that exists
        // even when the run has gaps in it; the midpoint of the span need
        // not be a member at all on a faded signal.
        const double l_mid = static_cast<double>(mem[(len - 1) / 2]);
        std::vector<double> abs_est;
        abs_est.reserve(len);
        for (size_t k = 0; k < len; k++) {
            const double l = static_cast<double>(mem[k]);
            abs_est.push_back(l * plen + p[k] - (l - l_mid) * period);
        }
        unwrap_about(abs_est, median_of(abs_est), period);
        const double anchor = median_of(abs_est);
        dlog(hooks, LogTopic::kInfo,
             "dbg: phasing cand %zu lines of %zu @line %zu "
             "pos=%.1f spread=%.1f (limit %.1f) score=%.3f",
             len, j - i + 1, i, med, sp,
             opt.max_spread_frac * period, median_of(sc));
        for (size_t k = 0; k < p.size(); k++)
            if (k < 3 || k + 3 >= p.size())
                dlog(hooks, LogTopic::kDetail,
                     "dbg:   line %zu pos=%.1f (%+.1f) score=%.3f",
                     mem[k], p[k], p[k] - med, sc[k]);

        // The spread test is what stops a stretch of dark picture lines
        // from passing: they can each score well, but they do not agree on
        // WHERE the white run is, and phasing lines do.
        // `max_lines` is a duration falsification — phasing is ~30 s of the
        // transmission, so it bounds the SPAN, not the member count. A run
        // that covers eight minutes is not phasing however few of those
        // lines agreed.
        const bool ok = len >= static_cast<size_t>(opt.min_lines) &&
                        (j - i + 1) <= max_lines &&
                        sp <= opt.max_spread_frac * period;
        if (ok)
            dlog(hooks, LogTopic::kInfo,
                 "dbg:   -> qualifies: nonlin=%.1f (raw %.1f) "
                 "noise=%.1f steps=%d",
                 nonlin, spread_10_90(resid), rough, n_steps);
        // Which qualifying run wins: the LAST one inside the transmission
        // the caller named, or the FIRST one when it named none. Never the
        // longest — see PhasingOptions::t_lo. Segmentation states the same
        // policy three sections down in fax.cpp ("the first one after the
        // previous boundary, never the last or the largest"); the phasing
        // detector did not share it until session 10, and the right answer
        // had been winning a one-line coin toss on `jmh sample`.
        const double run_t0 = static_cast<double>(i * plen) / fs;
        const double run_t1 = static_cast<double>((j + 1) * plen) / fs;
        const bool windowed = opt.t_hi > opt.t_lo;
        const bool inside =
            !windowed || (run_t0 >= opt.t_lo - 1.0 && run_t1 <= opt.t_hi);
        if (ok && inside && (windowed || !res.found)) {
            res.found = true;
            res.t_start = static_cast<double>(i * plen) / fs;
            res.t_end = static_cast<double>((j + 1) * plen) / fs;
            res.lines = static_cast<int>(len);
            res.line_start = std::fmod(med + period, period);
            res.anchor = anchor;
            res.spread = sp;
            res.period = meas_period;
            res.nonlinearity = nonlin;
            res.roughness = rough;
            res.steps = n_steps;
            res.asymmetric = n_asym * 2 >= static_cast<int>(len);
            res.score = median_of(sc);
        }
        i = next;
    }
    return res;
}

}  // namespace nova
