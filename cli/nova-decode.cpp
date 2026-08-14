// nova-decode — decode a WAV recording of a WEFAX signal to a PGM image.
#include "../core/demod.hpp"
#include "../core/fax.hpp"
#include "../core/image.hpp"
#include "../core/resample.hpp"
#include "../core/wav.hpp"
#include "env_hooks.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
constexpr int kInternalRate = 8000;

// What the phasing witness had to say, in words. Each of these is a
// DIFFERENT fact about the recording and they used to print as one.
const char* phasing_witness_text(const nova::DecodeResult& r) {
    switch (r.phasing_witness) {
        case nova::PhasingWitness::kNone:
            return "no phasing interval";
        case nova::PhasingWitness::kTooShort:
            return "phasing interval too short to fit";
        case nova::PhasingWitness::kNoisy:
            return "phasing edge too noisy to resolve a step";
        case nova::PhasingWitness::kOneSkip:
            return "phasing edge takes one jump, which is not a rate";
        case nova::PhasingWitness::kStraight:
            return "phasing edge straight";
        case nova::PhasingWitness::kSteps:
            return "phasing edge steps";
    }
    return "";
}

void usage() {
    std::fprintf(stderr,
                 "usage: nova-decode in.wav out.pgm [--lpm 60|90|120] "
                 "[--ioc 288|576] [--dev 150|400] [--start SEC] "
                 "[--no-autolock] [--no-phasing] [--no-segment] "
                 "[--phase FRAC] [--sync PPM]\n");
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        usage();
        return 2;
    }
    nova::DecodeOptions opt;
    double deviation = 400.0;
    for (int i = 3; i < argc; i++) {
        if (!std::strcmp(argv[i], "--lpm") && i + 1 < argc)
            opt.lpm = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--ioc") && i + 1 < argc)
            opt.ioc = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--dev") && i + 1 < argc)
            deviation = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--start") && i + 1 < argc)
            opt.start_sec = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--no-autolock"))
            opt.autolock = false;
        else if (!std::strcmp(argv[i], "--no-phasing"))
            opt.use_phasing = false;
        else if (!std::strcmp(argv[i], "--no-segment"))
            opt.segment = false;
        // The operator's two corrections [docs/05 §7.1], on the command
        // line for the same reason `nova-preview` carries them: they are
        // asymmetric, and the difference is only visible by running the
        // same recording both ways. --phase is a fraction of the line;
        // --sync is ppm, and is used only where the fit has no baseline.
        else if (!std::strcmp(argv[i], "--phase") && i + 1 < argc)
            opt.phase_anchor_hint = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--sync") && i + 1 < argc)
            opt.clock_ppm_fallback = std::atof(argv[++i]);
        else {
            usage();
            return 2;
        }
    }
    opt.hooks = nova::hooks_from_env();
    const bool bad_lpm = opt.lpm != 0 && opt.lpm != 60 && opt.lpm != 90 &&
                         opt.lpm != 120;
    const bool bad_ioc = opt.ioc != 0 && opt.ioc != 288 && opt.ioc != 576;
    if (bad_lpm || bad_ioc ||
        (deviation != 150.0 && deviation != 400.0)) {
        usage();
        return 2;
    }

    try {
        nova::Wav w = nova::read_wav(argv[1]);
        std::vector<float> mono =
            nova::resample(w.samples, w.sample_rate, kInternalRate);
        std::vector<float> video =
            nova::fm_demod(mono, kInternalRate, 1900.0, deviation);
        nova::DecodeResult r = nova::decode_fax(video, kInternalRate, opt);
        nova::write_pgm(argv[2], r.img);
        std::printf(
            "lpm=%d (measured %.3f)  ioc=%d  dev=%.0f Hz  clock=%+.1f ppm  "
            "lines=%d  locked=%d  clamped=%d  max_step=%.2f px  "
            "dead=%s(%.2f)%s\n",
            r.lpm, 60.0 / r.line_period_s, r.ioc, deviation, r.clock_ppm,
            r.lines, r.locked_lines, r.clamped_corrections, r.max_step_px,
            r.dead_sector == nova::DeadSector::kBlackPulse ? "pulse"
                                                           : "white",
            r.dead_consistency, r.per_line_sync ? "" : " no-per-line-sync");
        // How straight the drawn line starts came out, which is the number
        // an operator can check against the picture in front of them: the
        // dead sector's edge should be a straight vertical line, and this is
        // how far from straight it is. Only printed where per-line sync
        // exists, because nothing else can measure it (session 11).
        if (r.per_line_sync) {
            std::printf("  place   %.2f px rms, worst %.1f px", r.place_rms_px,
                        r.place_max_px);
            if (r.seams)
                std::printf("; %d seam(s) followed, largest %.1f px",
                            r.seams, r.max_seam_px);
            if (r.relocked_lines)
                std::printf("; %d dropped row(s) re-locked at the far side",
                            r.relocked_lines);
            std::printf("\n");
        }
        // What the operator's corrections actually did [docs/05 §7.1].
        // Printed only when one was given, and printed even when it was
        // ignored — a value that vanished silently is the whole failure
        // mode this pair is designed around.
        if (r.anchor_from_hint || opt.phase_anchor_hint >= 0.0)
            std::printf("  PHASE hint %.4f of a line: %s\n",
                        opt.phase_anchor_hint,
                        r.anchor_from_hint ? "seeded the anchor search"
                                           : "NOT USED");
        if (!std::isnan(opt.clock_ppm_fallback))
            std::printf("  SYNC %+.1f ppm: %s\n", opt.clock_ppm_fallback,
                        r.clock_from_fallback
                            ? "used — the fit had no baseline"
                            : "outranked by the fit, as designed");
        if (r.phasing_found)
            std::printf("  phasing %.2f-%.2f s  anchor delta %+.1f smp vs "
                        "image  (%s)\n",
                        r.phasing_t_start, r.phasing_t_end,
                        r.phasing_anchor_delta,
                        r.anchor_from_hint
                            ? "OPERATOR hint used"
                            : (r.anchor_from_phasing ? "PHASING anchor used"
                                                     : "image anchor used"));
        else
            std::printf("  phasing none\n");
        switch (r.timebase) {
            case nova::Timebase::kSteps:
                // Loud, because it changes what the line above it means.
                if (r.timebase_lines > 0)
                    std::printf("  timebase NOT LINEAR: %d stepped line(s), "
                                "%.1f per 1000 over %d.\n",
                                r.timebase_step_lines, r.timebase_step_rate,
                                r.timebase_lines);
                else
                    std::printf("  timebase NOT LINEAR: phasing edge %.1f "
                                "smp off straight in %d step(s), noise "
                                "%.1f.\n",
                                r.phasing_nonlinearity, r.phasing_steps,
                                r.phasing_roughness);
                std::printf("           The clock figure above is this "
                            "recording's clock PLUS its insertion rate, and "
                            "the anchor delta\n"
                            "           is not comparable with other "
                            "recordings. Where lines lock, the steps are "
                            "CORRECTED as well as\n"
                            "           counted (session 11): the picture is "
                            "drawn segment by segment, and `place` below "
                            "says how\n"
                            "           far from the signal the drawn lines "
                            "ended up.\n");
                break;
            case nova::Timebase::kLinear:
                // Only the evidence that exists. A white-only station has
                // no tracked residual to measure, and a recording with no
                // phasing interval has no edge to fit — printing 0.0 for
                // either would read as a measurement rather than a gap.
                std::printf("  timebase linear (");
                if (r.timebase_lines > 0)
                    std::printf("%.1f step(s) per 1000 lines over %d",
                                r.timebase_step_rate, r.timebase_lines);
                else
                    std::printf("no per-line sync to track");
                if (r.phasing_found)
                    std::printf("; %s, %.1f smp off straight, noise %.1f)\n",
                                phasing_witness_text(r),
                                r.phasing_nonlinearity, r.phasing_roughness);
                else
                    std::printf("; no phasing interval)\n");
                break;
            case nova::Timebase::kUnknown:
                // Say WHICH witness is missing. "No per-line sync" and
                // "the recording is too short to measure a rate over" are
                // different facts, and a 60 s cut of a pulse station hits
                // the second while looking like the first. Since session 10
                // a phasing interval can also be PRESENT and still unable to
                // answer — too noisy to resolve the threshold, or bent by a
                // single skip that is not a rate — and saying "no phasing
                // interval" there would be a lie about a run that was found.
                std::printf("  timebase not measurable (%s; %s)\n",
                            !r.per_line_sync
                                ? "no per-line sync to track"
                                : "too few drawn lines for a step rate",
                            phasing_witness_text(r));
                break;
        }
        if (r.segmented)
            std::printf("  image   %.2f-%.2f s  (dropped %d line(s) of "
                        "start/phasing, %d of stop)\n",
                        r.image_t_start, r.image_t_end, r.lines_dropped_head,
                        r.lines_dropped_tail);
        else
            std::printf("  image   whole recording (no control signals to "
                        "segment on)\n");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "nova-decode: %s\n", e.what());
        return 1;
    }
    return 0;
}
