// nova-decode — decode a WAV recording of a WEFAX signal to a PGM image.
#include "../core/demod.hpp"
#include "../core/fax.hpp"
#include "../core/image.hpp"
#include "../core/resample.hpp"
#include "../core/wav.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
constexpr int kInternalRate = 8000;

void usage() {
    std::fprintf(stderr,
                 "usage: nova-decode in.wav out.pgm [--lpm 60|90|120] "
                 "[--ioc 288|576] [--start SEC] [--no-autolock]\n");
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        usage();
        return 2;
    }
    nova::DecodeOptions opt;
    for (int i = 3; i < argc; i++) {
        if (!std::strcmp(argv[i], "--lpm") && i + 1 < argc)
            opt.lpm = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--ioc") && i + 1 < argc)
            opt.ioc = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--start") && i + 1 < argc)
            opt.start_sec = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--no-autolock"))
            opt.autolock = false;
        else if (!std::strcmp(argv[i], "--no-phasing"))
            opt.use_phasing = false;
        else if (!std::strcmp(argv[i], "--no-segment"))
            opt.segment = false;
        else {
            usage();
            return 2;
        }
    }

    try {
        nova::Wav w = nova::read_wav(argv[1]);
        std::vector<float> mono =
            nova::resample(w.samples, w.sample_rate, kInternalRate);
        std::vector<float> video =
            nova::fm_demod(mono, kInternalRate, 1900.0, 400.0);
        nova::DecodeResult r = nova::decode_fax(video, kInternalRate, opt);
        nova::write_pgm(argv[2], r.img);
        std::printf(
            "lpm=%d (measured %.3f)  clock=%+.1f ppm  lines=%d  "
            "locked=%d  clamped=%d  max_step=%.2f px  dead=%s(%.2f)%s\n",
            r.lpm, 60.0 / r.line_period_s, r.clock_ppm, r.lines,
            r.locked_lines, r.clamped_corrections, r.max_step_px,
            r.dead_sector == nova::DeadSector::kBlackPulse ? "pulse"
                                                           : "white",
            r.dead_consistency, r.per_line_sync ? "" : " no-per-line-sync");
        if (r.phasing_found)
            std::printf("  phasing %.2f-%.2f s  anchor delta %+.1f smp vs "
                        "image  (%s)\n",
                        r.phasing_t_start, r.phasing_t_end,
                        r.phasing_anchor_delta,
                        r.anchor_from_phasing ? "PHASING anchor used"
                                              : "image anchor used");
        else
            std::printf("  phasing none\n");
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
