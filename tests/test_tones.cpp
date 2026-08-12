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
                if (p.found && p.lines > best.lines) {
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

    std::printf(failures ? "\n%d FAILURE(S)\n" : "\nall tests passed\n",
                failures);
    return failures ? 1 : 0;
}
