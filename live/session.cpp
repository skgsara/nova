// session.cpp — see session.hpp for the contract, the ownership story and
// the block-size independence argument.
#include "session.hpp"

#include <algorithm>
#include <cmath>

namespace nova {

const char* session_state_name(SessionState s) {
    switch (s) {
        case SessionState::kIdle: return "IDLE";
        case SessionState::kReady: return "READY";
        case SessionState::kStartTone: return "START TONE";
        case SessionState::kPhasing: return "PHASING";
        case SessionState::kDrawingPreview: return "DRAWING \xe2\x80\x94 PREVIEW";
        case SessionState::kStopTone: return "STOP TONE";
        case SessionState::kDecoding: return "DECODING";
        case SessionState::kSaved: return "SAVED";
    }
    return "?";
}

namespace {

// The watcher scans at all three nominal line rates, because the start
// tone names the IOC [WMO §5.2.2] but nothing in the opening names the
// rate, and the live path has no comb scan. detect_phasing is cheap at
// these lengths; doing all three every step keeps the choice a pure
// function of the signal, never of when the scan happened to run.
const double kWatchRates[] = {120.0, 90.0, 60.0};

}  // namespace

LiveSession::LiveSession(int fs, const SessionOptions& opt)
    : fs_(fs), opt_(opt), tones_(fs, opt.tones, opt.hooks) {}

void LiveSession::enter(SessionState s, SessionOutput& out) {
    if (s == state_) return;
    state_ = s;
    out.entered.push_back(s);
    dlog(opt_.hooks, LogTopic::kInfo, "session: -> %s",
         session_state_name(s));
}

// --------------------------------------------------------------------------
// Operator controls
// --------------------------------------------------------------------------

SessionOutput LiveSession::start_capture() {
    SessionOutput out;
    active_out_ = &out;
    // SAVED is the other door back to READY [docs/05 §4: SAVED leaves on
    // "next transmission, or operator action" — Start IS that action].
    // Without it the button was active and labelled and did nothing, which
    // is the one thing a control must never be (found by Sara at the
    // keyboard, session 26: she clicked Start after a save and the shell
    // sat in SAVED until a tone happened to move it).
    if (state_ == SessionState::kIdle || state_ == SessionState::kSaved) {
        flushed_ = false;
        enter(SessionState::kReady, out);
    }
    active_out_ = nullptr;
    return out;
}

SessionOutput LiveSession::stop_capture() {
    SessionOutput out;
    active_out_ = &out;
    switch (state_) {
        case SessionState::kDrawingPreview:
            // The operator stop of §4: exactly the path a stop tone takes,
            // minus the tone. The image is held, never discarded.
            end_transmission(total_in_, /*via_tone=*/false, out);
            break;
        case SessionState::kStopTone:
            // The decode is already running; stop waiting out the tone.
            enter(SessionState::kDecoding, out);
            apply_batch_result(out);
            break;
        case SessionState::kReady:
        case SessionState::kStartTone:
        case SessionState::kPhasing:
            // No picture line has arrived, so there is nothing to hold.
            in_transmission_ = false;
            preview_.reset();
            enter(SessionState::kIdle, out);
            break;
        default:
            enter(SessionState::kIdle, out);
            break;
    }
    active_out_ = nullptr;
    return out;
}

SessionOutput LiveSession::force_start(int ioc, double lpm) {
    SessionOutput out;
    active_out_ = &out;
    if (state_ == SessionState::kReady || state_ == SessionState::kSaved) {
    ioc_ = ioc;
    forced_ = true;
    in_transmission_ = true;
    preview_.reset();
    have_result_ = false;
    // Nothing received before this moment belongs to the transmission.
    retained_.clear();
    retained_base_ = total_in_;
    PreviewOptions po;
    po.ioc = ioc;
    po.lpm = lpm;
    po.seed = PreviewSeed::kOperator;
    po.hooks = opt_.hooks;
    begin_drawing(total_in_, po, out);
    }
    active_out_ = nullptr;
    return out;
}

void LiveSession::set_phase(double frac) {
    phase_set_ = true;
    phase_frac_ = frac;
    if (preview_) preview_->set_phase_anchor(frac);
}

void LiveSession::set_sync(double ppm) {
    sync_set_ = true;
    sync_ppm_ = ppm;
    if (preview_) preview_->set_clock_ppm(ppm);
}

// --------------------------------------------------------------------------
// The video path
// --------------------------------------------------------------------------

SessionOutput LiveSession::push(const float* video, std::size_t n) {
    SessionOutput out;
    active_out_ = &out;
    if (state_ == SessionState::kIdle || flushed_ || n == 0) {
        active_out_ = nullptr;
        return out;
    }

    // Everything arriving while capturing is retained; how much of it
    // SURVIVES is decided by state (trim_preroll below, or the freeze).
    retained_.insert(retained_.end(), video, video + n);
    total_in_ += static_cast<long long>(n);

    // The tone detector is fed in every capturing state: monitoring never
    // stops, which is what the next transmission's start tone relies on.
    const std::vector<ToneEvent> events = tones_.push(video, n);
    for (const ToneEvent& e : events) {
        if (e.kind == ToneKind::kStop) {
            // A stop tone ends the picture only when one is being drawn.
            // In every other state it is a recording that joined a
            // transmission mid-tone, and it decides nothing.
            if (state_ == SessionState::kDrawingPreview) {
                end_transmission(
                    static_cast<long long>(std::llround(e.t_start * fs_)),
                    /*via_tone=*/true, out);
            }
        } else {
            if (state_ == SessionState::kReady ||
                state_ == SessionState::kSaved) {
                begin_opening(e, out);
            } else if (state_ == SessionState::kDecoding) {
                // The GUI is serialized [docs/05 §8.4]: the next
                // transmission's tone is remembered, not acted on, until
                // the batch decode has reported.
                pending_start_ = true;
                pending_tone_ = e;
            }
            // In START TONE / PHASING / DRAWING a start tone is a second
            // opening inside a transmission this session is already
            // committed to. "One recording, one transmission, take the
            // first" is the batch rule [ROADMAP registered gaps] and the
            // live path keeps it — see the header for the FAXSignal case.
        }
    }

    // The phasing watcher, on its absolute one-second grid.
    while (next_watch_at_ <= total_in_ &&
           (state_ == SessionState::kStartTone ||
            state_ == SessionState::kPhasing)) {
        watch_step(out);
        next_watch_at_ += fs_;
    }

    // When the start tone's run closes, its last hot frame is the tone's
    // end — the reference the phasing give-up waits from.
    if (!tone_end_known_ &&
        (state_ == SessionState::kStartTone ||
         state_ == SessionState::kPhasing)) {
        const double e = tones_.run_last_hot_sec(tone_kind_);
        if (e >= 0.0 && !tones_.run_open(tone_kind_)) {
            tone_end_known_ = true;
            tone_end_sec_ = e;
        }
    }

    // The opening cap [D-PERF-003]. Every other bound is measured from
    // the tone's end or from drawing, and on a start tone that never ends
    // neither exists — the session would sit here with the retained store
    // growing without bound. Abandon the opening and return to
    // monitoring; trim_preroll() below bounds the store again, and the
    // next real start tone is heard from READY as usual.
    if ((state_ == SessionState::kStartTone ||
         state_ == SessionState::kPhasing) &&
        total_in_ - opening_start_ >
            static_cast<long long>(std::llround(opt_.max_opening_sec * fs_))) {
        in_transmission_ = false;
        tone_end_known_ = false;
        enter(SessionState::kReady, out);
    }

    // The stop tone has ended when its run closes [§4: STOP TONE leaves
    // on "tone ends"]. The decode request went out when the tone was
    // found — thread 3 does not wait for this.
    if (state_ == SessionState::kStopTone &&
        !tones_.run_open(ToneKind::kStop)) {
        enter(SessionState::kDecoding, out);
        apply_batch_result(out);
    }

    // The picture, and the page cap that guards a missed stop tone
    // [docs/05 §12 item 3]. The preview is fed to the tone detector's
    // SAFE HORIZON, not to the stream end: a stop tone qualifies
    // min_stop_sec after it begins, and rows fed past the tone start in
    // the meantime would depend on how the pushes happened to chunk. The
    // horizon is an absolute position, so the drawn rows cannot.
    if (state_ == SessionState::kDrawingPreview) {
        if (total_in_ >= cap_sample_)
            end_transmission(cap_sample_, /*via_tone=*/false, out);
        else
            feed_preview(tones_.safe_horizon_samples(), out);
    }

    trim_preroll();
    active_out_ = nullptr;
    return out;
}

SessionOutput LiveSession::flush() {
    SessionOutput out;
    active_out_ = &out;
    if (!flushed_) {
    flushed_ = true;
    switch (state_) {
        case SessionState::kDrawingPreview:
            // End of stream ends the transmission exactly as an operator
            // stop does.
            end_transmission(total_in_, /*via_tone=*/false, out);
            break;
        case SessionState::kStopTone:
            enter(SessionState::kDecoding, out);
            apply_batch_result(out);
            break;
        case SessionState::kStartTone:
        case SessionState::kPhasing: {
            const long long tone_end = static_cast<long long>(
                std::llround(tone_end_sec_ * fs_));
            if (tone_end_known_ && total_in_ > tone_end) {
                // The stream ended inside (or past) an opening whose
                // phasing never qualified. Draw whatever arrived after
                // the tone — possibly nothing — and end there.
                PreviewOptions po;
                po.ioc = ioc_;
                po.seed = PreviewSeed::kSignal;
                po.hooks = opt_.hooks;
                begin_drawing(tone_end, po, out);
                end_transmission(total_in_, /*via_tone=*/false, out);
            } else {
                // The stream ended inside the opening itself: no picture
                // line ever arrived, so there is nothing to hold.
                in_transmission_ = false;
                enter(SessionState::kIdle, out);
            }
            break;
        }
        default:
            break;
    }
    }
    active_out_ = nullptr;
    return out;
}

// --------------------------------------------------------------------------
// The batch handoff
// --------------------------------------------------------------------------

void LiveSession::end_transmission(long long stop_sample, bool via_tone,
                                   SessionOutput& out) {
    if (preview_) {
        feed_preview(stop_sample, out);
        const std::vector<PreviewRow> rows = preview_->flush();
        out.rows.insert(out.rows.end(), rows.begin(), rows.end());
    }

    stop_sample = std::max(stop_sample, retained_base_);
    stop_sample = std::min(stop_sample, total_in_);
    const long long snap_start = retained_base_;
    const std::size_t n =
        static_cast<std::size_t>(stop_sample - retained_base_);
    auto snapshot = std::make_shared<std::vector<float>>(
        retained_.begin(), retained_.begin() + static_cast<std::ptrdiff_t>(n));

    DecodeOptions dopt;
    // The operator's IOC when forced; auto otherwise — the snapshot
    // carries the start tone, and batch IOC selection from it is the
    // proven path [ISO §4.2.5].
    dopt.ioc = forced_ ? ioc_ : 0;
    // The live corrections, carried into THIS transmission's re-decode and
    // no other [docs/05 §7, §7.1]. They are handed over as what they are —
    // a seed for the anchor search and a fallback for the clock — so the
    // operator's one action means the same thing in the preview they
    // corrected and in the picture that replaces it. Untouched controls
    // hand over nothing: the defaults are "no hint" and "no fallback", not
    // zeroes, because 0 ppm is a legal clock and column 0 a legal anchor.
    if (phase_set_) dopt.phase_anchor_hint = phase_frac_;
    if (sync_set_) dopt.clock_ppm_fallback = sync_ppm_;
    dopt.hooks = opt_.hooks;

    // The store starts over for the next transmission; the pre-roll rule
    // applies from here.
    retained_.erase(retained_.begin(),
                    retained_.begin() + static_cast<std::ptrdiff_t>(n));
    retained_base_ = stop_sample;
    in_transmission_ = false;
    have_result_ = false;
    batch_reported_ = false;
    pending_start_ = false;

    // The state is entered BEFORE the callback fires, so a callback that
    // runs decode_fax inline and reports back re-entrantly finds the
    // machine where §4 says it should be.
    enter(via_tone ? SessionState::kStopTone : SessionState::kDecoding, out);
    out.decode_requested = true;
    if (on_decode_) on_decode_(snapshot, snap_start, dopt);
}

SessionOutput LiveSession::batch_done(const DecodeResult& res) {
    SessionOutput local;
    // Re-entrant completion (an inline decode callback) records into the
    // outer call's output, so the event order survives: DECODING, then
    // SAVED, never the reverse.
    SessionOutput& out = active_out_ ? *active_out_ : local;
    if (state_ != SessionState::kDecoding &&
        state_ != SessionState::kStopTone)
        return local;
    batch_reported_ = true;
    batch_ok_ = true;
    result_ = res;
    apply_batch_result(out);
    return local;
}

SessionOutput LiveSession::batch_failed(DecodeErrorKind kind) {
    SessionOutput local;
    SessionOutput& out = active_out_ ? *active_out_ : local;
    if (state_ != SessionState::kDecoding &&
        state_ != SessionState::kStopTone)
        return local;
    batch_reported_ = true;
    batch_ok_ = false;
    batch_error_ = kind;
    apply_batch_result(out);
    return local;
}

void LiveSession::apply_batch_result(SessionOutput& out) {
    if (!batch_reported_ || state_ != SessionState::kDecoding) return;
    if (batch_ok_) {
        have_result_ = true;
        enter(SessionState::kSaved, out);
        // A start tone that qualified mid-decode begins the next
        // transmission now [§4: SAVED leaves on the next transmission].
        if (pending_start_) {
            pending_start_ = false;
            begin_opening(pending_tone_, out);
        }
    } else {
        // The picture is not discarded — the preview stays in the pane —
        // but nothing was saved, and monitoring resumes.
        dlog(opt_.hooks, LogTopic::kInfo,
             "session: batch decode failed (kind %d); back to READY",
             static_cast<int>(batch_error_));
        enter(SessionState::kReady, out);
    }
}

// --------------------------------------------------------------------------
// The opening: start tone, then the phasing watcher
// --------------------------------------------------------------------------

void LiveSession::begin_opening(const ToneEvent& e, SessionOutput& out) {
    ioc_ = (e.kind == ToneKind::kStartIOC288) ? 288 : 576;
    forced_ = false;
    tone_kind_ = e.kind;
    tone_end_known_ = false;
    in_transmission_ = true;
    preview_.reset();
    have_result_ = false;

    // The snapshot starts at the tone's true beginning, which the pre-roll
    // is there to make available; the detection event is emitted
    // min_start_sec into the tone by design [docs/05 §5].
    long long floor = static_cast<long long>(std::llround(e.t_start * fs_));
    if (floor < retained_base_) floor = retained_base_;
    retained_.erase(
        retained_.begin(),
        retained_.begin() +
            static_cast<std::ptrdiff_t>(floor - retained_base_));
    retained_base_ = floor;

    opening_start_ = floor;
    watch_from_ = floor;
    next_watch_at_ = floor + fs_;
    enter(SessionState::kStartTone, out);
}

void LiveSession::watch_step(SessionOutput& out) {
    const std::size_t off =
        static_cast<std::size_t>(watch_from_ - retained_base_);
    const std::vector<float> slice(retained_.begin() +
                                       static_cast<std::ptrdiff_t>(off),
                                   retained_.end());
    const double slice_sec = static_cast<double>(slice.size()) / fs_;

    // The first qualifying run at each nominal rate. The batch window rule
    // ("the last opening inside a known transmission") needs the stop tone
    // and cannot be known live, so the window is left unset and the FIRST
    // run wins, as it does unwindowed in the batch path.
    PhasingOptions pho = opt_.phasing;
    pho.t_lo = 0.0;
    pho.t_hi = 0.0;
    PhasingResult best;
    double best_rate = 120.0;
    for (double rate : kWatchRates) {
        const double period = fs_ * 60.0 / rate;
        const PhasingResult r =
            detect_phasing(slice, fs_, period, pho, opt_.hooks);
        if (!r.found) continue;
        // Deterministic preference: more agreeing lines, then the cleaner
        // run. Two rates qualifying on one signal is not expected — the
        // wedge sits at a different line phase at each wrong rate — but
        // the rule has to be written down, not left to fall out.
        if (best.found && r.lines < best.lines) continue;
        if (best.found && r.lines == best.lines && r.score <= best.score)
            continue;
        best = r;
        best_rate = rate;
    }

    if (!best.found) {
        // The phasing-less case: the tone has ended, nothing has qualified
        // within the wait, and the signal simply does not say where the
        // line starts. Draw from the tone's end on the nominal rate;
        // the operator's PHASE is the answer from there [docs/05 §13].
        const double tone_end_in_slice =
            tone_end_sec_ -
            static_cast<double>(watch_from_) / static_cast<double>(fs_);
        if (tone_end_known_ &&
            slice_sec > tone_end_in_slice + opt_.phasing_wait_sec) {
            PreviewOptions po;
            po.ioc = ioc_;
            po.seed = PreviewSeed::kSignal;
            po.hooks = opt_.hooks;
            begin_drawing(
                static_cast<long long>(std::llround(tone_end_sec_ * fs_)),
                po, out);
        }
        return;
    }

    if (state_ == SessionState::kStartTone)
        enter(SessionState::kPhasing, out);

    // Act only on a CLOSED run: one the buffer outlasts by more than the
    // run-assembly gap, so its t_end and anchor are final and no block
    // size can observe a different one. An open run's t_end is still
    // moving, and a decision taken on it would depend on when the scan
    // happened to run.
    const double period =
        best.period > 0.0 ? best.period : fs_ * 60.0 / best_rate;
    const double margin_sec =
        (opt_.phasing.max_gap + 2.0) * (period / fs_);
    if (slice_sec - best.t_end <= margin_sec) return;

    const long long draw_start =
        watch_from_ + static_cast<long long>(std::llround(best.t_end * fs_));
    PreviewOptions po;
    po.ioc = ioc_;
    po.lpm = 60.0 / (period / fs_);
    po.seed = PreviewSeed::kSignal;
    po.hooks = opt_.hooks;
    // The handoff session 21 found missing [docs/05 §6 item 1]: the
    // leading edge of white is dead-sector entry [WMO §5.2.3.4], and on a
    // white-only station it is the only line-start information the
    // transmission carries. The renderer still prefers its own per-line
    // template where a pulse exists — same rule as decode_fax.
    const double anchor_abs =
        static_cast<double>(watch_from_) + best.anchor;
    double f =
        std::fmod(anchor_abs - static_cast<double>(draw_start), period);
    if (f < 0.0) f += period;
    po.phase_anchor = f / period;
    begin_drawing(draw_start, po, out);
}

// --------------------------------------------------------------------------
// The picture
// --------------------------------------------------------------------------

void LiveSession::begin_drawing(long long draw_start,
                                const PreviewOptions& popts,
                                SessionOutput& out) {
    preview_ = std::make_unique<StreamPreview>(fs_, popts);
    draw_start_ = draw_start;
    fed_up_to_ = draw_start;
    cap_sample_ =
        draw_start +
        static_cast<long long>(std::llround(opt_.max_picture_sec * fs_));
    enter(SessionState::kDrawingPreview, out);
    // The backlog — the lines between the drawing point and now — is fed
    // at once. Nothing is lost to the acquisition wait: those lines are
    // the ones drawn first [live/preview.hpp]. The feed still respects
    // the tone detector's safe horizon, so the last fraction of a second
    // arrives through the same path as everything else.
    feed_preview(tones_.safe_horizon_samples(), out);
}

void LiveSession::feed_preview(long long upto, SessionOutput& out) {
    if (!preview_ || upto <= fed_up_to_) return;
    upto = std::min(upto, total_in_);
    const std::size_t off =
        static_cast<std::size_t>(fed_up_to_ - retained_base_);
    const std::size_t n = static_cast<std::size_t>(upto - fed_up_to_);
    std::vector<PreviewRow> rows =
        preview_->push(retained_.data() + off, n);
    fed_up_to_ = upto;
    out.rows.insert(out.rows.end(), rows.begin(), rows.end());
}

void LiveSession::trim_preroll() {
    if (in_transmission_) return;
    const std::size_t keep =
        static_cast<std::size_t>(opt_.preroll_sec * fs_);
    if (retained_.size() <= keep) return;
    const std::size_t drop = retained_.size() - keep;
    retained_.erase(retained_.begin(),
                    retained_.begin() + static_cast<std::ptrdiff_t>(drop));
    retained_base_ += static_cast<long long>(drop);
}

}  // namespace nova
