// test_live_session.cpp — §9 screamer 10 [docs/05]: the live session
// state machine (live/session.hpp) turns the streaming stages into a
// session, and the session it makes does not depend on how the audio
// callback chunked the stream.
//
// Why M4 owes this test. Everything below this machine is pinned by its
// own screamer — the front end (live_demod_equiv), the tone detector
// (live_tones), the renderer (live_preview) — and none of those says
// whether the session SEQUENCES correctly: whether the operator is shown
// START TONE when the tone is found, whether the picture starts where the
// phasing interval ended, whether the stop tone ends the drawing rather
// than being drawn, and whether an operator Stop holds the image instead
// of discarding it [docs/05 §4, the SR-97 precedent]. A machine that got
// any of those wrong would drive every stage below it correctly and still
// be broken.
//
// Claims defended:
//
//   - **the §4 state sequence, on real audio** — tone-driven fixtures walk
//     IDLE -> READY -> START TONE -> PHASING -> DRAWING — PREVIEW and, at
//     end of stream, DECODING -> SAVED; the one fixture that carries a
//     real stop tone (nmc-image-stop-tone-120s, cut session 21) reaches
//     STOP TONE on air for the first time, and leaves it only after the
//     tone has actually ended;
//   - **the drawing point and the anchor are the batch path's** — the
//     machine comes out of PHASING where decode_fax's segmentation puts
//     the picture start, with the phasing interval's anchor in hand [WMO
//     §5.2.3.4; the handoff session 21 found missing], and the preview's
//     dead sector lands in the batch image's column;
//   - **the freeze [docs/05 §3]** — the snapshot begins at the tone's true
//     start (the pre-roll's whole purpose: the detector emits min_start_sec
//     into the tone) and ends at the stop tone's start, and the batch
//     decode of it is what reaches SAVED;
//   - **operator Stop is the stop-tone path minus the tone** — same
//     freeze, same decode request, no STOP TONE state, image held;
//   - **block-size independence of the whole session** — state sequence,
//     drawing point, rows, picture and snapshot identical from one-sample
//     blocks up. This is the claim the three component screamers cannot
//     make, because the machine's decisions (when the watcher acts, where
//     the preview starts, where the store is trimmed) are exactly the
//     places a chunking dependency would hide;
//   - **the give-up**: a start tone with no phasing behind it draws from
//     the tone's end, never enters PHASING, and still ends cleanly at the
//     stop tone (synthetic — no library recording has a start tone and no
//     phasing);
//   - **the opening cap [D-PERF-003]**: a start tone that NEVER ends is a
//     stuck carrier, not a transmission — the opening is abandoned at
//     max_opening_sec, the session returns to READY, and the retained
//     store is bounded by the pre-roll again (synthetic);
//   - **one recording, one transmission, take the first** — the second
//     opening of faxsignal-two-openings is ignored while drawing, and a
//     second TRANSMISSION after SAVED starts a new session cycle
//     (synthetic, two generated transmissions back to back);
//   - **operator Start is the other way out of SAVED** [docs/05 §4:
//     "next transmission, or operator action"] — the tone half is T10,
//     the operator half T13, and between sessions 23 and 25 neither the
//     shell (which had the button active and reading "Start") nor the
//     machine (which only listened in IDLE) covered it, so the click was
//     swallowed. Found by Sara at the keyboard, session 26.
//
// Explicitly NOT claimed: preview picture quality (that is live_preview's
// brief, per fixture), and the FAXSignal two-openings case is drawn from
// the FIRST opening — the batch rule "the last opening before the picture"
// needs the stop tone, which has not happened yet live. See the session
// header; the difference is registered there, not fixed here.
#include "../core/demod.hpp"
#include "../core/fax.hpp"
#include "../core/gen.hpp"
#include "../core/image.hpp"
#include "../core/wav.hpp"
#include "../live/session.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}

std::vector<float> load_video(const std::string& path) {
    nova::Wav w = nova::read_wav(path);
    if (w.sample_rate != 8000)
        throw std::runtime_error(path + ": fixture is not 8 kHz");
    return nova::fm_demod(w.samples, 8000);
}

// --- the driver -------------------------------------------------------------
//
// One whole session over one signal: capture starts, the blocks flow, the
// scripted operator actions fire at their stream times, the stream ends.
// The decode callback runs decode_fax INLINE when asked to — re-entrantly,
// from inside push(), which is the harder of the two caller shapes the
// class contract allows (the GUI defers to thread 3).

struct Action {
    double at_sec;
    bool force;  // force_start(ioc, lpm) or stop_capture()
    int ioc = 576;
    double lpm = 120.0;
    bool done = false;
    // ...or neither: an operator correction [docs/05 §7], which returns
    // nothing and only shows up later, in what the session hands the batch
    // decode. Set `correct` and one or both of the two below.
    bool correct = false;
    double phase = -1.0;
    double sync = std::numeric_limits<double>::quiet_NaN();
};

struct Run {
    std::vector<nova::SessionState> seq;
    std::vector<double> enter_t;  // stream time when each state was entered
    nova::Image preview_img;
    std::vector<nova::PreviewRow> rows;
    bool anchor_from_phasing = false;
    bool pulse = false;
    int locked_rows = 0;
    double period = 0.0;
    long long draw_start = -1;
    int ioc = 0;
    int decode_requests = 0;
    std::shared_ptr<const std::vector<float>> snapshot;
    long long snapshot_start = -1;
    nova::DecodeOptions decode_opt;
    bool saved = false;
    nova::DecodeResult decoded;
    bool decode_threw = false;
};

Run drive(const std::vector<float>& video, std::size_t block,
          std::vector<Action> actions, const nova::SessionOptions& sopt,
          bool run_batch) {
    Run r;
    nova::LiveSession s(8000, sopt);
    s.set_decode_callback(
        [&](std::shared_ptr<const std::vector<float>> snap, long long start,
            const nova::DecodeOptions& dopt) {
            r.decode_requests++;
            r.snapshot = snap;
            r.snapshot_start = start;
            r.decode_opt = dopt;
            if (!run_batch) return;
            try {
                r.decoded = nova::decode_fax(*snap, 8000, dopt);
                for (nova::SessionState st : s.batch_done(r.decoded).entered)
                    r.seq.push_back(st);
            } catch (const nova::DecodeError&) {
                r.decode_threw = true;
                for (nova::SessionState st :
                     s.batch_failed(nova::DecodeErrorKind::kNoSignal).entered)
                    r.seq.push_back(st);
            }
        });
    auto collect = [&](const nova::SessionOutput& out) {
        for (nova::SessionState st : out.entered) {
            r.seq.push_back(st);
            r.enter_t.push_back(s.consumed_sec());
        }
        r.rows.insert(r.rows.end(), out.rows.begin(), out.rows.end());
    };

    collect(s.start_capture());
    for (std::size_t i = 0; i < video.size(); i += block) {
        const double t = static_cast<double>(i) / 8000.0;
        for (Action& a : actions)
            if (!a.done && t >= a.at_sec) {
                a.done = true;
                if (a.correct) {
                    if (a.phase >= 0.0) s.set_phase(a.phase);
                    if (!std::isnan(a.sync)) s.set_sync(a.sync);
                } else {
                    collect(a.force ? s.force_start(a.ioc, a.lpm)
                                    : s.stop_capture());
                }
            }
        collect(s.push(video.data() + i,
                       std::min(block, video.size() - i)));
    }
    for (Action& a : actions)
        if (!a.done) {
            a.done = true;
            if (a.correct) {
                if (a.phase >= 0.0) s.set_phase(a.phase);
                if (!std::isnan(a.sync)) s.set_sync(a.sync);
            } else {
                collect(a.force ? s.force_start(a.ioc, a.lpm)
                                : s.stop_capture());
            }
        }
    collect(s.flush());

    if (s.preview()) {
        r.preview_img = s.preview()->image();
        r.anchor_from_phasing = s.preview()->anchor_from_phasing();
        r.pulse =
            s.preview()->dead_sector() == nova::DeadSector::kBlackPulse;
        r.locked_rows = s.preview()->locked_rows();
        r.period = s.preview()->period_samples();
    }
    if (std::find(r.seq.begin(), r.seq.end(),
                  nova::SessionState::kDrawingPreview) != r.seq.end())
        r.draw_start = s.draw_start_sample();
    r.ioc = s.ioc();
    r.saved = s.state() == nova::SessionState::kSaved;
    return r;
}

// --- sequence assertion ------------------------------------------------------

const char* kNames[] = {"IDLE",   "READY",    "START TONE", "PHASING",
                        "DRAWING", "STOP TONE", "DECODING",  "SAVED"};

void print_seq(const std::vector<nova::SessionState>& seq) {
    for (std::size_t i = 0; i < seq.size(); i++)
        std::printf("%s%s", i ? " -> " : "    ",
                    kNames[static_cast<int>(seq[i])]);
    std::printf("\n");
}

bool seq_is(const Run& r, std::initializer_list<nova::SessionState> want) {
    const std::vector<nova::SessionState> w(want);
    if (r.seq == w) return true;
    std::printf("    sequence differs:\n      got:  ");
    for (std::size_t i = 0; i < r.seq.size(); i++)
        std::printf("%s%s", i ? " -> " : "", kNames[static_cast<int>(r.seq[i])]);
    std::printf("\n      want: ");
    for (std::size_t i = 0; i < w.size(); i++)
        std::printf("%s%s", i ? " -> " : "", kNames[static_cast<int>(w[i])]);
    std::printf("\n");
    return false;
}

// --- the picture-domain scorers ---------------------------------------------
// The same dead-sector scorer live_preview uses, copied rather than shared:
// a test that borrows its subject's instruments stops being independent.

int ring_delta(int a, int b, int w) {
    int d = (a - b) % w;
    if (d > w / 2) d -= w;
    if (d < -w / 2) d += w;
    return d;
}

int dead_sector_column(const nova::Image& img, bool pulse) {
    const int w = img.width;
    if (img.height <= 0 || w <= 0) return -1;
    std::vector<double> dark(w, 0.0), white(w, 0.0);
    for (int y = 0; y < img.height; y++) {
        const uint8_t* row = &img.px[static_cast<std::size_t>(y) * w];
        for (int x = 0; x < w; x++) {
            if (row[x] < nova::kFaxDarkLevel * 255.0) dark[x] += 1.0;
            if (row[x] > nova::kFaxWhiteLevel * 255.0) white[x] += 1.0;
        }
    }
    for (int x = 0; x < w; x++) {
        dark[x] /= img.height;
        white[x] /= img.height;
    }
    auto win_mean = [&](const std::vector<double>& f, int at, int win) {
        double s = 0.0;
        for (int j = 0; j < win; j++) s += f[((at + j) % w + w) % w];
        return s / win;
    };
    const int pulse_w = std::max(2, static_cast<int>(nova::kFaxPulseFrac * w));
    const int dead_w = std::max(2, static_cast<int>(nova::kFaxDeadFrac * w));
    double best = -1e300;
    int at = 0;
    for (int x = 0; x < w; x++) {
        const double s =
            pulse ? std::min(win_mean(dark, x, pulse_w),
                             win_mean(white, x + pulse_w, pulse_w))
                  : win_mean(white, x, dead_w) -
                        win_mean(white, x - dead_w, dead_w);
        if (s > best) {
            best = s;
            at = x;
        }
    }
    return at;
}

using SS = nova::SessionState;

// --- block-size independence -------------------------------------------------

const std::size_t kBlocks[] = {1, 1000, 65536};

bool same_run(const Run& a, const Run& b) {
    if (a.seq != b.seq) { std::printf("      seq differs\n"); return false; }
    if (a.draw_start != b.draw_start) {
        std::printf("      draw_start %lld vs %lld\n", a.draw_start,
                    b.draw_start);
        return false;
    }
    if (a.rows.size() != b.rows.size()) {
        std::printf("      rows %zu vs %zu\n", a.rows.size(), b.rows.size());
        return false;
    }
    for (std::size_t i = 0; i < a.rows.size(); i++)
        if (a.rows[i].start_sample != b.rows[i].start_sample ||
            a.rows[i].period != b.rows[i].period ||
            a.rows[i].locked != b.rows[i].locked) {
            std::printf("      row %zu differs (%.3f/%.3f/%d vs "
                        "%.3f/%.3f/%d)\n", i, a.rows[i].start_sample,
                        a.rows[i].period, a.rows[i].locked,
                        b.rows[i].start_sample, b.rows[i].period,
                        b.rows[i].locked);
            return false;
        }
    if (a.preview_img.width != b.preview_img.width ||
        a.preview_img.height != b.preview_img.height ||
        a.preview_img.px != b.preview_img.px) {
        std::printf("      picture differs (%dx%d vs %dx%d)\n",
                    a.preview_img.width, a.preview_img.height,
                    b.preview_img.width, b.preview_img.height);
        return false;
    }
    if (a.decode_requests != b.decode_requests) return false;
    if (a.snapshot && b.snapshot) {
        if (a.snapshot_start != b.snapshot_start) {
            std::printf("      snapshot start %lld vs %lld\n",
                        a.snapshot_start, b.snapshot_start);
            return false;
        }
        if (*a.snapshot != *b.snapshot) {
            std::printf("      snapshot content differs (%zu vs %zu)\n",
                        a.snapshot->size(), b.snapshot->size());
            return false;
        }
    }
    return true;
}

void check_block_independent(const std::vector<float>& video,
                             std::vector<Action> actions,
                             const nova::SessionOptions& sopt) {
    const Run base = drive(video, 1000, actions, sopt, false);
    bool identical = true;
    for (std::size_t b : kBlocks) {
        const Run r = drive(video, b, actions, sopt, false);
        if (!same_run(base, r)) {
            std::printf("    block %zu DIFFERS\n", b);
            identical = false;
        }
    }
    check(identical,
          "sequence, drawing point, rows, picture and snapshot identical "
          "at every block size");
}

// --------------------------------------------------------------------------
// T1: a tone-driven white-only station, end to end — the anchor handoff.
// --------------------------------------------------------------------------
void test_tone_driven_whitespace(const std::string& path) {
    std::printf("\n== T1 tone-driven session, white-only with phasing (%s)\n",
                path.substr(path.find_last_of('/') + 1).c_str());
    const std::vector<float> video = load_video(path);
    const Run r = drive(video, 1000, {}, nova::SessionOptions(), true);
    print_seq(r.seq);

    check(seq_is(r, {SS::kReady, SS::kStartTone, SS::kPhasing,
                     SS::kDrawingPreview, SS::kDecoding, SS::kSaved}),
          "the §4 sequence, end to end, on real audio");
    check(r.ioc == 576 && r.decode_opt.ioc == 0,
          "IOC 576 from the 300 Hz tone, left AUTO for the batch decode");
    check(r.decode_requests == 1 && r.saved && !r.decode_threw,
          "one decode request, and it reached SAVED");

    // The reference: the batch decode of the whole recording.
    const nova::DecodeResult ref =
        nova::decode_fax(video, 8000, nova::DecodeOptions());

    // The snapshot starts at the tone's true beginning (1.88 s by the
    // batch detector), not at the detection event ~2 s later, and runs to
    // the end of the stream. The streaming t_start is frame-quantized
    // (0.125 s hop), so the comparison is to a hop, not to the sample.
    std::printf("    snapshot [%lld .. +%zu), tone t_start %.2f s\n",
                r.snapshot_start, r.snapshot->size(), 1.88);
    check(r.snapshot_start >= 1.88 * 8000.0 - 1010.0 &&
              r.snapshot_start <= 1.88 * 8000.0 + 8.0,
          "the snapshot begins at the tone's true start (the pre-roll)");
    check(std::fabs(static_cast<double>(r.snapshot_start) +
                    static_cast<double>(r.snapshot->size()) -
                    static_cast<double>(video.size())) <= 8.0,
          "and runs to the end of the stream");

    // The drawing point is where the batch path's segmentation puts the
    // picture start: the end of the phasing interval.
    std::printf("    drawing from %.2f s; batch picture starts %.2f s\n",
                r.draw_start / 8000.0, ref.image_t_start);
    check(std::fabs(r.draw_start / 8000.0 - ref.image_t_start) <= 1.0,
          "drawing starts where the batch path's segmentation starts");

    // The handoff: VMW is white-only, so the phasing interval is the only
    // place its line phase exists, and the preview must say it used it.
    check(r.anchor_from_phasing && !r.pulse && r.locked_rows == 0,
          "white-only: the phasing anchor is the anchor, and no lock is "
          "invented");
    const double seeded_lpm = 60.0 / (r.period / 8000.0);
    const double batch_lpm = 60.0 / ref.line_period_s;
    std::printf("    rate seeded from the phasing interval: %.4f lpm, batch "
                "fit %.4f lpm (%.0f ppm apart)\n",
                seeded_lpm, batch_lpm,
                (r.period / (8000.0 * ref.line_period_s) - 1.0) * 1e6);
    check(std::fabs(r.period - 8000.0 * ref.line_period_s) <= 1.0,
          "the phasing interval's rate seed agrees with the batch fit");

    const int prev_col = dead_sector_column(r.preview_img, false);
    const int ref_col = dead_sector_column(ref.img, false);
    const int d = ring_delta(prev_col, ref_col, ref.img.width);
    std::printf("    dead sector: preview col %d, batch col %d, delta %+d "
                "px\n", prev_col, ref_col, d);
    check(std::abs(d) <= 24,
          "the preview's dead sector lands in the batch image's column");

    check_block_independent(video, {}, nova::SessionOptions());
}

// --------------------------------------------------------------------------
// T2/T3: tone-driven pulse stations, on both phasing waveforms [WMO
// §5.2.3.1-.2]. The preview anchors on its own per-line template there —
// the phasing anchor is supplied and correctly NOT used, decode_fax's rule.
// --------------------------------------------------------------------------
void test_tone_driven_pulse(const std::string& path, const char* why) {
    std::printf("\n== tone-driven session, pulse station (%s)\n", why);
    const std::vector<float> video = load_video(path);
    const Run r = drive(video, 1000, {}, nova::SessionOptions(), true);
    print_seq(r.seq);

    check(seq_is(r, {SS::kReady, SS::kStartTone, SS::kPhasing,
                     SS::kDrawingPreview, SS::kDecoding, SS::kSaved}),
          "the §4 sequence");
    check(r.saved && !r.decode_threw, "reached SAVED");
    check(r.pulse && !r.anchor_from_phasing,
          "a pulse station keeps its tracked anchor");

    const nova::DecodeResult ref =
        nova::decode_fax(video, 8000, nova::DecodeOptions());
    std::printf("    drawing from %.2f s; batch picture starts %.2f s\n",
                r.draw_start / 8000.0, ref.image_t_start);
    check(std::fabs(r.draw_start / 8000.0 - ref.image_t_start) <= 1.0,
          "drawing starts where the batch path's segmentation starts");
    const int prev_col = dead_sector_column(r.preview_img, true);
    const int ref_col = dead_sector_column(ref.img, true);
    const int d = ring_delta(prev_col, ref_col, ref.img.width);
    std::printf("    dead sector: preview col %d, batch col %d, delta %+d "
                "px\n", prev_col, ref_col, d);
    check(std::abs(d) <= 12,
          "the preview's dead sector lands in the batch image's column");
}

// --------------------------------------------------------------------------
// T4: two openings, one picture. The live machine commits to the FIRST
// opening — the batch "last opening" rule needs the stop tone, which has
// not happened yet. Registered in the session header; pinned here so the
// day someone "fixes" it, the suite says what they traded away.
// --------------------------------------------------------------------------
void test_two_openings(const std::string& path) {
    std::printf("\n== T4 two openings before one picture (FAXSignal)\n");
    const std::vector<float> video = load_video(path);
    const Run r = drive(video, 1000, {}, nova::SessionOptions(), true);
    print_seq(r.seq);

    check(seq_is(r, {SS::kReady, SS::kStartTone, SS::kPhasing,
                     SS::kDrawingPreview, SS::kDecoding, SS::kSaved}),
          "one cycle only: the second opening does not restart the session");
    // The first opening's phasing runs 7-22 s; drawing starts there, not
    // at the second opening's 64.5 s, which is where the batch picture
    // begins.
    std::printf("    drawing from %.2f s (batch picture: 64.5 s)\n",
                r.draw_start / 8000.0);
    check(std::fabs(r.draw_start / 8000.0 - 22.0) <= 1.0,
          "drawing starts at the FIRST opening's phasing end (22 s)");
    check(r.saved, "and the batch decode still saves the right picture");
}

// --------------------------------------------------------------------------
// T5: forced start into a mid-image recording, ended by the library's only
// real stop tone. The STOP TONE state is entered when the tone is FOUND
// and left when it ENDS.
// --------------------------------------------------------------------------
void test_forced_stop_tone(const std::string& path) {
    std::printf("\n== T5 forced start, ended by a real stop tone (%s)\n",
                path.substr(path.find_last_of('/') + 1).c_str());
    const std::vector<float> video = load_video(path);
    std::vector<Action> acts = {{0.0, true, 576, 120.0, false}};
    const Run r = drive(video, 1000, acts, nova::SessionOptions(), true);
    print_seq(r.seq);

    check(seq_is(r, {SS::kReady, SS::kDrawingPreview, SS::kStopTone,
                     SS::kDecoding, SS::kSaved}),
          "forced start, stop tone, decode, save");
    check(r.decode_opt.ioc == 576,
          "the operator's IOC is the one the batch decode runs with");
    check(r.decode_requests == 1 && r.saved && !r.decode_threw,
          "one decode request, and it reached SAVED");

    // The tone was found at 111.38 s and ends at 116.50 s. The decode
    // request goes out at once; the STATE waits for the tone to end.
    double t_stop = -1, t_dec = -1;
    for (std::size_t i = 0; i < r.seq.size(); i++) {
        if (r.seq[i] == SS::kStopTone) t_stop = r.enter_t[i];
        if (r.seq[i] == SS::kDecoding) t_dec = r.enter_t[i];
    }
    std::printf("    stop tone found at %.2f s, DECODING at %.2f s (tone "
                "ends 116.50 s)\n", t_stop, t_dec);
    check(t_stop >= 111.0 && t_stop <= 114.0,
          "STOP TONE entered when the tone is found");
    check(t_dec >= 116.4,
          "DECODING not entered before the tone has actually ended");

    // The freeze: forced start cleared the store, so the snapshot is the
    // whole stream up to the tone.
    check(r.snapshot_start == 0, "forced start: the snapshot starts here");
    check(std::fabs(static_cast<double>(r.snapshot->size()) -
                    111.38 * 8000.0) <= 2400.0,
          "the snapshot ends at the stop tone, not after it");

    // The batch picture is [7.68..111.17]: its onset gate lands 7.68 s in
    // (the cut opens with a comb-less stretch of chart), so the saved
    // image has 207 lines to the preview's 226. The claim is that the two
    // are the same picture over that span, not that the batch is forced
    // to draw a head it has no comb for.
    int in_span = 0;
    for (const nova::PreviewRow& row : r.rows)
        if (row.start_sample >= r.decoded.image_t_start * 8000.0 - 1.0 &&
            row.start_sample <= r.decoded.image_t_end * 8000.0 + 1.0)
            in_span++;
    std::printf("    preview %dx%d, %zu rows (%d inside the batch's span); "
                "batch %d lines\n",
                r.preview_img.width, r.preview_img.height, r.rows.size(),
                in_span, r.decoded.lines);
    check(r.rows.size() >= 215 && r.rows.size() <= 226,
          "the stop tone is not drawn into the preview");
    check(std::abs(in_span - r.decoded.lines) <= 4,
          "the saved image is the same picture over the batch's span");

    nova::SessionOptions sopt;
    check_block_independent(video, {{0.0, true, 576, 120.0, false}}, sopt);
}

// --------------------------------------------------------------------------
// T6/T7: the operator's Stop. From DRAWING it is the stop-tone path minus
// the tone — freeze, decode, save, nothing discarded. From READY there is
// nothing to hold.
// --------------------------------------------------------------------------
void test_operator_stop(const std::string& path) {
    std::printf("\n== T6/T7 the operator's Stop (%s)\n",
                path.substr(path.find_last_of('/') + 1).c_str());
    const std::vector<float> video = load_video(path);

    const Run held = drive(video, 1000,
                           {{0.0, true, 576, 120.0, false},
                            {60.0, false, 0, 0.0, false}},
                           nova::SessionOptions(), true);
    print_seq(held.seq);
    check(seq_is(held, {SS::kReady, SS::kDrawingPreview, SS::kDecoding,
                        SS::kSaved}),
          "operator stop: DRAWING -> DECODING directly, no STOP TONE");
    check(held.decode_requests == 1 && held.saved,
          "the image is HELD and saved, not discarded");
    check(std::fabs(static_cast<double>(held.snapshot->size()) -
                    60.0 * 8000.0) <= 8.0,
          "the snapshot ends where the operator stopped it");
    check(!held.rows.empty() && held.preview_img.height > 0,
          "and the drawn rows stay drawn");

    const Run idle = drive(video, 1000, {{20.0, false, 0, 0.0, false}},
                           nova::SessionOptions(), true);
    print_seq(idle.seq);
    check(seq_is(idle, {SS::kReady, SS::kIdle}),
          "from READY, Stop returns to IDLE with nothing to save");
    check(idle.decode_requests == 0, "no decode, nothing to decode");
}

// --------------------------------------------------------------------------
// T8: a recording with no opening never starts anything by itself.
// --------------------------------------------------------------------------
void test_no_opening(const std::string& path) {
    std::printf("\n== T8 no opening, no session (%s)\n",
                path.substr(path.find_last_of('/') + 1).c_str());
    const Run r = drive(load_video(path), 1000, {}, nova::SessionOptions(),
                        true);
    check(seq_is(r, {SS::kReady}), "mid-image audio alone stays READY");
    check(r.decode_requests == 0, "and nothing is decoded");
}

// --------------------------------------------------------------------------
// T9: the give-up. A start tone with no phasing behind it (synthetic: no
// library recording is one) draws from the tone's end, never enters
// PHASING, and the stop tone still ends it cleanly.
// --------------------------------------------------------------------------
void test_no_phasing_giveup() {
    std::printf("\n== T9 start tone with no phasing (generated)\n");
    nova::GenOptions g;
    g.phasing = false;
    const nova::Image content = nova::gen_test_pattern(600, 200);
    const std::vector<float> audio = nova::gen_fax_signal(content, 200, g);
    const std::vector<float> video = nova::fm_demod(audio, 8000);
    std::printf("    %.1f s of signal: 5 s tone, 200 lines, stop\n",
                video.size() / 8000.0);

    nova::SessionOptions sopt;
    sopt.phasing_wait_sec = 20.0;  // shorten the wait; the rule is pinned,
                                   // not the spec's 70 s of wall time
    const Run r = drive(video, 1000, {}, sopt, true);
    print_seq(r.seq);
    check(seq_is(r, {SS::kReady, SS::kStartTone, SS::kDrawingPreview,
                     SS::kStopTone, SS::kDecoding, SS::kSaved}),
          "no PHASING state when there is no phasing, stop tone still ends "
          "it");
    check(std::fabs(r.draw_start / 8000.0 - 5.0) <= 1.0,
          "drawing starts at the tone's end");
    check(!r.anchor_from_phasing, "and there is no anchor to hand over");
    check(r.saved && !r.decode_threw, "and the batch decode still saves");
}

// --------------------------------------------------------------------------
// T10: one recording, two transmissions. SAVED leaves on the next
// transmission's start tone [docs/05 §4] — including the awkward case:
// the tone arrives while the previous decode is still running (the GUI is
// serialized, §8.4), so it is remembered and acted on at SAVED.
// --------------------------------------------------------------------------
void test_two_transmissions() {
    std::printf("\n== T10 two transmissions back to back (generated)\n");
    const nova::Image content = nova::gen_test_pattern(600, 120);
    const std::vector<float> one =
        nova::fm_demod(nova::gen_fax_signal(content, 120, nova::GenOptions()),
                       8000);
    std::vector<float> video = one;
    video.insert(video.end(), one.begin(), one.end());
    std::printf("    %.1f s of signal, two full openings\n",
                video.size() / 8000.0);

    // (a) the easy case: the first decode finishes before the next opening,
    // so the second start tone arrives while SAVED and drives SAVED ->
    // START TONE directly.
    {
        const Run r = drive(video, 1000, {}, nova::SessionOptions(), true);
        print_seq(r.seq);
        check(seq_is(r, {SS::kReady, SS::kStartTone, SS::kPhasing,
                         SS::kDrawingPreview, SS::kStopTone, SS::kDecoding,
                         SS::kSaved, SS::kStartTone, SS::kPhasing,
                         SS::kDrawingPreview, SS::kStopTone, SS::kDecoding,
                         SS::kSaved}),
              "SAVED leaves on the next transmission's start tone");
        check(r.decode_requests == 2 && r.saved,
              "two transmissions, two decodes, two saves");
    }

    // (b) the awkward case: the decode is DEFERRED, so the second start
    // tone qualifies mid-DECODING and must be remembered, not lost.
    {
    nova::LiveSession s(8000, nova::SessionOptions());
    std::vector<nova::SessionState> seq;
    int decode_requests = 0;
    std::shared_ptr<const std::vector<float>> snap1, snap2;
    s.set_decode_callback(
        [&](std::shared_ptr<const std::vector<float>> snap, long long,
            const nova::DecodeOptions&) {
            // Deliberately DEFERRED: the decode is not run here, so the
            // second transmission's start tone arrives mid-DECODING.
            if (decode_requests++ == 0)
                snap1 = snap;
            else
                snap2 = snap;
        });
    auto collect = [&](const nova::SessionOutput& out) {
        for (nova::SessionState st : out.entered) seq.push_back(st);
    };

    collect(s.start_capture());
    bool first_done = false, second_done = false;
    for (std::size_t i = 0; i < video.size(); i += 1000) {
        collect(s.push(video.data() + i,
                       std::min<std::size_t>(1000, video.size() - i)));
        const double t = s.consumed_sec();
        // The first decode reports back only once the second transmission's
        // start tone (at ~95+2 s) has already qualified.
        if (!first_done && snap1 && t > 99.0 &&
            s.state() == SS::kDecoding) {
            first_done = true;
            collect(s.batch_done(nova::decode_fax(*snap1, 8000,
                                                  nova::DecodeOptions())));
        }
        if (!second_done && snap2 &&
            s.state() == SS::kDecoding) {
            second_done = true;
            collect(s.batch_done(nova::decode_fax(*snap2, 8000,
                                                  nova::DecodeOptions())));
        }
    }
    collect(s.flush());
    print_seq(seq);

    check(seq == std::vector<nova::SessionState>(
                     {SS::kReady, SS::kStartTone, SS::kPhasing,
                      SS::kDrawingPreview, SS::kStopTone, SS::kDecoding,
                      SS::kSaved, SS::kStartTone, SS::kPhasing,
                      SS::kDrawingPreview, SS::kStopTone, SS::kDecoding,
                      SS::kSaved}),
          "SAVED leaves on the next transmission, even one that arrived "
          "mid-decode");
    check(decode_requests == 2 && s.state() == SS::kSaved,
          "two transmissions, two decodes, two saves");
    }
}

// --------------------------------------------------------------------------
// T11: the page cap is the guard for a MISSED stop tone [docs/05 §12
// item 3]: with an absurdly short cap, the transmission ends there, by the
// stop path minus the tone.
// --------------------------------------------------------------------------
void test_page_cap(const std::string& path) {
    std::printf("\n== T11 the page cap ends a tone-less overrun\n");
    const std::vector<float> video = load_video(path);
    nova::SessionOptions sopt;
    sopt.max_picture_sec = 30.0;
    const Run r = drive(video, 1000, {{0.0, true, 576, 120.0, false}}, sopt,
                        true);
    print_seq(r.seq);
    check(r.decode_requests == 1 &&
              std::fabs(static_cast<double>(r.snapshot->size()) -
                        30.0 * 8000.0) <= 8.0,
          "the cap ends the transmission at the cap");
    check(std::find(r.seq.begin(), r.seq.end(), SS::kStopTone) ==
              r.seq.end(),
          "without inventing a stop tone");
    check(std::find(r.seq.begin(), r.seq.end(), SS::kDecoding) !=
              r.seq.end(),
          "and the decode runs on what was drawn");
}

// --- T12: the operator's corrections reach the batch decode ---------------
// The link nothing else covers. `override_phase_seed` and
// `override_sync_fallback` pin what `decode_fax` DOES with the two fields,
// and `live_preview` pins what the renderer does with the live values, but
// between them sits `LiveSession::begin_decode` copying one into the other
// — three lines, both ends already pinned, which is exactly the shape of
// thing that ships broken. A session whose operator touched nothing must
// hand over the two DEFAULTS, not two zeroes: 0 ppm is a legal clock and
// column 0 a legal anchor, so "untouched" and "typed zero" have to arrive
// as different facts [docs/05 §7.1].
void test_operator_values_reach_the_decode(const std::string& path) {
    std::printf("\n== T12 PHASE and SYNC reach the batch decode [§7.1]\n");
    const std::vector<float> video = load_video(path);
    nova::SessionOptions sopt;

    const Run untouched =
        drive(video, 1000, {{0.0, true, 576, 120.0, false}}, sopt, false);
    check(untouched.decode_requests == 1 &&
              untouched.decode_opt.phase_anchor_hint < 0.0 &&
              std::isnan(untouched.decode_opt.clock_ppm_fallback),
          "an operator who touched nothing hands over NO hint and NO "
          "fallback, not two zeroes");

    Action phase{};
    phase.at_sec = 20.0;
    phase.correct = true;
    phase.phase = 0.375;
    Action sync{};
    sync.at_sec = 20.0;
    sync.correct = true;
    sync.sync = -1234.0;
    const Run corrected =
        drive(video, 1000, {{0.0, true, 576, 120.0, false}, phase, sync},
              sopt, false);
    check(corrected.decode_requests == 1 &&
              corrected.decode_opt.phase_anchor_hint == 0.375 &&
              corrected.decode_opt.clock_ppm_fallback == -1234.0,
          "a PHASE and a SYNC set while the preview was drawing arrive at "
          "the decode unchanged");

    // Each travels on its own. A single "the operator edited something"
    // flag would pass the check above and quietly hand over a phantom
    // second value here.
    const Run only_sync = drive(
        video, 1000, {{0.0, true, 576, 120.0, false}, sync}, sopt, false);
    check(only_sync.decode_opt.phase_anchor_hint < 0.0 &&
              only_sync.decode_opt.clock_ppm_fallback == -1234.0,
          "SYNC alone does not invent a PHASE hint");
    const Run only_phase = drive(
        video, 1000, {{0.0, true, 576, 120.0, false}, phase}, sopt, false);
    check(std::isnan(only_phase.decode_opt.clock_ppm_fallback) &&
              only_phase.decode_opt.phase_anchor_hint == 0.375,
          "PHASE alone does not invent a SYNC fallback");
}

// --------------------------------------------------------------------------
// T13: the operator's way out of SAVED [docs/05 §4: "next transmission, or
// operator action"]. T10 pins the tone half; this pins the click. The
// machine is driven to SAVED, Start is clicked BEFORE the next tone, and a
// full second cycle must still run from READY.
// --------------------------------------------------------------------------
void test_start_rearms_from_saved() {
    std::printf("\n== T13 operator Start in SAVED re-arms to READY [§4]\n");
    const nova::Image content = nova::gen_test_pattern(600, 120);
    const std::vector<float> one =
        nova::fm_demod(nova::gen_fax_signal(content, 120, nova::GenOptions()),
                       8000);
    std::vector<float> video = one;
    video.insert(video.end(), one.begin(), one.end());

    nova::LiveSession s(8000, nova::SessionOptions());
    std::vector<nova::SessionState> seq;
    int decode_requests = 0;
    s.set_decode_callback(
        [&](std::shared_ptr<const std::vector<float>> snap, long long,
            const nova::DecodeOptions&) {
            decode_requests++;
            for (nova::SessionState st :
                     s.batch_done(nova::decode_fax(*snap, 8000,
                                                   nova::DecodeOptions()))
                         .entered)
                seq.push_back(st);
        });
    auto collect = [&](const nova::SessionOutput& out) {
        for (nova::SessionState st : out.entered) seq.push_back(st);
    };

    collect(s.start_capture());
    bool clicked = false;
    for (std::size_t i = 0; i < video.size(); i += 1000) {
        collect(s.push(video.data() + i,
                       std::min<std::size_t>(1000, video.size() - i)));
        if (!clicked && s.state() == SS::kSaved) {
            clicked = true;
            collect(s.start_capture());  // the operator's way out of SAVED
        }
    }
    collect(s.flush());
    print_seq(seq);

    check(clicked, "the first transmission reached SAVED before the click");
    check(seq == std::vector<nova::SessionState>(
                     {SS::kReady, SS::kStartTone, SS::kPhasing,
                      SS::kDrawingPreview, SS::kStopTone, SS::kDecoding,
                      SS::kSaved, SS::kReady, SS::kStartTone, SS::kPhasing,
                      SS::kDrawingPreview, SS::kStopTone, SS::kDecoding,
                      SS::kSaved}),
          "SAVED -> READY on Start, then a full second cycle");
    check(decode_requests == 2 && s.state() == SS::kSaved,
          "two transmissions, two decodes, two saves");
}

}  // namespace

// --------------------------------------------------------------------------
// T14: the opening cap [D-PERF-003, decision by Sara 2026-08-19]. A start
// tone that NEVER ends is a stuck carrier, not a transmission: the session
// abandons the opening at max_opening_sec and goes back to listening, with
// the retained store bounded again. Every other bound is measured from the
// tone's end or from drawing, and neither exists here.
// --------------------------------------------------------------------------
void test_opening_cap() {
    std::printf("\n== T14 a start tone that never ends is abandoned\n");
    nova::GenOptions g;
    g.start_sec = 120.0;   // the tone outlasts the cap several times over
    g.phasing = false;     // and nothing ever follows it
    g.stop_tone = false;
    const nova::Image content = nova::gen_test_pattern(600, 30);
    const std::vector<float> video =
        nova::fm_demod(nova::gen_fax_signal(content, 30, g), 8000);
    std::printf("    %.1f s of signal, all of it start tone\n",
                video.size() / 8000.0);

    nova::SessionOptions sopt;
    sopt.max_opening_sec = 30.0;  // shorten the cap; the rule is pinned,
                                  // not the 300 s of wall time
    nova::LiveSession s(8000, sopt);
    std::vector<nova::SessionState> seq;
    auto collect = [&](const nova::SessionOutput& out) {
        for (nova::SessionState st : out.entered) seq.push_back(st);
    };
    collect(s.start_capture());
    bool abandoned = false;
    for (std::size_t i = 0; i < video.size(); i += 1000) {
        collect(s.push(video.data() + i,
                       std::min<std::size_t>(1000, video.size() - i)));
        if (!abandoned && s.state() == SS::kReady &&
            std::find(seq.begin(), seq.end(), SS::kStartTone) !=
                seq.end()) {
            abandoned = true;
            check(s.consumed_sec() <= 30.0 + 6.0,
                  "abandoned soon after the cap");
            check(s.retained_samples() <= 11 * 8000,
                  "the retained store is bounded again (pre-roll, not the "
                  "whole tone)");
        }
    }
    collect(s.flush());
    print_seq(seq);
    check(abandoned, "the opening is abandoned back to READY");
    check(seq == std::vector<nova::SessionState>(
                     {SS::kReady, SS::kStartTone, SS::kReady}),
          "READY -> START TONE -> READY, nothing else");
    check(s.state() == SS::kReady,
          "and the session is still listening for the next one");
}

int main(int argc, char** argv) {
    if (argc < 6) {
        std::fprintf(stderr,
                     "usage: %s VMW-PHASING-IMAGE JMH-CHART XSG-PHASING-HEAD "
                     "FAXSIGNAL-TWO-OPENINGS NMC-STOP [NO-OPENING]\n",
                     argv[0]);
        return 2;
    }
    test_tone_driven_whitespace(argv[1]);
    test_tone_driven_pulse(argv[2], "JMH test chart, 5/95 phasing");
    test_tone_driven_pulse(argv[3], "XSG FYCI, 50/50 phasing");
    test_two_openings(argv[4]);
    test_forced_stop_tone(argv[5]);
    test_operator_stop(argv[5]);
    test_no_opening(argv[5]);
    test_no_phasing_giveup();
    test_two_transmissions();
    test_start_rearms_from_saved();
    test_page_cap(argv[5]);
    test_operator_values_reach_the_decode(argv[5]);
    test_opening_cap();

    std::printf("\n%s (%d failure(s))\n", failures ? "FAILED" : "OK",
                failures);
    return failures ? 1 : 0;
}
