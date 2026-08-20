// nova-tones — report the control tones in a recording, and (with --dump)
// the per-window purity that the decision rests on.
//
// This is the survey instrument for the false-start question: run it over
// picture-only recordings and every reported event is a false positive.
#include "../core/demod.hpp"
#include "../core/phasing.hpp"
#include "../core/resample.hpp"
#include "../core/tones.hpp"
#include "../core/wav.hpp"
#include "../core/version_flag.hpp"
#include "env_hooks.hpp"
#include "internal_rate.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

void usage() {
    std::fprintf(stderr,
                 "usage: nova-tones in.wav [--dump] [--purity P] "
                 "[--win SEC] [--max N]\n"
                 "       nova-tones --version\n");
}

// Fill opt/dump/max_sec from the command line; false means bad arguments
// (the complaint already printed) and main exits 2.
bool parse_args(int argc, char** argv, nova::ToneOptions& opt, bool& dump,
                double& max_sec) {
    if (argc < 2) {
        usage();
        return false;
    }
    for (int i = 2; i < argc; i++) {
        if (!std::strcmp(argv[i], "--dump"))
            dump = true;
        else if (!std::strcmp(argv[i], "--purity") && i + 1 < argc)
            opt.purity = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--win") && i + 1 < argc)
            opt.win_sec = std::atof(argv[++i]);
        // Widen the search band to ask "what frequency is this transmitter
        // actually sending?" rather than "is it in tolerance?".
        else if (!std::strcmp(argv[i], "--tol") && i + 1 < argc)
            opt.tol = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--spread") && i + 1 < argc)
            opt.max_spread = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--max") && i + 1 < argc)
            max_sec = std::atof(argv[++i]);
        else {
            std::fprintf(stderr, "nova-tones: bad arg %s\n", argv[i]);
            return false;
        }
    }
    return true;
}

// Phasing carries the line rate itself [WMO §5.2.3.1], so all three
// rates are tried and the longest coherent run wins — the answer is
// measured here, not taken from the comb.
void report_phasing(const std::vector<float>& video,
                    const nova::DecodeHooks& hooks) {
    nova::PhasingResult best;
    int best_lpm = 0;
    for (int lpm : {60, 90, 120}) {
        nova::PhasingResult p = nova::detect_phasing(
            video, nova::kInternalRate, nova::kInternalRate * 60.0 / lpm,
            nova::PhasingOptions(), hooks);
        if (p.found && p.lines > best.lines) {
            best = p;
            best_lpm = lpm;
        }
    }
    if (best.found)
        std::printf("  phasing    %7.2f - %7.2f s  (%d lines @ %d lpm)  "
                    "line_start=%.0f smp  anchor=%.1f  spread=%.1f  %s  "
                    "score=%.2f\n",
                    best.t_start, best.t_end, best.lines, best_lpm,
                    best.line_start, best.anchor, best.spread,
                    best.asymmetric ? "5/95" : "50/50", best.score);
    else
        std::printf("  phasing    none\n");
}
}  // namespace

int main(int argc, char** argv) {
    // Ahead of the argument-count check: --version is a question
    // about the program, not about a decode [E-GAP-001].
    if (nova::handled_version_flag(argc, argv, "nova-tones")) return 0;
    nova::ToneOptions opt;
    bool dump = false;
    double max_sec = 0.0;
    if (!parse_args(argc, argv, opt, dump, max_sec)) return 2;

    try {
        nova::Wav w = nova::read_wav(argv[1]);
        std::vector<float> mono =
            nova::resample(w.samples, w.sample_rate, nova::kInternalRate);
        if (max_sec > 0.0) {
            const size_t lim =
                static_cast<size_t>(max_sec * nova::kInternalRate);
            if (mono.size() > lim) mono.resize(lim);
        }
        // 1900 Hz: the WEFAX audio subcarrier centre frequency [WMO §5.5.1].
        std::vector<float> video =
            nova::fm_demod(mono, nova::kInternalRate, 1900.0, 400.0);
        const nova::DecodeHooks hooks = nova::hooks_from_env();

        if (dump) {
            const size_t n =
                static_cast<size_t>(opt.win_sec * nova::kInternalRate);
            const size_t hop =
                static_cast<size_t>(opt.hop_sec * nova::kInternalRate);
            // Band maxima, i.e. exactly the quantity the accept rule reads.
            std::printf("# t_sec p300 p450 p675\n");
            for (size_t s = 0; s + n <= video.size(); s += hop)
                std::printf("%.3f %.4f %.4f %.4f\n",
                            static_cast<double>(s) / nova::kInternalRate,
                            nova::tone_purity_band(video, s, n,
                                                   nova::kInternalRate,
                                                   300.0, opt.tol),
                            nova::tone_purity_band(video, s, n,
                                                   nova::kInternalRate,
                                                   450.0, opt.tol),
                            nova::tone_purity_band(video, s, n,
                                                   nova::kInternalRate,
                                                   675.0, opt.tol));
            return 0;
        }

        std::vector<nova::ToneEvent> ev =
            nova::detect_tones(video, nova::kInternalRate, opt, hooks);
        std::printf("%s: %zu tone(s)\n", argv[1], ev.size());
        for (const auto& e : ev)
            std::printf("  %-10s %7.2f - %7.2f s  (%.2f s)  f=%6.1f Hz  "
                        "purity=%.3f\n",
                        nova::tone_name(e.kind), e.t_start, e.t_end,
                        e.t_end - e.t_start, e.freq_hz, e.purity);
        report_phasing(video, hooks);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "nova-tones: %s\n", e.what());
        return 1;
    }
    return 0;
}
