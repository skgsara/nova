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
#include "env_hooks.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
constexpr int kInternalRate = 8000;

void usage() {
    std::fprintf(stderr,
                 "usage: nova-preview in.wav out.pgm [--dev 150|400] "
                 "[--force IOC LPM] [--phase FRAC] [--sync PPM]\n"
                 "  --force  forced start with operator IOC and rate, for "
                 "recordings with no opening\n"
                 "  --phase/--sync  operator overrides, applied when drawing "
                 "begins [docs/05 §7]\n");
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        usage();
        return 2;
    }
    double deviation = 400.0;
    bool force = false, have_phase = false, have_sync = false;
    int force_ioc = 576, force_lpm = 120;
    double phase = 0.0, sync = 0.0;
    for (int i = 3; i < argc; i++) {
        if (!std::strcmp(argv[i], "--dev") && i + 1 < argc)
            deviation = std::atof(argv[++i]);
        else if (!std::strcmp(argv[i], "--force") && i + 2 < argc) {
            force_ioc = std::atoi(argv[++i]);
            force_lpm = std::atoi(argv[++i]);
            force = true;
        } else if (!std::strcmp(argv[i], "--phase") && i + 1 < argc) {
            phase = std::atof(argv[++i]);
            have_phase = true;
        } else if (!std::strcmp(argv[i], "--sync") && i + 1 < argc) {
            sync = std::atof(argv[++i]);
            have_sync = true;
        } else {
            usage();
            return 2;
        }
    }
    if (deviation != 150.0 && deviation != 400.0) {
        usage();
        return 2;
    }

    try {
        nova::Wav w = nova::read_wav(argv[1]);
        std::vector<float> mono =
            nova::resample(w.samples, w.sample_rate, kInternalRate);
        std::vector<float> video =
            nova::fm_demod(mono, kInternalRate, 1900.0, deviation);

        nova::SessionOptions sopt;
        sopt.hooks = nova::hooks_from_env();
        nova::LiveSession s(kInternalRate, sopt);
        std::shared_ptr<const std::vector<float>> snapshot;
        nova::DecodeOptions dopt;
        s.set_decode_callback(
            [&](std::shared_ptr<const std::vector<float>> snap, long long,
                const nova::DecodeOptions& d) {
                snapshot = std::move(snap);
                dopt = d;
            });

        s.start_capture();
        if (force) s.force_start(force_ioc, force_lpm);
        std::size_t rows = 0;
        bool overrides_done = false;
        for (std::size_t i = 0; i < video.size(); i += 4096) {
            nova::SessionOutput out =
                s.push(video.data() + i,
                       std::min<std::size_t>(4096, video.size() - i));
            rows += out.rows.size();
            if (!overrides_done && s.preview()) {
                overrides_done = true;
                if (have_phase) s.set_phase(phase);
                if (have_sync) s.set_sync(sync);
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

        if (snapshot) {
            try {
                nova::DecodeResult r =
                    nova::decode_fax(*snapshot, kInternalRate, dopt);
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
    } catch (const std::exception& e) {
        std::fprintf(stderr, "nova-preview: %s\n", e.what());
        return 1;
    }
    return 0;
}
