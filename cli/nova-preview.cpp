// nova-preview — render the LIVE preview of a recording: the provisional,
// forward-only picture the pane will show while the chart is still arriving
// [docs/05 §4, §6]. This drives the real session state machine, so the
// opening handling (start tone, phasing watcher, anchor handoff) is the
// one the GUI gets. For a recording that starts mid-transmission — no
// start tone for the machine to hear — use --force, the operator's own
// answer [docs/04 Finding 2].
//
// The batch decode of the frozen snapshot runs at the end too, so the two
// pictures can be compared side by side (out.pgm is the PREVIEW; the saved
// image is nova-decode's job).
#include "../core/demod.hpp"
#include "../core/fax.hpp"
#include "../core/image.hpp"
#include "../core/resample.hpp"
#include "../core/wav.hpp"
#include "../live/session.hpp"
#include "../core/version_flag.hpp"
#include "env_hooks.hpp"
#include "internal_rate.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

struct Args {
    double deviation = 400.0;
    bool force = false, have_phase = false, have_sync = false;
    int force_ioc = 576, force_lpm = 120;
    double phase = 0.0, sync = 0.0;
};

void usage() {
    std::fprintf(stderr,
                 "usage: nova-preview in.wav out.pgm [--dev 150|400] "
                 "[--force IOC LPM] [--phase FRAC] [--sync PPM]\n"
                 "  --force  forced start with operator IOC and rate, for "
                 "recordings with no opening\n"
                 "  --phase/--sync  operator overrides, applied when drawing "
                 "begins [docs/05 §7]\n"
                 "       nova-preview --version\n");
}

// Fill args from the command line; false means bad arguments (usage
// already printed) and main exits 2.
bool parse_args(int argc, char** argv, Args& args) {
    if (argc < 3) {
        usage();
        return false;
    }
    for (int i = 3; i < argc; i++) {
        if (!std::strcmp(argv[i], "--dev") && i + 1 < argc)
            args.deviation = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--force") && i + 2 < argc) {
            args.force_ioc = std::atoi(argv[++i]);
            args.force_lpm = std::atoi(argv[++i]);
            args.force = true;
        } else if (!std::strcmp(argv[i], "--phase") && i + 1 < argc) {
            args.phase = std::atof(argv[++i]);
            args.have_phase = true;
        } else if (!std::strcmp(argv[i], "--sync") && i + 1 < argc) {
            args.sync = std::atof(argv[++i]);
            args.have_sync = true;
        } else {
            usage();
            return false;
        }
    }
    if (args.deviation != 150.0 && args.deviation != 400.0) {
        usage();
        return false;
    }
    return true;
}

// The batch decode of the frozen snapshot, so the two pictures can be
// compared side by side.
void report_saved(nova::LiveSession& s,
                  const std::shared_ptr<const std::vector<float>>& snapshot,
                  const nova::DecodeOptions& dopt) {
    try {
        nova::DecodeResult r =
            nova::decode_fax(*snapshot, nova::kInternalRate, dopt);
        s.batch_done(r);
        std::printf("saved  image %dx%d, lpm=%d (measured %.3f)  "
                    "ioc=%d  clock=%+.1f ppm  state %s\n",
                    r.img.width, r.img.height, r.lpm,
                    60.0 / r.line_period_s, r.ioc, r.clock_ppm,
                    nova::session_state_name(s.state()));
    } catch (const nova::DecodeError& e) {
        s.batch_failed(e.kind());
        std::printf("saved  decode failed: %s (state %s)\n",
                    e.what(), nova::session_state_name(s.state()));
    }
}
}  // namespace

int main(int argc, char** argv) {
    // Ahead of the argument-count check: --version is a question
    // about the program, not about a decode [E-GAP-001].
    if (nova::handled_version_flag(argc, argv, "nova-preview")) return 0;
    Args args;
    if (!parse_args(argc, argv, args)) return 2;

    try {
        nova::Wav w = nova::read_wav(argv[1]);
        std::vector<float> mono =
            nova::resample(w.samples, w.sample_rate, nova::kInternalRate);
        // 1900 Hz: the WEFAX audio subcarrier centre frequency [WMO §5.5.1].
        std::vector<float> video =
            nova::fm_demod(mono, nova::kInternalRate, 1900.0,
                           args.deviation);

        nova::SessionOptions sopt;
        sopt.hooks = nova::hooks_from_env();
        nova::LiveSession s(nova::kInternalRate, sopt);
        std::shared_ptr<const std::vector<float>> snapshot;
        nova::DecodeOptions dopt;
        s.set_decode_callback(
            [&](std::shared_ptr<const std::vector<float>> snap, long long,
                const nova::DecodeOptions& d) {
                snapshot = std::move(snap);
                dopt = d;
            });

        s.start_capture();
        if (args.force) s.force_start(args.force_ioc, args.force_lpm);
        std::size_t rows = 0;
        bool overrides_done = false;
        for (std::size_t i = 0; i < video.size(); i += 4096) {
            nova::SessionOutput out =
                s.push(video.data() + i,
                       std::min<std::size_t>(4096, video.size() - i));
            rows += out.rows.size();
            if (!overrides_done && s.preview()) {
                overrides_done = true;
                if (args.have_phase) s.set_phase(args.phase);
                if (args.have_sync) s.set_sync(args.sync);
            }
        }
        rows += s.flush().rows.size();

        const nova::StreamPreview* p = s.preview();
        if (!p || p->rows() == 0) {
            std::fprintf(stderr,
                         "nova-preview: no picture was drawn (state %s). "
                         "No opening was found — for a recording that "
                         "starts mid-transmission, use --force IOC LPM.\n",
                         nova::session_state_name(s.state()));
            return 1;
        }
        nova::write_pgm(argv[2], p->image());
        std::printf("preview %dx%d, %zu rows (%d locked, %d re-acquired), "
                    "period %.2f smp, seed %s, anchor %s, %s\n",
                    p->image().width, p->image().height, rows,
                    p->locked_rows(), p->reacquired_rows(),
                    p->period_samples(),
                    p->seed() == nova::PreviewSeed::kOperator ? "operator"
                    : p->seed() == nova::PreviewSeed::kSignal ? "signal"
                                                              : "nominal",
                    p->anchor_from_phasing() ? "from the phasing interval"
                    : p->dead_sector() == nova::DeadSector::kBlackPulse
                        ? "from the sync pulse"
                        : "guessed (unanchored)",
                    nova::session_state_name(s.state()));

        if (snapshot) report_saved(s, snapshot, dopt);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "nova-preview: %s\n", e.what());
        return 1;
    }
    return 0;
}
