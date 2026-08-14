// fax.cpp
#include "fax.hpp"
#include "phasing.hpp"
#include "tones.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace nova {
namespace {

// The line layout, the level slices and the two thresholds now live in
// core/fax.hpp, because the live preview renderer draws against the same
// template [docs/05 §6]. The measurement behind each one moved with it;
// these aliases keep the batch code below reading exactly as it did.
constexpr double kDeadFrac = kFaxDeadFrac;
constexpr double kPulseFrac = kFaxPulseFrac;
constexpr double kDarkLevel = kFaxDarkLevel;
constexpr double kWhiteLevel = kFaxWhiteLevel;
constexpr double kPulseConsistency = kFaxPulseConsistency;
constexpr double kPulseLock = kFaxPulseLock;
// Line layout measured on the JMH test chart (SESSION-LOG 2026-08-12
// session 3): 7.5 ms sync pulse, 10.5 ms white gap, 474 ms picture,
// then a ~8 ms black porch before the next pulse — the WMO §5.1.3.3
// dead sector is split around the line boundary. The image maps the
// FULL line starting at the sync pulse (576*pi px at IOC 576 is the
// full line, dead sector included; neither WMO nor ISO asks for any
// cropping), so pulse/gap/picture/porch all render truthfully.
constexpr double kPi = 3.14159265358979323846;

constexpr int kReacqMisses = kFaxReacqMisses;
constexpr int kReacqEvery = kFaxReacqEvery;
constexpr double kReacqStep = kFaxReacqStep;
// Half-width of the local median applied to the sync residual, in lines.
// Used twice: by the assembly, to correct the line it draws, and by the
// timebase test, which is asking whether that same correction moves in
// steps. One constant so the two cannot drift apart.
//
// Session 11: they are no longer identical, and the difference is the
// point. The test still smooths a flat ±kMedRad window, because it is a
// calibrated instrument (session 9's library thresholds were measured
// through it and a change to it re-opens every one of them). The ASSEMBLY
// now truncates the same window at a change point, because a smoother
// cannot be a corrector: it lags every real move by up to kMedRad lines.
// Measured on the residuals the decoder itself produced: on JSC1 the flat
// window leaves 4.21 px of line-start error against 1.89 px truncated,
// and Sara's review of the library found exactly those recordings — "the
// black strip, it's actually zig zagging".
constexpr int kMedRad = 8;
// How many locked lines on each side have to agree before the assembly
// believes the line start really moved. Four: enough that one bad template
// match cannot open a seam (the median of four rejects it), few enough that
// JSC's steps every ~11 lines are still resolved as separate moves.
constexpr int kSegHalf = 4;
// Smallest mid-line timebase move worth looking for a break point for, in
// samples. Below this the displacement is under a pixel of 1810 either way
// and the search has nothing to find.
constexpr double kIntraMin = 4.0;
// How far either way the picture may place a row that a dropout left with
// nothing to place it, in pixels of the drawn width.
constexpr int kCoastSearchPx = 120;
// Timebase linearity [§4d]. A step is a persistent move of the smoothed
// sync residual bigger than kStepSec; a recording is called non-linear
// above kStepRateLimit such steps per 1000 drawn lines, or when the
// phasing edge deviates from a straight line by more than kNonlinSec.
// All three sit mid-gap in the library measurement (session 9): step rate
// 0.0-7.0 clean against 64.8-339.8 on the six JSC recordings; phasing
// non-linearity 1.0-3.8 samples clean against 20.2-25.5. In seconds
// because the quantity being measured is inserted or dropped SAMPLES,
// which is a property of the capture chain and not of the line rate.
constexpr double kStepSec = 0.25e-3;    // 2 samples at 8 kHz
constexpr double kNonlinSec = 1.25e-3;  // 10 samples at 8 kHz
constexpr double kStepRateLimit = 20.0;
// ...and in the phasing domain, how many persistent moves make a RATE
// rather than a single skip. Session 9 settled the principle on the image
// side and session 10 applies it here: JMH KiwiSDR Himawari's phasing
// interval contains exactly one ~95-sample jump, JSC2 and JSC3 contain 16
// and 17. Two is the smallest number that is not one.
constexpr int kMinPhasingSteps = 2;
// Below this many drawn lines the rate is too noisy to convict on: at the
// clean-recording rate a 64-line window sees a step about once in three
// recordings, and one step in 64 lines already reads 15.6/1000.
constexpr size_t kStepMinLines = 128;

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

// `fax_lerp_at`, `fax_pulse_score` and `fax_best_sync` are declared in
// core/fax.hpp and defined below the anonymous namespace; only `mean_over`,
// which nothing outside this file needs, stays private.
inline double mean_over(const std::vector<float>& v, double from, double len) {
    double acc = 0.0;
    int n = 0;
    for (double x = from; x < from + len; x += 1.0, n++) acc += fax_lerp_at(v, x);
    return n ? acc / n : 0.0;
}

double median(std::vector<double> x) {
    if (x.empty()) return 0.0;
    std::nth_element(x.begin(), x.begin() + x.size() / 2, x.end());
    return x[x.size() / 2];
}

// --- fine period by folded-block phase drift ------------------------------
// `best_period` above runs on 200 Hz video, where ONE LAG STEP IS 1% of the
// line — 10 000 ppm. Everything finer comes from a parabolic vertex over
// three autocorrelation samples, and that interpolation is biased: measured
// on synthetic signals (session 5), it recovers only ~75% of the true clock
// error (+400 ppm reads +300, +137 reads +108, -213 reads -158) identically
// at noise 0.0 and 0.3 — quantization, not noise.
//
// On a station that sends the black pulse this barely matters: pass B refits
// the period from the locks. On a white-only station there are no locks, the
// coarse number IS the drawn period, and the missing quarter slants the
// picture (GYA 2300Z, VMW, NMC — all white-only).
//
// So measure the period where its error actually shows up: as phase drift
// ACROSS THE RECORDING. Fold each block of lines into one profile (JWX's
// clock-corrected accumulation idea, docs/00 — folding lifts a stable line
// shape out of a fading signal; JWX's own clock value is hand-entered by the
// operator, so only the accumulation is reusable), cross-correlate
// consecutive profiles for the phase walk, and take the median pairwise
// slope (the weatherfax_pi/KiwiSDR median-over-lines robustness). Precision
// scales with the BASELINE, not with lag resolution: a ±2 sample profile
// alignment over 1200 lines is 0.4 ppm.
constexpr int kFoldMinLines = 40;    // lines per block; fewer will not fold
constexpr int kFoldMaxBlocks = 16;
constexpr double kFoldMinPeak = 0.20;  // normalized correlation of a pair
// Spread rejection: how far one block pair may sit from the median shift
// before it is treated as a picture restart rather than as clock drift.
// 2% of a line tolerates ~200 ppm of block-to-block wander; a restart moves
// the paper by a large fraction of a whole line.
constexpr double kFoldSpread = 0.02;
constexpr int kFoldPasses = 3;

// Normalized circular cross-correlation of two mean-removed profiles over
// lags in ±maxlag. Returns the shift of `b` relative to `a` (positive =
// b's features sit LATER in the line, i.e. the true period is longer than
// the one they were folded on), with the peak value in *peak.
double fold_shift(const std::vector<double>& a, const std::vector<double>& b,
                  int maxlag, double* peak) {
    const int plen = static_cast<int>(a.size());
    double na = 0.0, nb = 0.0;
    for (int i = 0; i < plen; i++) {
        na += a[i] * a[i];
        nb += b[i] * b[i];
    }
    const double nrm = std::sqrt(na * nb);
    if (nrm <= 0.0) {
        *peak = 0.0;
        return 0.0;
    }
    double best = -1e300;
    int best_lag = 0;
    std::vector<double> c(2 * maxlag + 1, 0.0);
    for (int lag = -maxlag; lag <= maxlag; lag++) {
        double s = 0.0;
        for (int i = 0; i < plen; i++)
            s += a[i] * b[((i + lag) % plen + plen) % plen];
        c[lag + maxlag] = s / nrm;
        if (c[lag + maxlag] > best) {
            best = c[lag + maxlag];
            best_lag = lag;
        }
    }
    *peak = best;
    // A winner pinned to the edge of the search means the drift is larger
    // than the window: report it as a miss rather than as a measurement.
    if (best_lag <= -maxlag + 1 || best_lag >= maxlag - 1) {
        *peak = 0.0;
        return 0.0;
    }
    const double y0 = c[best_lag + maxlag - 1], y1 = c[best_lag + maxlag],
                 y2 = c[best_lag + maxlag + 1];
    const double den = y0 - 2.0 * y1 + y2;
    double d = (den < 0.0) ? 0.5 * (y0 - y2) / den : 0.0;
    d = std::max(-1.0, std::min(1.0, d));
    return best_lag + d;
}

double refine_period(const std::vector<float>& v, size_t start, double period,
                     int n_lines, const DecodeHooks& hooks) {
    const int nb = std::min(kFoldMaxBlocks, n_lines / kFoldMinLines);
    if (nb < 3) return period;  // too short to see drift; coarse is all there is
    const int per_block = n_lines / nb;

    for (int pass = 0; pass < kFoldPasses; pass++) {
        const int plen = static_cast<int>(period);
        // Lag window: ±plen/8 covers ~1500 ppm of drift per block at any
        // usable block length, and keeps the O(plen * lag) correlation cheap.
        const int maxlag = std::max(4, plen / 8);
        std::vector<std::vector<double>> prof(
            nb, std::vector<double>(plen, 0.0));
        for (int b = 0; b < nb; b++) {
            for (int l = 0; l < per_block; l++) {
                const double base =
                    start + (static_cast<double>(b) * per_block + l) * period;
                for (int i = 0; i < plen; i++)
                    prof[b][i] += fax_lerp_at(v, base + i);
            }
            double m = 0.0;
            for (int i = 0; i < plen; i++) m += prof[b][i];
            m /= plen;
            for (int i = 0; i < plen; i++) prof[b][i] -= m;
        }

        // Consecutive-block shifts first, then a robust centre. A long
        // recording is not one picture: each new chart restarts the paper at
        // its own phase, and that step (up to half a line) is not clock
        // drift. Measured (session 5): accumulating it blind put JSC2 at
        // +180 ppm against pass B's -73. True drift is the same in every
        // block, so the median of the consecutive shifts is it, and pairs
        // that disagree with the median by more than a whole picture's worth
        // of jitter are restarts, not measurements — median with spread
        // rejection, the weatherfax_pi/KiwiSDR treatment of the phasing
        // wedge (docs/00) applied to blocks instead of lines.
        std::vector<double> sh(nb, 0.0);
        std::vector<bool> good(nb, false);
        for (int b = 1; b < nb; b++) {
            double peak = 0.0;
            sh[b] = fold_shift(prof[b - 1], prof[b], maxlag, &peak);
            good[b] = peak >= kFoldMinPeak;
        }
        std::vector<double> gs;
        for (int b = 1; b < nb; b++)
            if (good[b]) gs.push_back(sh[b]);
        if (gs.empty()) return period;
        const double centre = median(gs);
        const double tol = std::max(8.0, kFoldSpread * plen);
        for (int b = 1; b < nb; b++)
            if (good[b] && std::fabs(sh[b] - centre) > tol) good[b] = false;

        // Phase walk, accumulated inside runs of surviving pairs. A fade or
        // a restart must not invalidate the blocks after it, and must not
        // have its unmeasured drift folded into the total: a rejected pair
        // starts a new segment, and only within-segment pairs contribute
        // slopes. The long baselines inside each segment are what make this
        // precise; the segmentation is what keeps it honest.
        std::vector<double> phase(nb, 0.0);
        std::vector<int> seg(nb, 0);
        for (int b = 1; b < nb; b++) {
            if (!good[b]) {
                seg[b] = seg[b - 1] + 1;
                phase[b] = 0.0;
            } else {
                seg[b] = seg[b - 1];
                phase[b] = phase[b - 1] + sh[b];
            }
        }
        if (hooks.log)
            for (int b = 1; b < nb; b++)
                dlog(hooks, LogTopic::kFold, "dbg: fold blk %2d shift %+9.2f %s",
                     b, sh[b], good[b] ? "" : "REJECT");
        std::vector<double> slopes;
        for (int i = 0; i < nb; i++)
            for (int j = i + 1; j < nb; j++)
                if (seg[i] == seg[j])
                    slopes.push_back((phase[j] - phase[i]) / (j - i));
        if (slopes.empty()) return period;
        const double drift = median(slopes);  // samples per block
        const double next = period + drift / per_block;
        dlog(hooks, LogTopic::kInfo,
             "dbg: fold pass %d: %d blocks x %d lines, drift "
             "%+.2f samples/block -> period %.4f (%+.1f ppm)",
             pass, nb, per_block, drift, next, (next / period - 1.0) * 1e6);
        // A "correction" of more than 3% is not a clock error, it is a
        // misfolded signal; keep the coarse period rather than invent one.
        if (std::fabs(next - period) > 0.03 * period) return period;
        period = next;
    }
    return period;
}

}  // namespace

// --- the shared sync template [core/fax.hpp] ------------------------------
// Public since session 21 so the live preview renderer locks onto the same
// feature this file does. The bodies are unchanged from when they were
// private; the comments justifying them moved to the header, where the new
// caller reads them.

float fax_lerp_at(const std::vector<float>& v, double pos) {
    if (pos < 0.0) return v.front();
    if (pos >= v.size() - 1) return v.back();
    const size_t i = static_cast<size_t>(pos);
    const double f = pos - i;
    return static_cast<float>(v[i] * (1.0 - f) + v[i + 1] * f);
}

double fax_pulse_score(const std::vector<float>& v, double p, double pulse) {
    return mean_over(v, p + pulse, pulse) - mean_over(v, p, pulse);
}

double fax_best_sync(const std::vector<float>& v, double lo, double hi,
                     double pulse, double* score, double step) {
    if (hi <= lo) {  // window clamped out of existence at the file edges
        if (score) *score = -1e300;
        return lo;
    }
    double best_s = -1e300, best_p = lo;
    for (double p = lo; p < hi; p += step) {
        const double s = fax_pulse_score(v, p, pulse);
        if (s > best_s) {
            best_s = s;
            best_p = p;
        }
    }
    if (step > 1.0) {
        const double f0 = std::max(lo, best_p - step);
        const double f1 = std::min(hi, best_p + step);
        for (double p = f0; p < f1; p += 1.0) {
            const double s = fax_pulse_score(v, p, pulse);
            if (s > best_s) {
                best_s = s;
                best_p = p;
            }
        }
    }
    // Sub-sample vertex, but only where there really is one. `denom != 0`
    // is not enough: at a coarse-scan winner the neighbours need not
    // bracket a maximum, and the vertex formula then throws the position
    // arbitrarily far — measured on FAXSignal, where one such jump moved a
    // line by a quarter million samples, poisoned the median intercept and
    // left the whole file at 59 locks of 2192.
    const double sm = fax_pulse_score(v, best_p - 1, pulse);
    const double sp = fax_pulse_score(v, best_p + 1, pulse);
    const double denom = sm - 2.0 * best_s + sp;
    if (denom < 0.0) {
        const double d = 0.5 * (sm - sp) / denom;
        best_p += std::max(-1.0, std::min(1.0, d));
    }
    if (score) *score = best_s;
    return best_p;
}

// --- decode_fax: state and stages -----------------------------------------
// decode_fax is a fixed pipeline of the numbered stages below, run in
// order. The split is a seam, not a reorganization: each stage function is
// the section the single function used to carry inline, with the same
// logic, the same constants and the same comments. What the split buys is
// the M4 surface (docs/03): progress is reported at stage boundaries and
// inside the long loops, cancellation is checked there too, and a future
// incremental pipeline can drive stages rather than one monolith.
namespace {

// Everything a stage leaves for the next one. Only cross-stage values live
// here; anything one stage uses internally stays a local of that stage.
struct DecodeState {
    DecodeState(const std::vector<float>& v, int f, const DecodeOptions& o)
        : video(v), fs(f), opt(o), hooks(o.hooks) {}

    const std::vector<float>& video;
    const int fs;
    const DecodeOptions& opt;
    const DecodeHooks& hooks;
    DecodeResult res;

    // 1. onset + period
    int lpm = 0;
    size_t start = 0;   // signal onset, in samples
    double nominal = 0.0;  // nominal line length, samples
    double period0 = 0.0;  // refined coarse line length, samples
    double pulse = 0.0;    // sync pulse width, samples
    double dead = 0.0;     // dead sector width, samples
    int n_lines = 0;

    // 2. dead-sector style + coarse anchor
    int plen = 0;
    bool has_pulse = false;
    double dead_start0 = 0.0;

    // 2b. control tones (shared with segmentation) and 3/4. the track
    std::vector<ToneEvent> tones;
    const double lock = kPulseLock;
    std::vector<double> spos, sstr;

    // 4. pass B fit
    double a = 0.0, b = 0.0;

    // 4c. segmentation
    int line_lo = 0, line_hi = 0;

    // 4e. change points
    std::vector<char> cpoint;
};

// --- 1. signal onset + coarse line rate -----------------------------------
void stage_onset(DecodeState& st) {
    const DecodeOptions& opt = st.opt;
    DecodeResult& res = st.res;
    const std::vector<float>& video = st.video;
    const int fs = st.fs;

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
        throw DecodeError(DecodeErrorKind::kTooShort,
                          "decode_fax: recording too short");

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
        throw_if_cancelled(st.hooks, "onset");
        Win w{s, {0.0, 0.0, 0.0}};
        for (size_t ri = 0; ri < 3; ri++) {
            if (opt.lpm != 0 && kRates[ri] != opt.lpm) continue;
            w.score[ri] = comb_score(v200, s, wlen, kRates[ri] / 60.0);
            file_max = std::max(file_max, w.score[ri]);
        }
        wins.push_back(w);
        dlog(st.hooks, LogTopic::kInfo,
             "dbg: comb win@%.1fs 60=%.3f 90=%.3f 120=%.3f",
             s / 200.0, w.score[0], w.score[1], w.score[2]);
        report(st.hooks, "onset",
               static_cast<double>(s - start200) /
                   std::max<size_t>(1, v200.size() - wlen));
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
        throw DecodeError(DecodeErrorKind::kNoSignal,
                          "decode_fax: no fax line comb found (fill or no "
                          "signal)");

    // Period: autocorrelation over everything from onset to EOF (the
    // onset gate has already excluded fill, the historical bias source).
    // Precision matters for weak signals where pass B cannot engage
    // (e.g. ±150 Hz LF deviation, whose sync template never reaches the
    // lock threshold): a 15 s window is not precise enough on its own.
    const double nom200 = 200.0 * 60.0 / lpm;
    std::vector<float> pwin(v200.begin() + onset200, v200.end());
    const double period_200 =
        best_period(pwin, nom200 * 0.97, nom200 * 1.03);
    dlog(st.hooks, LogTopic::kInfo,
         "dbg: onset=%.1fs lpm=%d coarse=%.5f lpm (%+.1f ppm)",
         onset200 / 200.0, lpm, 60.0 * 200.0 / period_200,
         (lpm * period_200 / (200.0 * 60.0) - 1.0) * 1e6);
    res.lpm = lpm;
    st.lpm = lpm;

    st.start = onset200 * static_cast<size_t>(fs) / 200;
    st.nominal = fs * 60.0 / lpm;

    // Refine the coarse period against the whole recording before anything
    // is measured on it. The coarse fit is off by 30-180 ppm on real
    // signals (session 5, measured against pass B across the library: JSC6
    // +261 coarse vs +438 fitted, XSG ASPN +26 vs -90). A pulse station
    // survives that because pass B refits from its locks; a white-only
    // station draws on it directly and slants by exactly that error.
    const size_t avail = video.size() - st.start;
    int n_lines0 = static_cast<int>(avail / (period_200 * fs / 200.0));
    if (opt.max_lines > 0) n_lines0 = std::min(n_lines0, opt.max_lines);
    const double period0 = refine_period(video, st.start,
                                         period_200 * fs / 200.0, n_lines0,
                                         st.hooks);
    dlog(st.hooks, LogTopic::kInfo, "dbg: period refined to %.4f (%+.1f ppm)",
         period0, (period0 / st.nominal - 1.0) * 1e6);
    st.period0 = period0;

    st.pulse = kPulseFrac * period0;
    st.dead = kDeadFrac * period0;

    int n_lines = static_cast<int>(avail / period0);
    if (opt.max_lines > 0) n_lines = std::min(n_lines, opt.max_lines);
    if (n_lines < 4)
        throw DecodeError(DecodeErrorKind::kTooFewLines,
                          "decode_fax: too few lines");
    st.n_lines = n_lines;
}

// --- 2. coarse phase + dead-sector style, from across-line consistency ----
void stage_dead_sector(DecodeState& st) {
    const DecodeOptions& opt = st.opt;
    DecodeResult& res = st.res;
    const std::vector<float>& video = st.video;
    const double period0 = st.period0;
    const int n_lines = st.n_lines;

    // The dead sector is the one part of the line that looks the same on
    // EVERY line [WMO §5.1.3.3]. Picture content does not: a chart border
    // is dark on many lines, never on all of them. So the anchor is found
    // by counting, per position, the FRACTION of lines that are dark (or
    // white) there — not by the average contrast of a fold, which picks
    // whatever has the strongest mean edge and is routinely content.
    // Measured (session 4): on FAXSignal the fold anchor landed 211 samples
    // off a black pulse present on 98% of lines — outside pass A's ±120
    // sample search — so the file locked 65 of 2192 lines while carrying a
    // textbook sync pulse. Same failure on jmh sample, test chart, XSG
    // ASPN, JSC2, NMC, HDSDR.
    const int plen = static_cast<int>(period0);
    st.plen = plen;
    // Profile length is bounded by clock smear, not by taste: the profile
    // is stacked on the coarse period, so a relative period error e spreads
    // the pulse by prof_lines*period0*e. The autocorrelation period is good
    // to ~1e-4 (himawari: 3999.90 coarse vs 3999.64 fitted), which at 400
    // lines is 104 samples — wider than the pulse itself, and measurably
    // enough to bias the anchor. 120 lines keeps the smear inside a third
    // of a pulse width; pass A's re-acquisition covers the rest.
    // Skip the phasing stage first. Phasing is ~30 s of alternating
    // black/white [WMO §5.2.3.1] whose white edge sits half a dead sector
    // away from where the image lines put theirs [WMO §5.2.3.4], so those
    // lines agree with nothing and drag both consistency profiles toward
    // the middle — measured: with phasing included every station in the
    // library scored 0.51-0.63 and the style decision became a coin toss.
    // The onset gate anchors on the line comb, and phasing has one, so
    // phasing is exactly what onset lands on when a station sends it.
    const int prof0 =
        std::min(n_lines / 4, static_cast<int>(30.0 * st.lpm / 60.0));
    const int prof_lines = std::min(n_lines - prof0, 120);
    std::vector<double> dark_frac(plen, 0.0), white_frac(plen, 0.0);
    for (int l = 0; l < prof_lines; l++) {
        throw_if_cancelled(st.hooks, "dead-sector");
        const double base = st.start + (prof0 + l) * period0;
        for (int i = 0; i < plen; i++) {
            const float x = fax_lerp_at(video, base + i);
            if (x < kDarkLevel) dark_frac[i] += 1.0;
            if (x > kWhiteLevel) white_frac[i] += 1.0;
        }
    }
    for (int i = 0; i < plen; i++) {
        dark_frac[i] /= prof_lines;
        white_frac[i] /= prof_lines;
    }

    // Wrapped window mean of a profile; the dead sector straddles the line
    // boundary, so every window here wraps.
    auto win_mean = [&](const std::vector<double>& f, int at, int win) {
        double s = 0.0;
        for (int j = 0; j < win; j++) s += f[((at + j) % plen + plen) % plen];
        return s / win;
    };
    const int pulse_w = std::max(2, static_cast<int>(kPulseFrac * plen));
    const int dead_w = std::max(2, static_cast<int>(kDeadFrac * plen));

    // Both anchors score a SHAPE, not a level. Level alone is not enough:
    // a full-disk satellite image carries black space at both line margins,
    // dark on 100% of lines over hundreds of samples, so "darkest window"
    // lands anywhere inside that band (measured: FAXSignal and himawari,
    // session 4). What identifies the pulse is that black is followed
    // immediately by white [WMO §5.1.3.3] — so score the pair, and take the
    // weaker half, which the black band cannot fake.
    double pulse_shape = -1.0, white_shape = -1.0;
    int pulse_at = 0, white_at = 0;
    for (int i = 0; i < plen; i++) {
        const double s = std::min(win_mean(dark_frac, i, pulse_w),
                                  win_mean(white_frac, i + pulse_w, pulse_w));
        if (s > pulse_shape) {
            pulse_shape = s;
            pulse_at = i;
        }
        // White-only: the anchor is the entry into the dead sector — where
        // consistent whiteness starts [WMO §5.2.3.4 puts the phasing
        // reference at exactly that edge]. A rising edge in white-fraction
        // survives the run being wider than the dead sector, which happens
        // whenever the chart also has a white margin (VMW 2215Z: 45 ms of
        // always-white for a 22.5 ms dead sector).
        const double e = win_mean(white_frac, i, dead_w) -
                         win_mean(white_frac, i - dead_w, dead_w);
        if (e > white_shape) {
            white_shape = e;
            white_at = i;
        }
    }
    const double pulse_cons = win_mean(dark_frac, pulse_at, pulse_w);
    const double white_cons = win_mean(white_frac, white_at, dead_w);
    if (st.hooks.log)
        for (int i = 0; i < plen; i += std::max(1, plen / 200))
            dlog(st.hooks, LogTopic::kProfile, "prof %5d dark=%.2f white=%.2f",
                 i, dark_frac[i], white_frac[i]);

    // The pulse is optional. Take it when the station really sends one,
    // because it is by far the stronger template; otherwise anchor on the
    // always-white dead sector itself.
    const bool has_pulse = pulse_shape >= kPulseConsistency;
    res.dead_sector =
        has_pulse ? DeadSector::kBlackPulse : DeadSector::kWhiteOnly;
    res.dead_consistency = has_pulse ? pulse_cons : white_cons;
    double dead_start0 = has_pulse ? pulse_at : white_at;

    // --- the operator's PHASE: seed the search, then refine [docs/05 §7.1]
    // The scan above is global, and that is exactly what fails when it
    // fails: it takes the strongest candidate in the whole line, and on the
    // recordings that need this field the strongest candidate is not the
    // dead sector. So run the SAME score again over a window around the
    // operator's hint and take the best position there. What that buys is
    // the split the decision is built on — the operator says which feature,
    // the profile says where in it — and it costs nothing when the hint is
    // already right, because the same score has the same maximum.
    //
    // The window is `search_frac` (±3% of a line, ±120 samples on a 4000-
    // sample line), which is not a new constant: it is the same latitude
    // the per-line tracker is given below for the same quantity, how far
    // from where we think it is the line start may actually be. A wider one
    // would let the global winner back in and swallow the hint, which is
    // the one thing this field exists to prevent.
    //
    // The STYLE decision above is deliberately left upstream of this, on
    // the global scan. Which of the two dead-sector styles a station sends
    // [WMO §5.1.3.3] is a property of the transmission measured across
    // every line; a click is about position. Refining the style here too
    // would let a hint pointed at a white feature on a pulse station turn
    // `per_line_sync` off and silently disable the tracker — a click with a
    // consequence the operator did not ask for and cannot see.
    if (opt.phase_anchor_hint >= 0.0) {
        const double frac = std::fmod(opt.phase_anchor_hint, 1.0);
        const int hint_at = static_cast<int>(frac * plen);
        const int win = std::max(2, static_cast<int>(opt.search_frac * plen));
        auto score_at = [&](int i) {
            return has_pulse
                       ? std::min(win_mean(dark_frac, i, pulse_w),
                                  win_mean(white_frac, i + pulse_w, pulse_w))
                       : win_mean(white_frac, i, dead_w) -
                             win_mean(white_frac, i - dead_w, dead_w);
        };
        double best = -2.0;
        int best_at = hint_at;
        for (int i = hint_at - win; i <= hint_at + win; i++) {
            const double s = score_at(i);
            if (s > best) {
                best = s;
                best_at = i;
            }
        }
        dead_start0 = ((best_at % plen) + plen) % plen;
        res.dead_consistency =
            has_pulse ? win_mean(dark_frac, best_at, pulse_w)
                      : win_mean(white_frac, best_at, dead_w);
        res.anchor_from_hint = true;
        dlog(st.hooks, LogTopic::kInfo,
             "dbg: phase hint %.4f -> %d, refined to %.0f (%+d samples), "
             "cons %.2f",
             opt.phase_anchor_hint, hint_at, dead_start0, best_at - hint_at,
             res.dead_consistency);
    }
    // Only a black pulse gives per-line phase. See the note on white_score
    // above: a white-only dead sector is decoded on the measured clock.
    res.per_line_sync = has_pulse;
    st.has_pulse = has_pulse;
    st.dead_start0 = dead_start0;
    dlog(st.hooks, LogTopic::kInfo,
         "dbg: pulse shape %.2f cons %.2f @%d | white shape %.2f "
         "cons %.2f @%d -> %s",
         pulse_shape, pulse_cons, pulse_at, white_shape,
         white_cons, white_at,
         has_pulse ? "black-pulse" : "white-only");
}

// --- 2b. phasing: the line-start reference the picture cannot give --------
void stage_phasing(DecodeState& st) {
    const DecodeOptions& opt = st.opt;
    DecodeResult& res = st.res;
    const std::vector<float>& video = st.video;
    const int fs = st.fs;
    const double period0 = st.period0;

    // [WMO §5.2.3.4] puts the leading edge of the phasing white at entry
    // into the dead sector — the same feature the image profile above is
    // hunting for, measured on 30 s that contain no picture content to be
    // fooled by. Measured on the library (session 7), on a black-pulse
    // station the two land on the same edge (JMH: phasing white edge at
    // -73 samples, image black run starts at -67, of 4000); on a WHITE-ONLY
    // station they do not, and the image one is the one that is wrong.
    //
    // The white-only anchor scores the rising edge of always-white, which
    // is dead-sector entry only if nothing else on the line is reliably
    // white. On VMW 2230Z the chart's blank right margin is: the always-
    // white run is 1350 samples where the dead sector is 180, so the anchor
    // sat 1149 samples early and the picture was drawn rotated by 520 px of
    // 1810 — the paper's right margin wrapped around to the left. The
    // phasing wedge sits in the LAST 4.5% of that white run, which is the
    // dead sector. Verified against the decoded picture, not just the
    // numbers (session 5's lesson).
    //
    // WHICH phasing interval, when a recording holds more than one, is
    // decided by the control tones — so they are detected here rather than
    // at §4c, and the one scan is shared by both. See PhasingOptions::t_lo:
    // inside a known transmission the last opening before the picture is
    // the one that matters, and outside one the first is the safe answer.
    // The tone scan also selects IOC when the caller did not: ISO §4.2.5
    // makes the 300/675 Hz start signal the receiver's IOC selection, not
    // merely a crop boundary. Keep the scan shared with segmentation and
    // the phasing window rather than running it twice.
    st.tones = (opt.segment || opt.ioc == 0)
                   ? detect_tones(video, fs, ToneOptions(), st.hooks)
                   : std::vector<ToneEvent>();
    const std::vector<ToneEvent>& tones = st.tones;
    int ioc = opt.ioc;
    if (ioc == 0) {
        ioc = 576;
        for (const auto& e : tones)
            if (e.kind == ToneKind::kStartIOC288) {
                ioc = 288;
                break;
            } else if (e.kind == ToneKind::kStartIOC576) {
                break;
            }
    }
    res.ioc = ioc;
    PhasingOptions popt;
    for (const auto& e : tones)
        if (e.kind != ToneKind::kStop) {
            popt.t_lo = e.t_end;
            break;
        }
    for (const auto& e : tones)
        if (e.kind == ToneKind::kStop && e.t_start > popt.t_lo) {
            popt.t_hi = e.t_start;
            break;
        }
    const PhasingResult ph = detect_phasing(video, fs, period0, popt,
                                            st.hooks);
    res.phasing_found = ph.found;
    if (ph.found) {
        res.phasing_t_start = ph.t_start;
        res.phasing_t_end = ph.t_end;
        res.phasing_lines = ph.lines;
        res.phasing_spread = ph.spread;
        res.phasing_nonlinearity = ph.nonlinearity;
        res.phasing_roughness = ph.roughness;
        res.phasing_steps = ph.steps;
        res.phasing_score = ph.score;
        const double phase =
            std::fmod(std::fmod(ph.anchor - st.start, period0) + period0,
                      period0);
        double d = phase - st.dead_start0;
        while (d > period0 / 2.0) d -= period0;
        while (d < -period0 / 2.0) d += period0;
        res.phasing_anchor_delta = d;
        // Only where the image has nothing to offer. A pulse station
        // already anchors on a feature it re-measures every line, at
        // 88-99% lock rates; swapping that for a phase measured once,
        // 30 s in, would trade a tracked reference for a fixed one.
        // The delta is reported either way, so the day a pulse station
        // disagrees by more than a porch, it will be in the output.
        //
        // ...and only where the OPERATOR has nothing to offer either. The
        // phasing anchor is one of the two automatic answers `phase_anchor
        // _hint` exists to overrule [docs/05 §7.1]; letting it win here
        // would make the hint work on pulse stations and vanish on
        // white-only ones, which are the recordings that need it most. The
        // delta is still reported, so the two answers can still be
        // compared afterwards.
        if (opt.use_phasing && !st.has_pulse && !res.anchor_from_hint) {
            st.dead_start0 = phase;
            res.anchor_from_phasing = true;
        }
    }
    dlog(st.hooks, LogTopic::kInfo,
         "dbg: phasing found=%d %.2f-%.2f s lines=%d "
         "anchor=%.1f delta=%+.1f -> %s",
         ph.found, ph.t_start, ph.t_end, ph.lines, ph.anchor,
         res.phasing_anchor_delta,
         res.anchor_from_phasing ? "USED" : "image anchor");
}

}  // namespace

namespace {

// --- 3. pass A: sequential sync tracking ------------------------------------
void stage_track(DecodeState& st) {
    const DecodeOptions& opt = st.opt;
    DecodeResult& res = st.res;
    const std::vector<float>& video = st.video;
    const int n_lines = st.n_lines;
    const double period0 = st.period0;

    // Walk line to line: each search window is centred on the previous
    // lock + coarse period, so sound-card drift can never walk the sync
    // out of the window (the failure mode of a fixed coarse grid).
    // Lines with no real sync match are coasted (prediction only).
    const double lock = st.lock;
    st.spos.assign(n_lines, 0.0);
    st.sstr.assign(n_lines, 0.0);
    std::vector<double>& spos = st.spos;
    std::vector<double>& sstr = st.sstr;
    if (!res.per_line_sync) {
        // Nothing to track. Draw on the measured clock from the coarse
        // anchor and leave every line unlocked, honestly.
        for (int l = 0; l < n_lines; l++) {
            spos[l] = st.start + st.dead_start0 + l * period0;
            sstr[l] = -1.0;
        }
    } else {
        // An operator hint switches the whole-line re-acquisition sweep
        // below OFF, and that is not a tuning choice — it is what makes
        // `phase_anchor_hint` work at all on a station that sends a pulse
        // [docs/05 §7.1]. Without it the hint reaches `dead_start0` and
        // then dies two stages later: the sweep looks over HALF A LINE, so
        // it is free to walk the tracker off the feature the operator
        // picked and back onto the one the automatic scan liked — which is
        // the candidate they were overruling. Measured before this existed:
        // a hint 900 samples away on a synthetic decoy, and one at half a
        // line on JMH, each moved the anchor and left the saved page
        // byte-identical.
        //
        // The cost, stated because it is real: with a hint, a tracker that
        // falls off a dropout can no longer sweep the line to find its way
        // back, so a hinted decode of a recording with a big time-skip can
        // tear where an un-hinted one recovers. That is the right way round.
        // The sweep's job is to decide WHICH feature the line starts on,
        // and once the operator has answered that question, a search free to
        // answer it differently is not a recovery.
        //
        // Line 0's `wide` search below is deliberately NOT narrowed with it.
        // Narrowing it too was written, and then removed: no fixture and no
        // synthetic could be made to tell the two apart (the mutation
        // survived), and code whose effect cannot be shown is not code this
        // decoder keeps. It would matter only where a stronger competing
        // feature sits between `search_frac` and 5% of a line from the
        // operator's click, which is a recording the library does not have.
        const bool hinted = opt.phase_anchor_hint >= 0.0;
        const double wide = 0.05 * period0;
        // must exceed the phasing<->image regime offset (~half a dead
        // sector = 0.0225 lines) or the tracker falls off the grid at the
        // phasing->image boundary and coasts to EOF
        const double narrow = opt.search_frac * period0;
        // the template reads a dead sector's worth either side of p
        const double margin = 2.0 * st.dead + 2.0;
        const double pmin = margin;
        const double pmax = video.size() - margin;
        double pred = st.start + st.dead_start0;
        spos[0] = fax_best_sync(video, std::max(pmin, pred - wide),
                            std::min(pmax, pred + wide), st.pulse, &sstr[0]);
        double last_good = spos[0];
        long last_good_l = 0;
        if (sstr[0] < lock) last_good = pred;  // coast from coarse
        // Re-acquisition. A tracker that only ever looks ±narrow around its
        // own prediction can never come back from being wrong: a coarse
        // anchor off by more than the window, or a stream time-skip, puts
        // the sync outside every future window and the file coasts to EOF
        // (measured: himawari.wav, anchor 128 samples late, 14 locks of
        // 1988 — the signal itself is textbook). So after a run of misses,
        // sweep the whole line. The sweep only counts if the template
        // actually matches, so a white-only station cannot re-acquire onto
        // picture content: it simply keeps coasting, which is the honest
        // outcome.
        int miss = 0;
        for (int l = 1; l < n_lines; l++) {
            if ((l & 63) == 0) {
                throw_if_cancelled(st.hooks, "sync-track");
                report(st.hooks, "sync-track",
                       static_cast<double>(l) / n_lines);
            }
            const double c = last_good + (l - last_good_l) * period0;
            const bool reacq =
                !hinted && miss >= kReacqMisses && (miss % kReacqEvery) == 0;
            const double span = reacq ? 0.5 * period0 : narrow;
            const double lo = std::max(pmin, c - span);
            const double hi = std::min(pmax, c + span);
            spos[l] = fax_best_sync(video, lo, hi, st.pulse, &sstr[l],
                                reacq ? kReacqStep : 1.0);
            if (sstr[l] >= lock) {
                last_good = spos[l];
                last_good_l = l;
                miss = 0;
            } else {
                miss++;
            }
        }
    }
}

// --- 4. pass B: robust period/phase from median LONG-baseline slope --------
void stage_fit(DecodeState& st) {
    const DecodeOptions& opt = st.opt;
    DecodeResult& res = st.res;
    const int n_lines = st.n_lines;
    const double period0 = st.period0;
    const double lock = st.lock;
    const std::vector<double>& spos = st.spos;
    const std::vector<double>& sstr = st.sstr;

    // Least squares bends here by design of the signal: phasing lines and
    // image lines anchor the template ~half a dead sector apart (the wedge
    // is the mirror of the pulse), so a single fitted line through both
    // regimes tilts the slope by tens of ppm. The median over pairs is what
    // rejects that — but the pairs must be FAR APART.
    //
    // Until session 5 they were neighbours (<= 10 lines). A sync position is
    // measured to a sample or two, so a one-line slope is the period plus
    // ~2 samples of noise: on a 4000-sample line that is ±500 ppm of scatter
    // around a signal of ~100 ppm, and the median of that distribution does
    // not recover the period — measured on JSC2 from the very same spos
    // array: neighbour pairs give -75 ppm, pairs 500+ lines apart give
    // +178 ppm. The long baseline is confirmed by two independent methods
    // (the block fold above: +172; image shear at nominal clock: +151) and
    // by the picture, which was visibly slanted: JSC2 drifted a third of a
    // page and the local-median correction froze once residuals passed
    // 2*search, so nothing downstream caught it. Precision is baseline, not
    // averaging: pairing each locked line with the one half a recording
    // later turns the same ±2 samples into fractions of a ppm.
    double a = st.start + st.dead_start0, b = period0;
    // Did the long-baseline fit actually have a baseline? This is the
    // question `clock_ppm_fallback` is answered by, and asking the fit
    // itself is better than asking three proxies for it: §7.1 names a
    // white-only station, a forced start and too few locked lines, and all
    // three arrive here as the same fact — no segment of locked lines long
    // enough to pair across. See the fallback below.
    bool fitted = false;
    {
        std::vector<int> lk;
        for (int l = 0; l < n_lines; l++)
            if (sstr[l] >= lock) lk.push_back(l);
        // Cut the locked lines at every STEP first. A long baseline is only
        // meaningful inside one regime: phasing lines and image lines anchor
        // the template a step apart (measured on the 60 s KiwiSDR fixture:
        // +167 samples between line 39 and line 53), and a stream time-skip
        // does the same mid-recording. A pair straddling a step reads that
        // step as drift — on a short fixture, where most pairs straddle,
        // that alone put the clock at +607 ppm against a true -88.
        // Line-to-line jitter is a sample or two, so the cut is unambiguous.
        // Test the TOTAL deviation over the gap, not a per-line slope: the
        // step usually falls across unlocked lines (the fixture's lands
        // between locked line 39 and locked line 53), and dividing 167
        // samples by a 14-line gap hides it under any sane per-line
        // threshold. period0 is the fold estimate, good to a few ppm, so
        // genuine drift across a gap of even 100 lines is a few samples.
        std::vector<size_t> cut{0};
        for (size_t i = 1; i < lk.size(); i++) {
            const double gap = lk[i] - lk[i - 1];
            const double jump =
                (spos[lk[i]] - spos[lk[i - 1]]) - period0 * gap;
            if (std::fabs(jump) > 0.02 * period0) cut.push_back(i);
        }
        cut.push_back(lk.size());

        // Baseline inside a segment: long enough that sample quantization in
        // spos is small against the accumulated drift, short enough that one
        // discontinuity contaminates only a minority of pairs. Both ends
        // measured (session 5) by sweeping the baseline k: JSC2 reads -75 ppm
        // at k<=8 and settles at +175 from k=128; the Himawari time-skip is
        // invisible up to k=512 and swings the answer to -393 at k=1024,
        // where every pair straddles it. An eighth of the segment sits inside
        // both limits by construction and grows with the recording.
        std::vector<double> slopes;
        for (size_t c = 0; c + 1 < cut.size(); c++) {
            const size_t lo = cut[c], hi = cut[c + 1];
            const size_t n = hi - lo;
            if (n < 16) continue;
            const size_t k = std::min(std::max<size_t>(64, n / 8), n / 2);
            for (size_t i = lo; i + k < hi; i++) {
                const int l0 = lk[i], l1 = lk[i + k];
                const double s = (spos[l1] - spos[l0]) / (l1 - l0);
                if (std::fabs(s - period0) < 0.02 * period0)
                    slopes.push_back(s);
            }
        }
        if (!slopes.empty()) {
            b = median(slopes);
            std::vector<double> intercepts;
            for (int l = 0; l < n_lines; l++)
                if (sstr[l] >= lock) intercepts.push_back(spos[l] - b * l);
            a = median(intercepts);
            // ...and only counts as a baseline if the picture is going to
            // be drawn on it. `autolock = false` throws this fit away two
            // statements below, so claiming a measurement there would hand
            // the operator's value to nothing.
            fitted = opt.autolock;
        }
    }
    st.a = a;
    st.b = b;
    res.line_period_s = b / st.fs;
    res.clock_ppm = (b / st.nominal - 1.0) * 1e6;

    if (!opt.autolock) {
        // no clock correction at all: coarse phase, nominal period
        st.a = st.start + st.dead_start0;
        st.b = st.nominal;
    }

    // --- the operator's SYNC, where and only where nothing measured it ----
    // [docs/05 §7.1] The fit above wins wherever it ran, and this is the
    // half of the decision most easily written as a plain override — which
    // would be the quiet bug, because it fails on exactly the recordings
    // that look fine: a healthy pulse station would be drawn on an
    // eyeballed ppm instead of a fitted one and nothing would say so.
    //
    // Where the fit did NOT run there is no measurement to outrank it. The
    // period is then `period0`, the whole-file fold refinement, which is
    // the number that is off by 30-180 ppm on real signals (session 5) and
    // slants a white-only station by exactly that error — the one thing
    // the operator can see and the fold cannot. So their trim replaces it,
    // as a ppm against nominal, the same quantity `res.clock_ppm` reports
    // and the same one `StreamPreview::set_clock_ppm` applies live.
    if (!fitted && !std::isnan(opt.clock_ppm_fallback)) {
        st.b = st.nominal * (1.0 + opt.clock_ppm_fallback * 1e-6);
        res.line_period_s = st.b / st.fs;
        res.clock_ppm = opt.clock_ppm_fallback;
        res.clock_from_fallback = true;
        dlog(st.hooks, LogTopic::kInfo,
             "dbg: no fit baseline -> operator SYNC %+.1f ppm, b=%.4f",
             opt.clock_ppm_fallback, st.b);
    }

    dlog(st.hooks, LogTopic::kInfo,
         "dbg: dead_start0=%.1f a=%.1f b=%.4f n_lines=%d",
         st.dead_start0, st.a, st.b, n_lines);
    for (int l = 0; l < n_lines; l++) {
        // NOVA_DEBUG printed every 10th line, NOVA_DEBUG_FULL every line;
        // as topics that is kInfo for the sparse form, kDetail for the
        // dense one.
        if (l % 10 == 0)
            dlog(st.hooks, LogTopic::kInfo,
                 "dbg: l=%3d spos=%.1f sstr=%.2f resid=%+.1f",
                 l, spos[l], sstr[l], spos[l] - (st.a + st.b * l));
        else
            dlog(st.hooks, LogTopic::kDetail,
                 "dbg: l=%3d spos=%.1f sstr=%.2f resid=%+.1f",
                 l, spos[l], sstr[l], spos[l] - (st.a + st.b * l));
    }
}

// --- 4c. segmentation: start -> phasing -> image -> stop --------------------
void stage_segment(DecodeState& st) {
    const DecodeOptions& opt = st.opt;
    DecodeResult& res = st.res;
    const int n_lines = st.n_lines;
    const double lock = st.lock;
    const std::vector<double>& sstr = st.sstr;

    // The transmission sequence of WMO §5.2.3 is not picture: the start
    // tone and the phasing interval precede the image and the stop tone
    // ends it [WMO §5.2.5]. Drawing them produced tens of lines of black/
    // white bars top and bottom of every decode, and — worse — they were
    // counted in `lines` and diluted `locked_lines`, since a phasing line
    // cannot match the picture-line sync template by design.
    //
    // This crops the OUTPUT only. Onset, period, anchor and both tracking
    // passes still see the whole recording, deliberately: the long-baseline
    // period fit of session 5 needs every lock it can get, and the phasing
    // lines lock nowhere near the picture template anyway. Nothing measured
    // moves — only what is drawn.
    int line_lo = 0, line_hi = n_lines;
    if (opt.segment) {
        const std::vector<ToneEvent>& ev = st.tones;  // scanned once, at §2b
        // Segment the FIRST transmission. A recording can hold more than
        // one: `jmh sample` carries a start at 6 s, its stop at 404 s, and
        // then the NEXT transmission's start at 425 s. Every boundary rule
        // here is therefore "the first one after the previous boundary",
        // never "the last" or "the largest" — taking the latest start tone
        // dropped that recording's entire chart and kept 143 s of the
        // following one.
        bool have_head = false, have_tail = false;
        double t0 = 0.0;
        for (const auto& e : ev)
            if (e.kind != ToneKind::kStop) {
                t0 = e.t_end;
                have_head = true;
                break;
            }
        // ...then the first stop tone that follows the opening.
        double t1 = static_cast<double>(st.video.size()) / st.fs;
        for (const auto& e : ev)
            if (e.kind == ToneKind::kStop && e.t_start > t0) {
                t1 = e.t_start;
                have_tail = true;
                break;
            }
        // Phasing belongs to this transmission's opening only if it sits
        // between the start tone and that stop [WMO §5.2.3]. It runs longer
        // than the tone, so where it is present it sets the boundary.
        if (res.phasing_found && res.phasing_t_end > t0 &&
            res.phasing_t_end <= t1) {
            t0 = res.phasing_t_end;
            have_head = true;
        }
        // Only crop an end a control signal actually bounds. Otherwise the
        // rounding of the line index alone would report "dropped 1 line of
        // stop" on a recording with no stop tone in it (JSC1).
        const int lo = have_head
                           ? static_cast<int>(std::ceil((t0 * st.fs - st.a) / st.b))
                           : 0;
        const int hi = have_tail
                           ? static_cast<int>(std::floor((t1 * st.fs - st.a) / st.b))
                           : n_lines;
        const int clo = std::max(0, std::min(lo, n_lines));
        const int chi = std::max(clo, std::min(hi, n_lines));
        // A segment that leaves nothing is a detection failure, not an
        // instruction to emit an empty picture: fall back to the whole
        // recording and say so rather than returning a blank image.
        if (chi - clo >= 4) {
            line_lo = clo;
            line_hi = chi;
            res.segmented = (line_lo != 0 || line_hi != n_lines);
        }
        dlog(st.hooks, LogTopic::kInfo,
             "dbg: segment t0=%.2f t1=%.2f -> lines [%d,%d) of %d%s",
             t0, t1, line_lo, line_hi, n_lines,
             res.segmented ? "" : " (not applied)");
    }
    st.line_lo = line_lo;
    st.line_hi = line_hi;
    res.lines_dropped_head = line_lo;
    res.lines_dropped_tail = n_lines - line_hi;
    res.image_t_start = (st.a + st.b * line_lo) / st.fs;
    res.image_t_end = (st.a + st.b * line_hi) / st.fs;

    // Honest lock metric: lines where the sync template actually matched.
    // (Before session 3 this counted "correction did not jump", which is
    // vacuously true when the tracker coasts through noise.) Counted over
    // the DRAWN lines only, so it describes the picture that came out
    // rather than a window the caller never sees.
    res.locked_lines = 0;
    for (int l = line_lo; l < line_hi; l++)
        if (sstr[l] >= lock) res.locked_lines++;
}

}  // namespace

namespace {

// --- 4d. is the timebase linear? --------------------------------------------
void stage_timebase(DecodeState& st) {
    DecodeResult& res = st.res;
    const int n_lines = st.n_lines;
    const int fs = st.fs;
    const double lock = st.lock;
    const std::vector<double>& spos = st.spos;
    const std::vector<double>& sstr = st.sstr;
    const int line_lo = st.line_lo, line_hi = st.line_hi;

    // Everything upstream models the recording's time axis as ONE straight
    // line: a period, an intercept, and a clock error in ppm. Two library
    // recordings break that model — JSC2 and JSC3 carry ~21-sample steps
    // every few lines, in the audio, present at 44.1 kHz through a separate
    // demodulator (session 8) — and nothing said so, which left `clock_ppm`
    // meaning the clock on eighteen files and the clock PLUS an insertion
    // rate on two. This measures the difference instead of assuming it away.
    //
    // Prior art has nothing to reuse here (docs/00, session 9): JWX applies
    // one operator-typed constant to every line; weatherfax_pi notices lost
    // samples only where PortAudio hands it a paInputOverflow, which a file
    // never does; fldigi accumulates a histogram of per-line shifts but
    // collapses it to its mode. All three ABSORB a stepping timebase and
    // none reports one.
    //
    // Two statistics, sharing no code, either of which can convict:
    //
    // (a) image domain, needs per-line sync. The tracked sync residual,
    //     local-median smoothed exactly as the assembly below smooths it.
    //     A jump between two neighbouring locked lines is mostly
    //     measurement noise; an inserted sample is PERSISTENT, so it
    //     survives the median and noise does not. Measured over the
    //     library, rate per 1000 drawn lines of smoothed steps above
    //     kStepSec: nine clean recordings 0.0-7.0, all six JSC 64.8-339.8.
    //
    // (b) phasing domain, needs a phasing interval. The phasing signal is
    //     one edge repeated at exactly the line rate [WMO §5.2.3], so its
    //     per-line positions must lie on a straight line; what is left
    //     after removing the best one is non-linearity. Measured: eleven
    //     clean recordings 1.0-3.8 samples, JSC2/3/4 20.2-25.5.
    //
    // Thresholds sit in the middle of both gaps, in SAMPLES OF TIME rather
    // than fractions of a line, because an insertion is a fixed number of
    // samples in someone's capture chain and knows nothing about the line
    // rate — which is also why the same numbers separate 60 lpm and 120 lpm
    // recordings without being rescaled.
    //
    // A verdict of kLinear is about the RECORDING, not the picture: every
    // one of these files decodes correctly, because a per-line tracker
    // absorbs steps without being told they are there. What it changes is
    // the meaning of clock_ppm, and whether phasing_anchor_delta can be
    // compared against the rest of the library at all.
    const double step_min = kStepSec * fs;
    std::vector<double> smoothed;
    for (int l = line_lo; l < line_hi; l++) {
        std::vector<double> r;
        for (int k = std::max(0, l - kMedRad);
             k <= std::min(n_lines - 1, l + kMedRad); k++)
            if (sstr[k] >= lock) r.push_back(spos[k] - (st.a + st.b * k));
        if (!r.empty()) smoothed.push_back(median(r));
    }
    if (smoothed.size() >= kStepMinLines) {
        res.timebase_lines = static_cast<int>(smoothed.size());
        int n = 0;
        for (size_t m = 1; m < smoothed.size(); m++)
            if (std::fabs(smoothed[m] - smoothed[m - 1]) > step_min) n++;
        res.timebase_step_lines = n;
        res.timebase_step_rate =
            1000.0 * n / static_cast<double>(smoothed.size() - 1);
        res.timebase = res.timebase_step_rate > kStepRateLimit
                           ? Timebase::kSteps
                           : Timebase::kLinear;
    }
    // Either statistic can convict on its own; only one has to be
    // available. They agree wherever both are (JSC2/3/4 both ways).
    //
    // Session 10: "the edge is not straight" is not the same claim as
    // "the timebase steps", and until this session the second was read
    // off the first. Three things bend a phasing edge, and the library
    // holds one of each:
    //
    //   NOISE. GYA 2300Z's interval is faded; its per-line edge moves
    //   ~15 samples line to line, so a 10-sample threshold is below its
    //   own measurement floor and any verdict is a coin toss. Read raw
    //   it scored 46.2 — worse than JSC2, on a recording with no steps.
    //   ONE SKIP. JMH KiwiSDR Himawari's interval straddles a single
    //   ~95-sample jump with textbook-linear edge either side. Session 9
    //   already settled that one skip is not a rate — in the image
    //   domain, which counts steps. This domain measured a spread and so
    //   could not tell one jump from fifty; it now counts too. Its 1922
    //   tracked lines read 1.6 steps per 1000, i.e. linear, and the
    //   60-line phasing interval was over-ruling them.
    //   STEPS. JSC2 and JSC3, 16 and 17 persistent moves in 59 lines.
    //
    // The same kNonlinSec does all three jobs, so there is one number to
    // move and not three: it is the resolution the test claims. An
    // interval whose own line-to-line noise exceeds it cannot resolve it
    // in either direction, and says so rather than guessing.
    if (res.phasing_found && res.phasing_lines >= 8) {
        const double limit = kNonlinSec * fs;
        if (res.phasing_roughness >= limit) {
            res.phasing_witness = PhasingWitness::kNoisy;
        } else if (res.phasing_nonlinearity <= limit) {
            res.phasing_witness = PhasingWitness::kStraight;
        } else if (res.phasing_steps >= kMinPhasingSteps) {
            res.phasing_witness = PhasingWitness::kSteps;
        } else {
            res.phasing_witness = PhasingWitness::kOneSkip;
        }
        if (res.phasing_witness == PhasingWitness::kSteps)
            res.timebase = Timebase::kSteps;
        else if (res.phasing_witness == PhasingWitness::kStraight &&
                 res.timebase == Timebase::kUnknown)
            res.timebase = Timebase::kLinear;
    } else if (res.phasing_found) {
        res.phasing_witness = PhasingWitness::kTooShort;
    }
    dlog(st.hooks, LogTopic::kInfo,
         "dbg: timebase %s step_lines=%d rate=%.1f/1000 "
         "phasing_nonlin=%.1f smp",
         res.timebase == Timebase::kSteps ? "STEPS"
         : res.timebase == Timebase::kLinear ? "linear"
                                             : "unknown",
         res.timebase_step_lines, res.timebase_step_rate,
         res.phasing_nonlinearity);
}

// --- 4e. change points: where the line start REALLY moves -------------------
void stage_change_points(DecodeState& st) {
    const DecodeOptions& opt = st.opt;
    const int n_lines = st.n_lines;
    const int fs = st.fs;
    const double lock = st.lock;
    const std::vector<double>& spos = st.spos;
    const std::vector<double>& sstr = st.sstr;

    // A smoother and a corrector want opposite things from the same window.
    // The ±kMedRad median exists to reject a bad template match on one line,
    // and it does; but where the line start genuinely moves — a sample-level
    // skip in the capture chain — it also averages that move across up to
    // 17 lines, so the picture is drawn in the wrong place on either side of
    // it. On a recording that steps every few lines the correction is never
    // in the right place at all, which is what Sara's review of the whole
    // library found by eye before any of this was measured: "the black
    // strip, it's actually zig zagging, not solid at all" on all six JSC
    // recordings, and nothing of the sort on FAXSignal or XSG.
    //
    // The fix is to tell the two apart. A MOVE is a change of level that the
    // locked lines on both sides agree about; NOISE is not. So: compare the
    // median of the kSegHalf locked lines before line l with the median of
    // the kSegHalf from l on, and call l a change point when they disagree
    // by more than kNonlinSec — the resolution the timebase test already
    // claims, which is the same claim in a different domain (below that, a
    // move cannot be told from the measurement).
    //
    // Prior art has none of this, and the way it does not is the reason it
    // is written here rather than reused (docs/00, session 11): fldigi
    // measures a per-line correlation shift and keeps only the MODE of a
    // histogram of them (`correlation_shift`), then draws every line at
    // `m_img_width * frac(m_img_sample / m_smpl_per_lin)` — sample count
    // alone. JWX rotates each line by one operator-typed constant times the
    // line number (`clock_correct_line`). weatherfax_pi and KiwiSDR take one
    // median of the phasing positions and skip that many samples ONCE
    // (`phasingSkipData` → `m_skip`), then never look again. All four
    // correct a CLOCK; none corrects a timebase, and the one that already
    // computes the per-line number throws it away.
    st.cpoint.assign(n_lines, 0);
    if (opt.autolock) {
        std::vector<int> lk;
        for (int l = 0; l < n_lines; l++)
            if (sstr[l] >= lock) lk.push_back(l);
        const double move_min = kNonlinSec * fs;
        // Every line the comparison fires on is a change point, including
        // runs of adjacent ones. Thinning each run to its largest member —
        // "one skip is one line" — is the obvious tidy-up and it is WRONG,
        // measured both ways: it merges the genuinely separate steps of a
        // recording that inserts samples every few lines, and it made every
        // number worse (ground-truth synthetic 2.05 -> 2.39 px, its linear
        // control 0.19 -> 1.71, the JSC2 fixture's drawn edge p90 2.0 -> 5.0
        // px). Left un-thinned deliberately.
        for (size_t j = kSegHalf; j + kSegHalf <= lk.size(); j++) {
            std::vector<double> pre, post;
            for (size_t k = j - kSegHalf; k < j; k++)
                pre.push_back(spos[lk[k]] - (st.a + st.b * lk[k]));
            for (size_t k = j; k < j + kSegHalf; k++)
                post.push_back(spos[lk[k]] - (st.a + st.b * lk[k]));
            if (std::fabs(median(post) - median(pre)) > move_min)
                st.cpoint[lk[j]] = 1;
        }

        // A move the STREAM really took persists; a lock error cancels
        // itself the next time the tracker finds the true feature. Session
        // 26 measured this on HLL 2147Z: isolated hops of +50..+90 samples
        // that return to the family level one to three lines later, always
        // with a dipped lock score (0.62-0.71 against the family's
        // 0.88-0.91) — the pulse template's white window polluted by dark
        // content close behind the gap, so a position ~60 samples late
        // out-scores the true one (0.72 vs 0.44 measured on line 342,
        // where the audio itself is straight to ±5 samples). Two hops in
        // one four-line window beat the median above, the "move" is
        // vouched, and the dead-sector strip jogs — the raggedness Sara
        // sent back twice.
        //
        // So: a step that RETURNS within the vouching distance never
        // established a level — four lines at the new level is what "the
        // level moved" means to the detector above. The pair is cancelled
        // when the steps net to zero within the detection resolution and
        // the levels outside the pair agree the same way. The lines
        // between are then drawn by the same segment as their neighbours.
        //
        // The cost, stated: a real drop compensated by a real insertion
        // within three lines would also cancel, and its rows would be
        // drawn at the surrounding level — displaced by exactly the event
        // it hides. Nothing in the library does that: the warp drop and
        // the JSC insertions never return, and a browser catch-up pair is
        // unmeasured, not known.
        std::vector<int> cp;
        for (int l = 0; l < n_lines; l++)
            if (st.cpoint[l]) cp.push_back(l);
        auto win_med = [&](int line, bool post_side) {
            const size_t j = static_cast<size_t>(
                std::lower_bound(lk.begin(), lk.end(), line) - lk.begin());
            std::vector<double> v;
            if (post_side) {
                for (size_t k = j; k < j + kSegHalf && k < lk.size(); k++)
                    v.push_back(spos[lk[k]] - (st.a + st.b * lk[k]));
            } else {
                for (size_t k = j >= kSegHalf ? j - kSegHalf : 0; k < j; k++)
                    v.push_back(spos[lk[k]] - (st.a + st.b * lk[k]));
            }
            return v.empty() ? 0.0 : median(v);
        };
        for (size_t i = 0; i + 1 < cp.size();) {
            const int l1 = cp[i], l2 = cp[i + 1];
            bool cancel = false;
            if (l2 - l1 < kSegHalf) {
                const double d1 = win_med(l1, true) - win_med(l1, false);
                const double d2 = win_med(l2, true) - win_med(l2, false);
                if (std::fabs(d1 + d2) <= move_min &&
                    std::fabs(win_med(l1, false) - win_med(l2, true)) <=
                        move_min)
                    cancel = true;
            }
            if (cancel) {
                st.cpoint[l1] = 0;
                st.cpoint[l2] = 0;
                dlog(st.hooks, LogTopic::kSeams,
                     "dbg: seam pair at lines %d/%d cancelled: a lock hop "
                     "that returned, not a stream move",
                     l1, l2);
                i += 2;
            } else {
                i++;
            }
        }
    }
}

}  // namespace

namespace {

// --- 5. assembly: segmented robust fit of the tracked residual --------------
void stage_assembly(DecodeState& st) {
    const DecodeOptions& opt = st.opt;
    DecodeResult& res = st.res;
    const std::vector<float>& video = st.video;
    const int fs = st.fs;
    const double lock = st.lock;
    const std::vector<double>& spos = st.spos;
    const std::vector<double>& sstr = st.sstr;
    const int line_lo = st.line_lo, line_hi = st.line_hi;
    const double a = st.a, b = st.b;
    const std::vector<char>& cpoint = st.cpoint;

    // The template anchor is the sync-pulse start in image lines; in the
    // phasing region the best template match sits ~half a dead sector
    // earlier (phasing is white-wedge-then-black, the mirror image). The
    // local window is cut at change points and at the phasing/image
    // boundary, a Theil-Sen line carries the residual ramp inside each
    // segment, and the clamp rejects moves the lines on both sides did not
    // vouch for.
    const int width = (res.ioc == 288) ? 905 : 1810;
    const int out_lines = line_hi - line_lo;
    Image img;
    img.width = width;
    img.height = out_lines;
    img.px.resize(static_cast<size_t>(width) * out_lines);

    std::vector<double> starts(static_cast<size_t>(out_lines), 0.0);
    std::vector<char> unlocked(static_cast<size_t>(out_lines), 1);
    const double corr_clamp = 0.03 * b;
    double prev_corr = 0.0;
    bool have_corr = false;
    double place_sq = 0.0;
    int place_n = 0;
    for (int l = line_lo; l < line_hi; l++) {
        double line_start = a + b * l;  // sync-anchor position
        if (opt.autolock) {
            // The window stops at a change point on either side, so the
            // median never mixes two levels: within one segment it is the
            // same noise-rejecting median as before, and at a seam it
            // switches over in a single line instead of ramping across it.
            // ...and at the edges of the drawn picture, because a phasing
            // line and an image line do not anchor the same feature: the
            // wedge is the mirror of the pulse, ~half a dead sector away
            // [WMO §5.2.3.1]. Reaching back into the phasing region for a
            // median drew the first lines of the picture at the control
            // signal's phase and then jumped — 88.6 px into XSG FYCI, 76.7
            // into `test chart`, at drawn lines 8 and 8.
            int wlo = std::max(line_lo, l - kMedRad);
            for (int k = l; k > wlo; k--)
                if (cpoint[k]) { wlo = k; break; }
            int whi = std::min(line_hi - 1, l + kMedRad);
            for (int k = l + 1; k <= whi; k++)
                if (cpoint[k]) { whi = k - 1; break; }
            // No |residual| gate here. Until session 11 a residual further
            // than 2*search from the FITTED line was dropped as bogus, which
            // is a statement about the fit, not about the line: JMH KiwiSDR
            // Himawari loses ~1270 samples mid-recording, so its first 720
            // drawn lines sit 1100-1390 samples off the line fitted through
            // the other 1200 — every one of them thrown away, corr frozen at
            // 0, and the whole top of the chart drawn half a page across.
            // That is the "sync lose at the top part" in Sara's review. What
            // makes a residual bogus is disagreeing with its NEIGHBOURS, and
            // the segment median already rejects that.
            std::vector<double> r;
            std::vector<int> rl;
            for (int k = wlo; k <= whi; k++)
                if (sstr[k] >= lock) {
                    r.push_back(spos[k] - (a + b * k));
                    rl.push_back(k);
                }
            // A line with nothing locked near it coasts: it keeps the level,
            // it does not SET one. Until session 11 coasting counted as a
            // level, so a picture whose first lines are unlocked started
            // from a correction of zero and the clamp then walked it up to
            // the truth at 0.03 lines each — on the warp fixture, whose
            // first nine lines do not lock, the first two DRAWN lines came
            // out 80.7 and 26.3 px from where the signal put them and ten
            // lines were clamped. The clamp is there to stop a bad median,
            // not to ration the first real measurement.
            const bool measured = !r.empty();
            // Inside a segment the residual is not flat: whatever the
            // period fit could not absorb walks it, and on a recording that
            // inserts samples the fit absorbs the MEAN insertion rate, so
            // between two steps the residual ramps back down at that rate.
            // On the ground-truth synthetic that ramp is 1.9 samples a line
            // over an 11-line segment — 19 samples of tilt, twice the step
            // it sits between — and a median through it is the average of a
            // slope, wrong at both ends by half of it. So: a robust LINE
            // through the segment (Theil-Sen, median of pairwise slopes,
            // which a bad lock cannot lever) evaluated at this line, and the
            // median only where there are too few points to see a slope.
            double corr = have_corr ? prev_corr : 0.0;
            if (measured) {
                corr = median(r);
                if (r.size() >= 4) {
                    std::vector<double> slopes;
                    slopes.reserve(r.size() * (r.size() - 1) / 2);
                    for (size_t p = 0; p < r.size(); p++)
                        for (size_t q = p + 1; q < r.size(); q++)
                            if (rl[q] != rl[p])
                                slopes.push_back((r[q] - r[p]) /
                                                 (rl[q] - rl[p]));
                    if (!slopes.empty()) {
                        const double sl = median(slopes);
                        std::vector<double> icept;
                        icept.reserve(r.size());
                        for (size_t p = 0; p < r.size(); p++)
                            icept.push_back(r[p] - sl * (rl[p] - l));
                        corr = median(icept);
                    }
                }
            }
            // A seam is a move the lines on both sides vouched for, so it
            // goes through whole; the clamp still guards everything else,
            // where a large single-line move means the median was fooled.
            const bool seam = cpoint[l] != 0;
            if (have_corr && measured && !seam) {
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
            if (seam && have_corr) {
                res.seams++;
                res.max_seam_px = std::max(res.max_seam_px, step_px);
                dlog(st.hooks, LogTopic::kSeams,
                     "dbg: seam line %d (drawn %d): %+.1f smp = %.1f px",
                     l, l - line_lo, corr - prev_corr, step_px);
            } else {
                res.max_step_px = std::max(res.max_step_px, step_px);
            }
            prev_corr = corr;
            if (measured) have_corr = true;
            line_start += corr;
            // What the eye will see: how far this line is drawn from where
            // the signal put it. Locked lines only — an unlocked line has no
            // measurement to be judged against.
            if (sstr[l] >= lock) {
                const double err = (spos[l] - (a + b * l) - corr) / b * width;
                place_sq += err * err;
                place_n++;
                res.place_max_px = std::max(res.place_max_px, std::fabs(err));
                if (std::fabs(err) > 20.0)
                    dlog(st.hooks, LogTopic::kSeams,
                         "dbg: line %d (drawn %d) drawn %.1f px from "
                         "where the signal put it (sstr %.2f)",
                         l, l - line_lo, err, sstr[l]);
            }
        }

        starts[l - line_lo] = line_start;
        unlocked[l - line_lo] = sstr[l] >= lock ? 0 : 1;
    }

    // --- 5b. inside the line: where the samples actually went --------------
    // A line start is one number per line, and a capture chain that inserts
    // samples does not wait for a line boundary to do it. When it lands
    // mid-line, everything after that point in THAT line is displaced while
    // everything before it is not, so no per-line offset can place the row:
    // the row is stretched, not moved.
    //
    // Measured in the session-11 decodes, which is how this was found: on
    // JSC1 the left end and the right end of the same drawn row move
    // independently (correlation +0.12, and they disagree by 5 px of 1810 at
    // the 90th percentile; 10 px on JSC2). On XSG FYCI, a recording with a
    // linear timebase, the same measurement reads 1 px. Sara's verdict on
    // the session-11 decodes was "for JSCs, small zigzag are still zigzags,
    // still cause difficulties of reading", and this is what is left.
    //
    // The size of the move is already known — it is how much this line's
    // correction differs from the next line's. Only its POSITION in the line
    // is unknown, and the picture itself says where: a weather fax moves
    // 1/1810 of a page between one line and the next, so the break is where
    // splitting the row there makes it agree best with the row above. The
    // candidates include "at the very start" and "not in this line at all",
    // so the search can only choose a row that matches its neighbour better
    // than the un-split one did.
    // Which unlocked rows nothing can place but the picture — and which the
    // signal can still place after all.
    //
    // A row with no template match of its own is drawn where the locked
    // lines within ±kMedRad put it, and that is right while nothing moves.
    // It is wrong in one specific place: a run of unlocked rows with a
    // dropout inside it, where the lines before and after disagree about
    // the phase and the rows between belong to neither. JMH KiwiSDR
    // Himawari loses ~1270 samples that way and its eight unlocked rows sat
    // ~75 px from the rest of the chart — the band Sara found still there
    // after session 11, and again in the session-11b decodes, where the
    // picture placement below had moved them by the best match within
    // ±120 px: the true move is 574 px, so the search could not even reach
    // it. Session 12's answer is to ask the SIGNAL first (the probe below);
    // the picture placement remains only for the row the drop landed in,
    // and for faded stations whose pulse scores under the noise at either
    // level — the registered gap it always was.
    //
    // Only those rows qualify. Rows that merely fail to lock — the warp
    // fixture's first nine, which the window places correctly — are left
    // alone, because letting the picture move them drags the head 41 px off
    // the body (measured; it is why the unrestricted version of this was
    // abandoned).
    std::vector<char> adrift(static_cast<size_t>(out_lines), 0);
    // A row the phase move falls INSIDE of is stretched, not moved (§5b
    // above), and the move across a dropout is routinely bigger than the
    // quarter-line the split search normally allows (measured: 0.32 and
    // 0.41 of a line on the two KiwiSDR dropouts of session 12). The cap
    // exists to stop the search inventing breaks on noisy rows; a row next
    // to a re-locked run has independent evidence the phase really moved,
    // so it is searched over the whole line instead.
    std::vector<char> torn(static_cast<size_t>(out_lines), 0);
    if (opt.autolock && res.per_line_sync) {
        int i = 0;
        while (i < out_lines) {
            if (!unlocked[i]) { i++; continue; }
            int j = i;
            while (j + 1 < out_lines && unlocked[j + 1]) j++;
            if (i > 0 && j + 1 < out_lines && j - i + 1 >= kSegHalf) {
                const double moved =
                    std::fabs((starts[j + 1] - starts[i - 1]) -
                              b * (j + 1 - (i - 1)));
                dlog(st.hooks, LogTopic::kSeams,
                     "dbg: unlocked run rows %d-%d (%d), phase "
                     "moved %.1f smp across it",
                     i, j, j - i + 1, moved);
                if (moved > kNonlinSec * fs) {
                    // The run is bracketed by two known levels: the locked
                    // lines before it (old phase) and after it (new). The
                    // tracker never locked these rows because its narrow
                    // window sat on the old prediction until the re-acquire
                    // sweep fired — but the pulse is IN the audio (Sara,
                    // session 12: KiwiSDR over the internet drops samples;
                    // the signal either side of a drop is intact). So ask
                    // each row directly, at both levels, ±20 samples around
                    // the extrapolated positions. Measured on the three
                    // library dropouts: the far side scores 0.66-0.96, the
                    // near side <= 0.22, and exactly one row per run scores
                    // nothing at either level — the row the drop landed in,
                    // whose pulse it took. That row is left to the split
                    // search and the picture; every other row is placed by
                    // the signal, which no ±120 px picture match can do —
                    // the moves are 574 and 743 PIXELS.
                    bool any = false;
                    for (int m = i; m <= j; m++) {
                        const double l_old =
                            starts[i - 1] + b * (m - (i - 1));
                        const double l_new =
                            starts[j + 1] + b * (m - (j + 1));
                        double so, sn;
                        const double po = fax_best_sync(video, l_old - 20,
                                                    l_old + 20, st.pulse, &so);
                        const double pn = fax_best_sync(video, l_new - 20,
                                                    l_new + 20, st.pulse, &sn);
                        dlog(st.hooks, LogTopic::kSeams,
                             "dbg: run row %d: old %.2f  new %.2f",
                             m, so, sn);
                        if (sn >= lock && so < lock - 0.15) {
                            starts[m] = pn;
                            unlocked[m] = 0;
                            res.relocked_lines++;
                            any = true;
                        } else if (so >= lock && sn < lock - 0.15) {
                            starts[m] = po;
                            unlocked[m] = 0;
                            res.relocked_lines++;
                            any = true;
                        } else {
                            adrift[m] = 1;
                        }
                    }
                    if (any)
                        for (int m = i - 1; m <= j; m++) torn[m] = 1;
                }
            }
            i = j + 1;
        }
    }

    const int rough = width / 4;
    std::vector<uint8_t> cand(width), best(width);
    for (int l = line_lo; l < line_hi; l++) {
        const int row = l - line_lo;
        if ((row & 31) == 0) {
            throw_if_cancelled(st.hooks, "assembly");
            report(st.hooks, "assembly",
                   static_cast<double>(row) / out_lines);
        }
        const double s0 = starts[row];
        const double k =
            (row + 1 < out_lines) ? (starts[row + 1] - s0 - b) : 0.0;
        auto fill = [&](std::vector<uint8_t>& dst, double brk, int step) {
            for (int j = 0; j < width; j += step) {
                const double off = b * j / width;
                const double pos = s0 + off + (off > brk ? k : 0.0);
                dst[j] = static_cast<uint8_t>(
                    std::lround(fax_lerp_at(video, pos) * 255.0f));
            }
        };
        double brk = b;  // no break: the pre-session-11b behaviour
        const double kcap = torn[row] ? b : 0.25 * b;
        if (opt.autolock && row > 0 && std::fabs(k) >= kIntraMin &&
            std::fabs(k) < kcap) {
            const uint8_t* above =
                &img.px[static_cast<size_t>(row - 1) * width];
            auto cost = [&](double p) {
                fill(cand, p, 4);
                long acc = 0;
                for (int j = 0; j < rough * 4; j += 4)
                    acc += std::abs(static_cast<int>(cand[j]) -
                                    static_cast<int>(above[j]));
                return acc;
            };
            const int kSteps = 32;
            long bestc = -1;
            for (int t = 0; t <= kSteps; t++) {
                const double p = b * t / kSteps;
                const long c = cost(p);
                if (bestc < 0 || c < bestc) { bestc = c; brk = p; }
            }
            // ...then refine within the coarse cell it won.
            const double cell = b / kSteps;
            double fine = brk;
            for (int t = -3; t <= 3; t++) {
                const double p = brk + cell * t / 3.0;
                if (p < 0.0 || p > b) continue;
                const long c = cost(p);
                if (c < bestc) { bestc = c; fine = p; }
            }
            brk = fine;
            res.intra_line_breaks++;
        }
        // NOT DONE, and the measurement is why. A row with no sync of its
        // own is drawn where its neighbours are, which is right while
        // nothing moves and wrong at exactly the place where something did:
        // the eight lines bracketing JMH KiwiSDR Himawari's 1270-sample loss
        // carry no lock and sit ~75 px from the rest of the chart. The
        // obvious answer — let the PICTURE place them, by the offset that
        // best matches the row above (fldigi's per-line correlation, kept
        // per line instead of collapsed to a histogram mode) — was built and
        // measured, and it fails a screamer it should not: on the warp
        // fixture, whose first nine rows carry no lock but ARE placed
        // correctly by the ±8-line window, the search drags the head 41 px
        // off the body, and a 3% significance guard does not stop it. On a
        // white-only station, where every row coasts, it is worse still:
        // the page wanders off the phasing anchor and `roundtrip [7]` and
        // `fixture_phasing_anchor` both fail. The idea is right for rows
        // that nothing else can place; telling those apart from rows the
        // window already placed is the unsolved part, and it is the same
        // problem the white-only half of M2b has to solve.
        double draw0 = starts[row];
        // A torn row that was just split is placed by the split, in two
        // pieces; a whole-row offset on top of it would drag both halves
        // off together, so the picture placement stays out of it.
        if (adrift[row] && row > 0 &&
            !(torn[row] && std::fabs(k) >= kIntraMin)) {
            // fldigi computes this correlation per line and keeps only the
            // mode of a histogram of them (docs/00, session 11); here it is
            // kept per line, and asked only where nothing else can answer.
            const uint8_t* above =
                &img.px[static_cast<size_t>(row - 1) * width];
            const double px_smp = b / width;
            auto sad = [&](double off) {
                long acc = 0;
                for (int j = 0; j < rough * 4; j += 4) {
                    const double o = b * j / width;
                    const double pos = draw0 + off + o + (o > brk ? k : 0.0);
                    const int v = static_cast<int>(
                        std::lround(fax_lerp_at(video, pos) * 255.0f));
                    acc += std::abs(v - static_cast<int>(above[j]));
                }
                return acc;
            };
            const long stay = sad(0.0);
            long bestc = stay;
            double bestoff = 0.0;
            for (int t = -kCoastSearchPx; t <= kCoastSearchPx; t += 2) {
                const long acc = sad(t * px_smp);
                if (acc < bestc) { bestc = acc; bestoff = t * px_smp; }
            }
            // These rows are already known to have nothing else placing
            // them — the phase moved across the run they sit in — so any
            // improvement is better evidence than the stale level they
            // would otherwise keep.
            if (bestc < stay) {
                draw0 += bestoff;
                starts[row] = draw0;  // the next adrift row follows this one
                res.picture_placed++;
            }
        }
        for (int j = 0; j < width; j++) {
            const double o = b * j / width;
            const double pos = draw0 + o + (o > brk ? k : 0.0);
            best[j] = static_cast<uint8_t>(
                std::lround(fax_lerp_at(video, pos) * 255.0f));
        }
        std::copy(best.begin(), best.end(),
                  img.px.begin() + static_cast<size_t>(row) * width);
    }

    if (place_n > 0)
        res.place_rms_px = std::sqrt(place_sq / place_n);
    dlog(st.hooks, LogTopic::kInfo,
         "dbg: place rms=%.2f px max=%.1f px over %d locked "
         "drawn lines; %d seam(s), largest %.1f px; %d intra-line "
         "break(s); %d row(s) re-locked past a dropout; %d row(s) "
         "placed by the picture",
         res.place_rms_px, res.place_max_px, place_n, res.seams,
         res.max_seam_px, res.intra_line_breaks,
         res.relocked_lines, res.picture_placed);

    res.img = std::move(img);
    res.lines = out_lines;
}

}  // namespace

DecodeResult decode_fax(const std::vector<float>& video, int fs,
                        const DecodeOptions& opt) {
    if (video.empty())
        throw DecodeError(DecodeErrorKind::kEmptyInput,
                          "decode_fax: empty input");
    DecodeState st(video, fs, opt);
    // The numbered stages of the pre-M4 decode_fax, in the same order,
    // with the same logic. The table is the progress surface: a caller
    // sees each stage as it is entered, and the long ones report their
    // fraction from inside.
    struct Stage {
        const char* name;
        void (*run)(DecodeState&);
    };
    static const Stage kStages[] = {
        {"onset", stage_onset},
        {"dead-sector", stage_dead_sector},
        {"phasing", stage_phasing},
        {"sync-track", stage_track},
        {"period-fit", stage_fit},
        {"segmentation", stage_segment},
        {"timebase", stage_timebase},
        {"change-points", stage_change_points},
        {"assembly", stage_assembly},
    };
    for (const Stage& s : kStages) {
        throw_if_cancelled(opt.hooks, s.name);
        report(opt.hooks, s.name, 0.0);
        s.run(st);
        report(opt.hooks, s.name, 1.0);
    }
    return std::move(st.res);
}

}  // namespace nova
