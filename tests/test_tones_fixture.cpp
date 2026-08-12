// test_tones_fixture.cpp — M3 fixture screamers: control signals in real
// off-air recordings.
//
// Why this exists separately from test_tones.cpp: the synthetic false-start
// test is far too easy. Generated picture content peaks at purity 0.001 in
// the control bands, while REAL recordings reach 0.16 (library survey,
// session 6) — two orders of magnitude harder. A threshold justified only
// against synthetic content would be justified against nothing.
//
// Fixtures used:
//   vmw-start-phasing-100s.wav — VMW (Australian BOM) 2230Z, first 100 s.
//     Carries a real 300 Hz start tone (56.9-62.0 s) followed immediately
//     by a real 30 s phasing interval (62.0-92.0 s). VMW is a WHITE-ONLY
//     station: its dead sector holds no per-line sync at all (session 4),
//     so the phasing stage is the only place its line-start phase can come
//     from [WMO §5.2.3.4]. This fixture is the evidence that it is there.
//   kyodo-news-jsc1-60lpm-120s.wav — JSC1 Kyodo News, 60..180 s. Dense
//     newspaper text: the roadmap's named false-start trap. The parent
//     recording carries no control signal anywhere in its 1606 s, so any
//     event reported here is a false positive.
//
// Claims defended: [WMO §5.2.2] start tone, [WMO §5.2.3.1-.4] phasing rate,
// waveform and line-start reference, [WMO §5.2.6] ±1% frequency.
#include "../core/demod.hpp"
#include "../core/phasing.hpp"
#include "../core/tones.hpp"
#include "../core/wav.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
int failures = 0;

void check(bool ok, const char* what) {
    std::printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: nova-test-tones-fixture <path> --expect-none\n"
                     "       nova-test-tones-fixture <path> --expect-start "
                     "<at_sec> <tol> --expect-phasing <lpm> <min_lines> "
                     "<max_spread>\n");
        return 2;
    }
    const char* path = argv[1];

    nova::Wav w = nova::read_wav(path);
    std::vector<float> video =
        nova::fm_demod(w.samples, w.sample_rate, 1900.0, 400.0);
    std::vector<nova::ToneEvent> ev =
        nova::detect_tones(video, w.sample_rate);

    if (!std::strcmp(argv[2], "--expect-none")) {
        // The false-start screamer, on real text-heavy content.
        std::printf("  %zu tone event(s) on %s\n", ev.size(), path);
        for (const auto& e : ev)
            std::printf("    unexpected %s %.2f-%.2f s purity %.3f\n",
                        nova::tone_name(e.kind), e.t_start, e.t_end,
                        e.purity);
        check(ev.empty(), "no control tone invented from newspaper text");

        nova::PhasingResult p = nova::detect_phasing(
            video, w.sample_rate, w.sample_rate * 60.0 / 60);
        if (p.found)
            std::printf("    unexpected phasing %.2f-%.2f s (%d lines)\n",
                        p.t_start, p.t_end, p.lines);
        check(!p.found, "no phasing invented from newspaper text");
        std::printf(failures ? "\n%d FAILURE(S)\n" : "\nall checks passed\n",
                    failures);
        return failures ? 1 : 0;
    }

    // --expect-start <at> <tol> --expect-phasing <lpm> <min_lines> <spread>
    if (argc != 9) {
        std::fprintf(stderr, "test_tones_fixture: bad argument count\n");
        return 2;
    }
    const double want_at = std::atof(argv[3]);
    const double tol = std::atof(argv[4]);
    const int want_lpm = std::atoi(argv[6]);
    const int min_lines = std::atoi(argv[7]);
    const double max_spread = std::atof(argv[8]);

    const nova::ToneEvent* s = nullptr;
    for (const auto& e : ev)
        if (e.kind == nova::ToneKind::kStartIOC576) s = &e;
    check(s != nullptr, "300 Hz start tone found in a real recording");
    if (s) {
        std::printf("    start %.2f-%.2f s  f=%.2f Hz  purity=%.3f\n",
                    s->t_start, s->t_end, s->freq_hz, s->purity);
        check(std::fabs(s->t_start - want_at) <= tol,
              "start tone at the expected time");
        check(std::fabs(s->freq_hz - 300.0) <= 3.0,
              "within the ±1% of WMO §5.2.6");
    }

    nova::PhasingResult best;
    int best_lpm = 0;
    for (int cand : {60, 90, 120}) {
        nova::PhasingResult p = nova::detect_phasing(
            video, w.sample_rate, w.sample_rate * 60.0 / cand);
        if (p.found && p.lines > best.lines) {
            best = p;
            best_lpm = cand;
        }
    }
    check(best.found, "phasing interval found in a real recording");
    if (best.found) {
        std::printf("    phasing %.2f-%.2f s  %d lines @ %d lpm  "
                    "line_start=%.0f  spread=%.1f  %s  score=%.2f\n",
                    best.t_start, best.t_end, best.lines, best_lpm,
                    best.line_start, best.spread,
                    best.asymmetric ? "5/95" : "50/50", best.score);
        check(best_lpm == want_lpm, "phasing recovers the line rate");
        check(best.lines >= min_lines, "enough phasing lines agreed");
        check(best.spread <= max_spread, "per-line positions agree tightly");
        // The structural check that no purity or score threshold can fake:
        // the phasing interval must begin where the start tone ends
        // [WMO §5.2.3, transmission sequence in docs/01 §4]. Two detectors
        // sharing no code have to agree on a boundary neither was told.
        if (s)
            check(std::fabs(best.t_start - s->t_end) < 1.0,
                  "phasing begins where the start tone ends");
    }

    std::printf(failures ? "\n%d FAILURE(S)\n" : "\nall checks passed\n",
                failures);
    return failures ? 1 : 0;
}
