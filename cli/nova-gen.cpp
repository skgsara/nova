// nova-gen — test-harness WEFAX signal generator (writes a WAV).
// Test tool, not a product feature (ROADMAP M0).
#include "../core/gen.hpp"
#include "../core/wav.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
void usage() {
    std::fprintf(stderr,
                 "usage: nova-gen out.wav [--lpm 60|90|120] [--ioc 288|576]\n"
                 "       [--lines N] [--ppm X] [--noise X] [--dev 150|400]\n"
                 "       [--fs N] [--no-start] [--no-stop] [--no-phasing]\n"
                 "       [--no-pulse]\n");
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        usage();
        return 2;
    }
    nova::GenOptions opt;
    int lines = 200;
    for (int i = 2; i < argc; i++) {
        if (!std::strcmp(argv[i], "--lpm") && i + 1 < argc)
            opt.lpm = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--ioc") && i + 1 < argc)
            opt.ioc = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--lines") && i + 1 < argc)
            lines = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--ppm") && i + 1 < argc)
            opt.ppm = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--noise") && i + 1 < argc)
            opt.noise = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--dev") && i + 1 < argc)
            opt.deviation = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--fs") && i + 1 < argc)
            opt.fs = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--no-start"))
            opt.start_tone = false;
        else if (!std::strcmp(argv[i], "--no-stop"))
            opt.stop_tone = false;
        else if (!std::strcmp(argv[i], "--no-phasing"))
            opt.phasing = false;
        else if (!std::strcmp(argv[i], "--no-pulse"))
            opt.dead_pulse = false;  // white-only dead sector
        else {
            usage();
            return 2;
        }
    }
    try {
        const int width = (opt.ioc == 288) ? 905 : 1810;
        nova::Image content = nova::gen_test_pattern(width, lines);
        std::vector<float> sig = nova::gen_fax_signal(content, lines, opt);
        nova::write_wav(argv[1], opt.fs, sig);
        std::printf("wrote %s: %d lpm, IOC %d, %d image lines, %+.0f ppm, "
                    "noise %.3f, %.1f s\n",
                    argv[1], opt.lpm, opt.ioc, lines, opt.ppm, opt.noise,
                    sig.size() / static_cast<double>(opt.fs));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "nova-gen: %s\n", e.what());
        return 1;
    }
    return 0;
}
