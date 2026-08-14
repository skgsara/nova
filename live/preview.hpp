// preview.hpp — the provisional renderer: rows drawn as the signal
// arrives, forward-only, single pass, never revised [docs/05 §6,
// docs/04 answer 5].
//
// Why this is not just `decode_fax` fed in pieces. The batch decoder is
// built out of estimators that look at the whole recording and then place
// every row: a period fitted over a long baseline, a change-point timebase
// fit, dropout repair that reads the far side of a run, intra-line breaks
// placed against the row below. Every one of those would move a row that
// is already on the operator's screen. **The preview cannot use the batch
// period estimator at all** — that is the constraint §6 is written around,
// and it is the reason this class exists rather than a `decode_fax`
// entry point that takes a callback.
//
// What it does instead:
//
//   - **rate** from a short EMA over the last N locked lines, seeded in
//     the order §6 gives: operator forced-start values (IOC + rate) or a
//     live SYNC trim; else IOC from the start tone and rate from the
//     phasing interval; else nominal 120 lpm / IOC 576. The seed is a
//     `PreviewOptions`, so the seeding *policy* lives in the caller (the
//     live state machine) and the renderer only ever holds one working
//     period;
//   - **phase** from the same per-line dead-sector relock the batch path
//     uses — `fax_best_sync` in `core/fax.hpp`, the same template against
//     the same threshold. It works forward, so it is kept.
//
// What is NOT available live and is not faked here: bracketed dropout
// repair (needs the far side of the run, session 12), intra-line break
// placement (needs the row below, session 11b), and change-point timebase
// fitting (session 9). Rows the batch path would repair are drawn wrong
// once, and repaired in the saved image. That visible difference is the
// announced swap, and it is why the pane says "provisional" from the
// first row [docs/05 §4].
//
// ---------------------------------------------------------------------
// The acquisition window — a design decision §6 did not make
// ---------------------------------------------------------------------
// §6 says where the RATE is seeded from and is silent on where the PHASE
// comes from before any row has been drawn. The batch path answers that
// with across-line consistency over 120 lines [core/fax.cpp
// `stage_dead_sector`]: the dead sector is the one part of the line that
// looks the same on EVERY line, and session 4 measured that a single-line
// search lands on picture content instead often enough to be useless.
// A forward renderer has two ways out, and only one of them is allowed:
//
//   (a) draw from an unverified anchor and correct it later — forbidden,
//       that is revision;
//   (b) wait a few lines before the first row appears — permitted, that
//       is latency, and no row is ever redrawn.
//
// So this class holds `acq_lines` lines before it emits row 0, builds the
// same dark/white consistency profiles over exactly those lines, decides
// the dead-sector style against the same `kFaxPulseConsistency` cut, and
// commits an anchor it then never revisits. Nothing is lost to the wait:
// those lines are the ones drawn first.
//
// The window is SHORT (16 lines, ~8 s at 120 lpm) and the two pressures
// on it point opposite ways, which is what fixes it:
//   - longer is a cleaner profile, because consistency is a fraction over
//     lines and 16 of them quantize it to 1/16;
//   - shorter is a sharper one, because the profile is stacked on the
//     SEEDED period, and a relative rate error e smears the pulse by
//     `acq_lines * period * e`. At 16 lines and a seed 300 ppm off, that
//     is 19 samples against a 90-sample pulse — inside a quarter of it.
//     At the 120 lines the batch path can afford (it has a refined period
//     by then) the same error would smear 144 samples, wider than the
//     pulse itself.
// A preview that waited longer would be measurably worse, not just later.
//
// ---------------------------------------------------------------------
// Block-size independence, which is the claim `live_preview` pins
// ---------------------------------------------------------------------
// The picture must not depend on how the audio callback happened to chunk
// the stream. That is structural here, not tested-in:
//
//   - every position is an ABSOLUTE sample index into the pushed stream,
//     never an offset into the current block;
//   - a row is drawn only once every sample it will read is present, so
//     no row is ever built from a short read and patched up;
//   - the buffer is released from the front only behind what a future row
//     can still reach, computed from the next row's start and nothing
//     else — so the renderer structurally CANNOT look back, which is the
//     forward-only rule enforced by the memory layout rather than by
//     discipline.
//
// Threading: a thread-2 object [docs/05 §2]. It allocates; never call it
// from the RtAudio callback.
#pragma once

#include "../core/fax.hpp"
#include "../core/hooks.hpp"
#include "../core/image.hpp"

#include <cstddef>
#include <vector>

namespace nova {

// Where the working geometry came from, in §6's order of preference.
// Reported so the status panel can say what it is drawing on without
// showing a number the preview has no right to display [docs/05 §4:
// clock and timebase readouts stay blank until the batch decode].
enum class PreviewSeed {
    kOperator,  // forced start: IOC and rate supplied by the operator
    kSignal,    // IOC from the start tone, rate from the phasing interval
    kNominal,   // 120 lpm / IOC 576
};

struct PreviewOptions {
    int ioc = 576;        // 576 or 288; sets the drawn width, as batch does
    double lpm = 120.0;   // fractional: the phasing interval can measure it
    // Operator SYNC trim on the seeded rate, ppm [docs/05 §7]. See
    // `set_clock_ppm` for the one thing about it that will surprise.
    double clock_ppm = 0.0;
    PreviewSeed seed = PreviewSeed::kNominal;

    // Line-start phase, as a fraction of a line, when the caller already
    // knows it — the live state machine has just watched the phasing
    // interval, whose LEADING EDGE OF WHITE is dead-sector entry
    // [WMO §5.2.3.4; core/phasing.hpp `PhasingResult::anchor`].
    //
    // §6's seed list names IOC and rate and is silent on phase, which left
    // the renderer finding its own anchor from image lines alone. That is
    // the best available on a station that sends a black pulse, and the
    // only wrong answer on one that does not: a white-only dead sector
    // carries no per-line phase at all [core/fax.hpp `fax_best_sync`], so
    // the phasing interval is the single place its anchor exists, and the
    // batch path takes it from exactly there. Measured before this
    // existed, on `vmw-phasing-image-160s`: the preview drew the page
    // 524 px — nearly a third of a line — around from where the saved
    // image put it, so the operator would have watched a chart arrive
    // sideways and then jump when the decode replaced it.
    //
    // Used ONLY where the image lines cannot answer, which is the same
    // rule `decode_fax` follows (`DecodeResult::anchor_from_phasing`): on
    // a pulse station the per-line template is the better witness and
    // wins. Negative — the default — means no phasing was seen, which is
    // also what a forced start gets.
    double phase_anchor = -1.0;

    // Per-line dead-sector relock. Off draws on the seeded clock alone,
    // which is also what a white-only station gets whatever this says.
    bool autolock = true;
    // Per-line sync search window, fraction of a line. Same meaning and
    // same default as `DecodeOptions::search_frac`.
    double search_frac = 0.03;
    // Lines held before row 0 is emitted; see the header note above.
    int acq_lines = 16;
    // N in "a short EMA over the last N locked lines" [§6].
    int ema_lines = 8;
    // Largest relative jump in the measured period the EMA will believe in
    // one step. A single bad template match measures a wild period; the
    // batch path rejects those with a median over neighbours, which needs
    // the neighbours. Forward, a plain gate is what is left.
    double ema_max_jump = 0.03;
    // Page cap [docs/05 §4]. 0 = unbounded.
    int max_lines = 0;
    DecodeHooks hooks;
};

// One drawn row. Emitted once, when the row is drawn, and never amended.
struct PreviewRow {
    int index = 0;
    double start_sample = 0.0;  // absolute, in the pushed stream
    double period = 0.0;        // working period this row was drawn at
    bool locked = false;        // the sync template really matched
    double sync_score = 0.0;
    // This is the row where an operator override took effect — the
    // affordance §7 asks for ("touch once and wait several lines before
    // judging" earns a mark the operator can actually see).
    bool phase_mark = false;
    bool sync_mark = false;
};

class StreamPreview {
public:
    explicit StreamPreview(int fs, const PreviewOptions& opt = PreviewOptions());

    // Feed demodulated VIDEO — the same domain `decode_fax` reads — from
    // the point where drawing begins. Segmentation is the caller's job:
    // the live state machine enters `DRAWING — PREVIEW` on the end of the
    // phasing interval or on a forced start [docs/05 §4], and starts
    // pushing there. Returns the rows completed during this call.
    std::vector<PreviewRow> push(const float* video, std::size_t n);
    std::vector<PreviewRow> push(const std::vector<float>& video) {
        return push(video.data(), video.size());
    }

    // End of transmission — the stop tone, the page cap, or the operator's
    // Stop [docs/05 §4]. Draws what is left: the rows whose PIXELS are all
    // present but whose sync template no longer has margin past them, and,
    // if the transmission was too short for a full acquisition window, an
    // anchor taken over however many whole lines did arrive.
    //
    // Without this a short transmission renders as nothing at all rather
    // than as a short picture, which is the wrong failure: `decode_fax`
    // draws the 16-line stub in `vmw-start-phasing-100s`, and a preview
    // that showed a blank pane for it would look broken rather than brief.
    std::vector<PreviewRow> flush();

    // The picture so far. Grows by whole rows; drawn rows are never
    // touched again, which is the whole contract of this class.
    const Image& image() const { return img_; }
    int width() const { return width_; }
    int rows() const { return img_.height; }

    // --- the live override surface [docs/05 §7] ---------------------------
    // PHASE: the operator reports WHERE THE DEAD SECTOR IS, as a fraction
    // of the drawn line width — never a delta. Applies forward from the
    // next row; drawn rows never move. The correction is always taken
    // FORWARD in the signal (there may be no samples left behind to take
    // it backward through), so a report of 0.9 costs most of one row
    // rather than winding back a tenth of one.
    void set_phase_anchor(double frac);

    // SYNC: a ppm trim on the line rate, applied forward from the next row.
    //
    // **It is a seed, not a winner, and that is deliberate.** Where the
    // station sends a black pulse, the per-line relock keeps measuring the
    // real period and the EMA walks the trim off within `ema_lines` rows.
    // Where it does not — a white-only station, which is the case that has
    // no per-line measurement at all — nothing ever contradicts the
    // operator and the value stands for the whole page. That is the same
    // asymmetry the batch re-decode was given in session 17
    // (`clock_ppm_fallback` is a fallback, `phase_anchor_hint` is a seed
    // the search refines) [docs/05 §7.1], one stage earlier. Implementing
    // this as a plain override that measurement may not touch would make
    // the live path and the batch path disagree about the same operator
    // action, which is exactly the bug §7.1 exists to prevent.
    void set_clock_ppm(double ppm);

    // --- what the status panel may ask ------------------------------------
    bool acquired() const { return acquired_; }
    DeadSector dead_sector() const { return dead_; }
    // True when the anchor came from the phasing interval the caller
    // supplied rather than from the image lines — the same distinction
    // `DecodeResult::anchor_from_phasing` reports for the saved image.
    bool anchor_from_phasing() const { return anchor_from_phasing_; }
    double dead_consistency() const { return dead_cons_; }
    // The working period, in samples. An internal number: §4 keeps the
    // clock and timebase readouts blank until the batch decode produces
    // them, precisely so a short baseline's +261 ppm is never displayed.
    double period_samples() const { return period_; }
    int locked_rows() const { return locked_rows_; }
    // Rows the ±narrow window had lost and a whole-line sweep found again.
    // Nonzero means the picture survived something — a dropout, a stream
    // skip — that would otherwise have torn it from there to the end.
    int reacquired_rows() const { return reacquired_rows_; }
    PreviewSeed seed() const { return opt_.seed; }
    double consumed_sec() const {
        return static_cast<double>(total_in_) / fs_;
    }
    // First sample still retained. Everything before it has been released
    // and can never be read again — the forward-only rule, in memory.
    long long retained_from() const { return buf_start_; }

private:
    bool try_acquire(int lines);
    bool can_draw_row() const;
    void draw_row(std::vector<PreviewRow>& out, bool final_row);
    void release_behind();

    // Samples the sync template reaches either side of a position. It
    // reads a dead sector's worth each way (as `stage_track` computes it),
    // plus the parabolic refinement's one-sample probes.
    double template_margin() const { return 2.0 * kFaxDeadFrac * period_ + 4.0; }
    // Half-width of the NEXT row's sync search. Widens to half a line on a
    // re-acquisition sweep, which is why it is a function of the miss run
    // rather than a constant: the buffer test and the search itself must
    // ask for the same window, or a sweep would read samples nobody
    // required to be present.
    bool reacq_due() const {
        return opt_.autolock && has_pulse_ && miss_ >= kFaxReacqMisses &&
               (miss_ % kFaxReacqEvery) == 0;
    }
    double search_span() const {
        return reacq_due() ? 0.5 * period_ : opt_.search_frac * period_;
    }

    int fs_;
    PreviewOptions opt_;
    int width_ = 1810;
    double nominal_ = 0.0;  // seeded line period, samples, before any EMA
    double period_ = 0.0;   // the working period

    bool acquired_ = false;
    DeadSector dead_ = DeadSector::kBlackPulse;
    double dead_cons_ = 0.0;
    bool has_pulse_ = false;
    bool anchor_from_phasing_ = false;

    double next_start_ = 0.0;  // absolute sample position of the next row
    int row_index_ = 0;
    int locked_rows_ = 0;
    int reacquired_rows_ = 0;
    int miss_ = 0;  // consecutive unlocked rows, for the re-acquisition rule
    bool have_prev_lock_ = false;
    double prev_lock_pos_ = 0.0;
    int prev_lock_row_ = 0;
    bool pending_phase_mark_ = false;
    bool pending_sync_mark_ = false;

    Image img_;
    std::vector<uint8_t> row_;  // scratch, one row wide

    std::vector<float> buf_;  // video from buf_start_ to total_in_
    long long buf_start_ = 0;
    long long total_in_ = 0;
};

}  // namespace nova
