// test_tones.cpp — M3 screamer tests: control-tone and phasing detection.
//
// Claims defended (docs/01 §3, docs/02):
//   [WMO §5.2.2]   300 Hz = IOC 576 start, 675 Hz = IOC 288 start
//   [WMO §5.2.5]   450 Hz = stop
//   [WMO §5.2.6]   control frequencies hold to ±1% — including the ones
//                  the GENERATOR emits, which is a real regression guard:
//                  before session 6 it emitted 307.7/500/800 Hz for
//                  300/450/675 and nothing noticed.
//   [WMO §5.2.3.1] phasing rate selects the line rate (1.0/1.5/2.0 Hz)
//   [WMO §5.2.3.2] phasing is symmetric OR 5/95 — both must be accepted
//   [WMO §5.2.3.4] leading edge of white = dead-sector entry = line start
//   [ISO §4.2.5]   start/stop detection drives sequencing
//
// The false-start claim is defended by [4] and [5]: picture content must
// never produce a tone event, and the purity margin between content and a
// real tone is asserted, not assumed. The library measurement behind the
// threshold is in docs/00 (session 6): content never exceeded 0.16 across
// 5.9 hours, real tones ran 0.68-0.99, threshold 0.35.
#include "../core/demod.hpp"
#include "../core/gen.hpp"
#include "../core/phasing.hpp"
#include "../core/tones.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

namespace {
int failures = 0;

void check(bool ok, const char* what) {
    std::printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}

constexpr int kFs = 8000;

std::vector<float> make_video(const nova::GenOptions& g, int lines) {
    const int width = (g.ioc == 288) ? 905 : 1810;
    nova::Image content = nova::gen_test_pattern(width, lines);
    std::vector<float> sig = nova::gen_fax_signal(content, lines, g);
    return nova::fm_demod(sig, g.fs, 1900.0, g.deviation);
}

const nova::ToneEvent* find(const std::vector<nova::ToneEvent>& ev,
                            nova::ToneKind k) {
    for (const auto& e : ev)
        if (e.kind == k) return &e;
    return nullptr;
}

int count(const std::vector<nova::ToneEvent>& ev, nova::ToneKind k) {
    int n = 0;
    for (const auto& e : ev)
        if (e.kind == k) n++;
    return n;
}

// Highest purity any window of `v` shows in the band around `nominal` —
// the false-positive figure of merit.
double max_purity(const std::vector<float>& v, double nominal) {
    nova::ToneOptions o;
    const size_t n = static_cast<size_t>(o.win_sec * kFs);
    const size_t hop = static_cast<size_t>(o.hop_sec * kFs);
    double mx = 0.0;
    for (size_t s = 0; s + n <= v.size(); s += hop)
        mx = std::max(mx, nova::tone_purity_band(v, s, n, kFs, nominal,
                                                 o.tol));
    return mx;
}
}  // namespace

int main() {
    std::printf("[1] start tone, IOC 576 [WMO §5.2.2]\n");
    {
        nova::GenOptions g;
        std::vector<float> v = make_video(g, 120);
        auto ev = nova::detect_tones(v, kFs);
        const nova::ToneEvent* s = find(ev, nova::ToneKind::kStartIOC576);
        check(s != nullptr, "300 Hz start detected");
        if (s) {
            std::printf("    f=%.2f Hz t=%.2f-%.2f s purity=%.3f\n",
                        s->freq_hz, s->t_start, s->t_end, s->purity);
            // ±1% of 300 Hz is ±3 Hz [WMO §5.2.6].
            check(std::fabs(s->freq_hz - 300.0) <= 3.0,
                  "measured within the ±1% of WMO §5.2.6");
            check(s->t_start < 0.5 && std::fabs(s->t_end - 5.0) < 0.5,
                  "spans the generated 5 s tone");
        }
        check(count(ev, nova::ToneKind::kStartIOC288) == 0,
              "not mistaken for a 675 Hz IOC-288 start");
    }

    std::printf("[2] start tone, IOC 288 [WMO §5.2.2]\n");
    {
        nova::GenOptions g;
        g.ioc = 288;
        std::vector<float> v = make_video(g, 120);
        auto ev = nova::detect_tones(v, kFs);
        const nova::ToneEvent* s = find(ev, nova::ToneKind::kStartIOC288);
        check(s != nullptr, "675 Hz start detected");
        if (s) {
            std::printf("    f=%.2f Hz purity=%.3f\n", s->freq_hz, s->purity);
            check(std::fabs(s->freq_hz - 675.0) <= 6.75,
                  "measured within the ±1% of WMO §5.2.6");
        }
        check(count(ev, nova::ToneKind::kStartIOC576) == 0,
              "not mistaken for a 300 Hz IOC-576 start");
    }

    std::printf("[3] stop tone [WMO §5.2.5]\n");
    {
        nova::GenOptions g;
        std::vector<float> v = make_video(g, 120);
        auto ev = nova::detect_tones(v, kFs);
        const nova::ToneEvent* s = find(ev, nova::ToneKind::kStop);
        check(s != nullptr, "450 Hz stop detected");
        if (s) {
            std::printf("    f=%.2f Hz t=%.2f-%.2f s purity=%.3f\n",
                        s->freq_hz, s->t_start, s->t_end, s->purity);
            check(std::fabs(s->freq_hz - 450.0) <= 4.5,
                  "measured within the ±1% of WMO §5.2.6");
            // 5 s start + 30 phasing lines + 120 image lines at 120 lpm.
            const double expect = 5.0 + (30 + 120) * 0.5;
            check(std::fabs(s->t_start - expect) < 1.0,
                  "lands where the transmission sequence puts it");
        }
    }

    std::printf("[4] FALSE START: picture content alone [M3 trap]\n");
    {
        // No control signals at all — only image lines. Any event here is
        // a false positive, which is the failure this whole detector is
        // designed against.
        nova::GenOptions g;
        g.start_tone = false;
        g.stop_tone = false;
        g.phasing = false;
        std::vector<float> v = make_video(g, 400);
        auto ev = nova::detect_tones(v, kFs);
        std::printf("    %zu event(s) on 200 s of picture content\n",
                    ev.size());
        for (const auto& e : ev)
            std::printf("    unexpected %s at %.2f s purity %.3f\n",
                        nova::tone_name(e.kind), e.t_start, e.purity);
        check(ev.empty(), "no tone invented from picture content");
    }

    std::printf("[5] purity margin, content vs tone\n");
    {
        nova::GenOptions g;
        g.start_tone = false;
        g.stop_tone = false;
        g.phasing = false;
        std::vector<float> content = make_video(g, 400);
        const double c300 = max_purity(content, 300.0);
        const double c450 = max_purity(content, 450.0);
        const double c675 = max_purity(content, 675.0);
        std::printf("    content max purity: 300=%.3f 450=%.3f 675=%.3f\n",
                    c300, c450, c675);
        // The threshold has to sit in a gap, not on an edge. Library
        // measurement puts real content at <=0.16 (docs/00, session 6).
        check(std::max({c300, c450, c675}) < 0.25,
              "content stays well below the 0.35 accept threshold");
    }

    std::printf("[6] tones survive noise\n");
    {
        nova::GenOptions g;
        g.noise = 0.05;
        std::vector<float> v = make_video(g, 120);
        auto ev = nova::detect_tones(v, kFs);
        check(find(ev, nova::ToneKind::kStartIOC576) != nullptr,
              "start still detected at 0.05 noise");
        check(find(ev, nova::ToneKind::kStop) != nullptr,
              "stop still detected at 0.05 noise");
    }

    std::printf("[7] phasing: rate selection [WMO §5.2.3.1]\n");
    {
        for (int lpm : {60, 90, 120}) {
            nova::GenOptions g;
            g.lpm = lpm;
            std::vector<float> v = make_video(g, 60);
            // The rate is recovered by trying all three, as the decoder
            // must: the phasing signal itself says which one is right.
            int best_lpm = 0;
            nova::PhasingResult best;
            for (int cand : {60, 90, 120}) {
                nova::PhasingResult p =
                    nova::detect_phasing(v, kFs, kFs * 60.0 / cand);
                // A harmonic can contain MORE wedge-shaped windows (a 1 Hz
                // phasing signal has two candidate windows at 2 Hz), so
                // line count alone convicts the wrong rate. Agreement is
                // the rate evidence: measured after edge refinement, the
                // true rate spreads 0.0 samples while the 60->120 harmonic
                // spreads 35.5.
                if (p.found &&
                    (!best.found || p.spread < best.spread - 1.0 ||
                     (std::fabs(p.spread - best.spread) <= 1.0 &&
                      p.lines > best.lines))) {
                    best = p;
                    best_lpm = cand;
                }
            }
            std::printf("    generated %3d lpm -> detected %3d lpm, "
                        "%d lines, spread %.1f smp\n",
                        lpm, best_lpm, best.lines, best.spread);
            check(best_lpm == lpm, "phasing recovers the line rate");
            check(best.found && best.asymmetric,
                  "5/95 waveform identified [WMO §5.2.3.2]");
        }
    }

    std::printf("[8] phasing: symmetric 50/50 waveform [WMO §5.2.3.2]\n");
    {
        nova::GenOptions g;
        g.phasing_symmetric = true;
        std::vector<float> v = make_video(g, 60);
        nova::PhasingResult p = nova::detect_phasing(v, kFs, kFs * 0.5);
        std::printf("    found=%d lines=%d asymmetric=%d spread=%.1f\n",
                    p.found ? 1 : 0, p.lines, p.asymmetric ? 1 : 0, p.spread);
        check(p.found, "symmetric phasing detected at all");
        check(!p.asymmetric, "reported as symmetric, not 5/95");
    }

    std::printf("[9] phasing: not invented from picture content\n");
    {
        nova::GenOptions g;
        g.start_tone = false;
        g.stop_tone = false;
        g.phasing = false;
        std::vector<float> v = make_video(g, 400);
        nova::PhasingResult p = nova::detect_phasing(v, kFs, kFs * 0.5);
        std::printf("    found=%d lines=%d\n", p.found ? 1 : 0, p.lines);
        check(!p.found, "no phasing found in image lines");
    }

    std::printf("[10] phasing marks dead-sector entry [WMO §5.2.3.4]\n");
    {
        // The generator puts the phasing white run, and the image dead
        // sector, at the same place in the line — as the spec requires.
        // So the phasing line_start must agree with the line grid the
        // signal was built on, within the demodulator's group delay.
        nova::GenOptions g;
        std::vector<float> v = make_video(g, 60);
        nova::PhasingResult p = nova::detect_phasing(v, kFs, kFs * 0.5);
        check(p.found, "phasing found");
        if (p.found) {
            const double period = kFs * 0.5;
            // The 5 s start tone is exactly 10 lines at 120 lpm, so the
            // generated line grid is aligned to sample 0 and the expected
            // answer is 0, offset only by the demod's group delay (63-tap
            // FIR -> 31 samples) — under 1.5% of the line either way.
            double d = p.line_start;
            if (d > period / 2) d -= period;
            std::printf("    line_start=%.1f smp (%.2f%% of the line)\n",
                        p.line_start, 100.0 * d / period);
            check(std::fabs(d) < 0.015 * period,
                  "white leading edge within 1.5% of the true line start");
            check(p.spread < 0.01 * period,
                  "per-line positions agree to under 1% of a line");
        }
    }

    std::printf("[11] phasing anchor is an ABSOLUTE line start "
                "[WMO §5.2.3.4]\n");
    {
        // `line_start` is a residue modulo the truncated integer period;
        // `anchor` is the position a decoder actually consumes. Two ways it
        // can be wrong while line_start stays perfect, both found by
        // measurement in session 7 and both pinned here:
        //
        //   (a) PARITY. Referring the run to its midpoint refers it to a
        //       half-line when the run has an even number of lines, putting
        //       the anchor exactly half a period out. 30 lines is even, so
        //       the whole library read half a line off and every synthetic
        //       test still passed. Both parities are generated here.
        //   (b) GRID SLIP. The detector counts lines in whole samples while
        //       a real clock is never nominal, so the integer grid walks
        //       against the signal; at -137 ppm over 30 lines that is
        //       ~16 samples of pure bias.
        for (int nph : {30, 31}) {
            for (double ppm : {0.0, -137.0}) {
                nova::GenOptions g;
                g.phasing_lines = nph;
                g.ppm = ppm;
                const double period = kFs * 0.5 * (1.0 + ppm * 1e-6);
                std::vector<float> v = make_video(g, 60);
                nova::PhasingResult p = nova::detect_phasing(v, kFs, period);
                char what[96];
                std::snprintf(what, sizeof what,
                              "%d phasing lines at %+.0f ppm", nph, ppm);
                if (!p.found) {
                    check(false, what);
                    continue;
                }
                // The generated grid starts at sample 0 (the 5 s start tone
                // is a whole number of lines), so every line's white edge is
                // at a multiple of the period, offset only by the demod's
                // 63-tap group delay.
                const double kDelay = 31.0;
                double d = std::fmod(p.anchor - kDelay, period);
                if (d > period / 2) d -= period;
                if (d < -period / 2) d += period;
                std::printf("    %s: anchor=%.1f  off-grid %+.1f smp "
                            "(%.2f%% of the line)\n",
                            what, p.anchor, d, 100.0 * d / period);
                check(std::fabs(d) < 0.015 * period, what);
                // ...and it must be a position INSIDE the interval it was
                // measured on, not an extrapolation to somewhere else.
                check(p.anchor >= p.t_start * kFs - period &&
                          p.anchor <= p.t_end * kFs + period,
                      "anchor lies within the phasing interval");
            }
        }
    }

    std::printf("[12] phasing survives a FADED interval [WMO §5.2.3]\n");
    {
        // GYA 2300Z, measured session 10: a real 40-line phasing interval
        // whose per-line scores run 0.34-0.88 — reaching BELOW the 0.48-0.62
        // band that dark picture content scores — because fading cuts the
        // contrast the score measures while leaving the edge where it was.
        // Score alone therefore cannot decide membership on a faded signal.
        // Growing runs from consecutive above-threshold lines chopped that
        // interval into ten fragments of one to six lines and reported no
        // phasing at all, on the one station in the library that has no
        // other source of line phase.
        //
        // The fade is injected into the AUDIO and only over the phasing
        // interval, so the picture, the tones and the clock are untouched
        // and the single thing under test is whether the run survives.
        nova::GenOptions g;
        nova::Image content = nova::gen_test_pattern(1810, 60);
        std::vector<float> sig = nova::gen_fax_signal(content, 60, g);
        const size_t t0 = static_cast<size_t>(5.0 * kFs);  // 5 s start tone
        const size_t t1 =
            t0 + static_cast<size_t>(g.phasing_lines * 0.5 * kFs);
        std::mt19937 rng(20260812u);
        std::normal_distribution<float> noise(0.0f, 0.20f);
        std::vector<float> faded = sig;
        for (size_t i = t0; i < std::min(t1, faded.size()); i++)
            faded[i] += noise(rng);
        std::vector<float> v =
            nova::fm_demod(faded, g.fs, 1900.0, g.deviation);

        // The fade has to be deep enough that the OLD rule could not have
        // passed this test by luck: most lines must fail min_score, and the
        // longest run of consecutive passing lines must be shorter than
        // min_lines. Asserted, not assumed — otherwise a change to the
        // generator could quietly turn this back into the easy case.
        const nova::PhasingOptions defaults;
        const double period = kFs * 0.5;
        int weak = 0, best_consec = 0, consec = 0;
        for (int l = 0; l < g.phasing_lines; l++) {
            const size_t s = t0 + static_cast<size_t>(l * period);
            double wsum = 0.0, total = 0.0;
            const size_t wlen = static_cast<size_t>(0.05 * period);
            for (size_t k = 0; k < static_cast<size_t>(period); k++) {
                if (s + k >= v.size()) break;
                total += v[s + k];
                if (k < wlen) wsum += v[s + k];
            }
            const double sc = wsum / wlen -
                              (total - wsum) / (period - wlen);
            if (sc < defaults.min_score) {
                weak++;
                consec = 0;
            } else if (++consec > best_consec) {
                best_consec = consec;
            }
        }
        std::printf("    fade: %d of %d lines below min_score, longest "
                    "consecutive run %d (min_lines %d)\n",
                    weak, g.phasing_lines, best_consec, defaults.min_lines);
        check(weak * 2 > g.phasing_lines,
              "the fade really does put most lines under the score floor");
        check(best_consec < defaults.min_lines,
              "...and no consecutive-only run could reach min_lines");

        nova::PhasingResult p = nova::detect_phasing(v, kFs, period);
        std::printf("    found=%d lines=%d of %d  spread=%.1f  score=%.3f\n",
                    p.found ? 1 : 0, p.lines, g.phasing_lines, p.spread,
                    p.score);
        check(p.found, "faded phasing interval is still found (screamer)");
        if (p.found) {
            check(p.lines >= defaults.min_lines,
                  "enough of its lines are recovered");
            // And it is still the RIGHT interval: same grid as [11], same
            // tolerance. Finding a run is worth nothing if its anchor is
            // wrong — that is how a white-only station gets drawn rotated.
            const double kDelay = 31.0;
            double d = std::fmod(p.anchor - kDelay, period);
            if (d > period / 2) d -= period;
            if (d < -period / 2) d += period;
            std::printf("    anchor=%.1f  off-grid %+.1f smp (%.2f%%)\n",
                        p.anchor, d, 100.0 * d / period);
            check(std::fabs(d) < 0.015 * period,
                  "...and the anchor still lands on the true line start");
        }

        // The other half of the rule: position agreement is what carries the
        // run, so a stretch that fades and never agrees on WHERE the white
        // is must NOT be joined up into one. Same fade depth, applied to
        // picture content with no phasing in it at all.
        nova::GenOptions gp;
        gp.phasing = false;
        gp.start_tone = false;
        gp.stop_tone = false;
        std::vector<float> psig = nova::gen_fax_signal(content, 60, gp);
        std::mt19937 rng2(20260813u);
        for (size_t i = 0; i < psig.size(); i++) psig[i] += noise(rng2);
        std::vector<float> pv =
            nova::fm_demod(psig, gp.fs, 1900.0, gp.deviation);
        nova::PhasingResult q = nova::detect_phasing(pv, kFs, period);
        std::printf("    faded picture content: found=%d lines=%d\n",
                    q.found ? 1 : 0, q.lines);
        check(!q.found, "a faded stretch of picture is not phasing");
    }

    std::printf("[13] which phasing interval, when there are two\n");
    {
        // Two openings, one picture — FAXSignal's shape, built with ground
        // truth: phasing, a gap, a SECOND and longer phasing, then image.
        // Three rules give three different answers here and the library
        // shows all three are reachable, so both branches are pinned.
        nova::GenOptions g;
        nova::Image content = nova::gen_test_pattern(1810, 60);
        std::vector<float> a = nova::gen_fax_signal(content, 60, g);
        nova::GenOptions g2;
        g2.start_tone = false;
        g2.stop_tone = false;
        g2.phasing_lines = 45;  // deliberately LONGER than the first
        std::vector<float> b = nova::gen_fax_signal(content, 60, g2);
        std::vector<float> both = a;
        both.insert(both.end(), b.begin(), b.end());
        std::vector<float> v = nova::fm_demod(both, g.fs, 1900.0, g.deviation);
        const double period = kFs * 0.5;
        const double first_t0 = 5.0;                        // after the tone
        const double second_t0 = a.size() / double(g.fs);   // start of b

        // No window: the FIRST, because a later run may belong to a
        // transmission this decode is not drawing (`jmh sample`).
        nova::PhasingResult f = nova::detect_phasing(v, kFs, period);
        std::printf("    no window  -> %.2f-%.2f s (%d lines)\n", f.t_start,
                    f.t_end, f.lines);
        check(f.found && std::fabs(f.t_start - first_t0) < 1.0,
              "with no transmission bounds, the first opening wins");
        check(f.lines < 45, "...and not simply the longest run");

        // Windowed: the LAST inside it, because that is the opening the
        // picture begins after (FAXSignal).
        nova::PhasingOptions o;
        o.t_lo = first_t0;
        o.t_hi = both.size() / double(g.fs);
        nova::PhasingResult l = nova::detect_phasing(v, kFs, period, o);
        std::printf("    windowed   -> %.2f-%.2f s (%d lines)\n", l.t_start,
                    l.t_end, l.lines);
        check(l.found && l.t_start > second_t0 - 1.0,
              "inside a known transmission, the last opening wins");

        // ...and a run beyond the window is not eligible at all, which is
        // what keeps the next transmission's phasing out.
        nova::PhasingOptions o2;
        o2.t_lo = first_t0;
        o2.t_hi = second_t0 - 1.0;
        nova::PhasingResult c = nova::detect_phasing(v, kFs, period, o2);
        std::printf("    cut window -> %.2f-%.2f s (%d lines)\n", c.t_start,
                    c.t_end, c.lines);
        check(c.found && std::fabs(c.t_start - first_t0) < 1.0,
              "a run past the stop tone is not eligible");
    }

    std::printf(failures ? "\n%d FAILURE(S)\n" : "\nall tests passed\n",
                failures);
    return failures ? 1 : 0;
}
