// session.hpp — the live session state machine [docs/05 §4]: the piece
// that turns the three built streaming stages (front end aside:
// tone_stream §5, preview §6) into a SESSION.
//
// The states are the operator's view of the protocol, not a percentage
// [docs/04 Finding 3]:
//
//   IDLE -> READY -> START TONE -> PHASING -> DRAWING — PREVIEW
//        -> STOP TONE -> DECODING -> SAVED
//
// plus the two operator-driven edges §4 names: the forced start (READY ->
// DRAWING — PREVIEW with operator IOC and rate) and the operator Stop,
// which takes DRAWING — PREVIEW -> DECODING by exactly the path a stop
// tone takes — freeze the snapshot, batch decode, save. Stop holds the
// image; it never discards it [§4, the SR-97 precedent]. From READY,
// where no rows have been drawn, Stop returns to IDLE with nothing to
// save; START TONE and PHASING behave the same way, because no picture
// line has arrived yet and there is nothing to hold.
//
// What this class owns [docs/05 §2-§3]:
//
//   - the retained video store (§3): everything from the start of the
//     transmission to its end, so the saved image can be a batch decode
//     of exactly what was received. While monitoring (READY, and the
//     post-decode states) only a short pre-roll is kept, so the snapshot
//     begins at the tone's true beginning — the streaming detector emits
//     min_start_sec into the tone, and a snapshot that started at the
//     EVENT would hand the batch decode a truncated tone.
//   - the streaming tone detector (§5), fed in every capturing state, so
//     monitoring never stops;
//   - the phasing watcher: from the start-tone event onwards the buffered
//     video is re-scanned with `detect_phasing` once per second of new
//     signal, at all three nominal line rates (the start tone names the
//     IOC [WMO §5.2.2] but not the rate, and the live path has no comb
//     scan to measure one with). A qualifying run that has CLOSED — the
//     buffer covers its end by more than the run-assembly gap — ends
//     PHASING: its `t_end` is where drawing starts, its `anchor` is the
//     preview's `phase_anchor` [WMO §5.2.3.4, the handoff session 21 found
//     missing], and its measured `period` is the rate seed [§6 seed order
//     item 2]. If the tone has ended and no run qualifies within
//     `phasing_wait_sec`, the transmission is drawn from the tone's end on
//     the nominal rate with no anchor — the phasing-less case, where the
//     signal simply does not say (§13).
//   - the provisional renderer (§6) from the drawing point onwards;
//   - the freeze: at the end of the transmission the retained store
//     becomes a `shared_ptr<const vector<float>>` and is handed to the
//     decode callback. Threading is the caller's business (§2 puts the
//     batch decode on thread 3); the callback may run `decode_fax` inline
//     and report back re-entrantly, or defer — `batch_done` /
//     `batch_failed` are valid whenever they arrive.
//
// What it does NOT do: touch a widget, know the time of day, name a file,
// or decode anything itself. The decode callback receives the snapshot and
// a filled-in DecodeOptions and does the rest.
//
// Block-size independence. Every decision here is a function of absolute
// stream position, never of how the caller chunked the pushes: the tone
// detector's events are pinned block-invariant by `live_tones`, the
// watcher steps on an absolute one-second grid, the preview is fed slices
// of an absolute range and is pinned block-invariant by `live_preview`,
// and the retained store is trimmed at absolute positions. The screamer
// (`tests/test_live_session.cpp`) asserts the whole session's outcome —
// state sequence, drawing point, rows, picture, snapshot bounds — is
// identical at several block sizes.
//
// One known live/batch difference, registered rather than fixed: the batch
// phasing rule "the LAST opening inside a known transmission" needs the
// stop tone, which has not happened yet live. A recording with two
// openings before one picture (FAXSignal) is drawn from the FIRST
// opening's phasing end, and the second opening appears in the preview as
// picture rows until the real picture arrives. The saved image crops
// correctly; the preview cannot, and a forward-only preview that waited
// to find out would not be a preview.
//
// Threading: a thread-2 object [docs/05 §2]. It allocates; never call it
// from the RtAudio callback. Operator controls (force_start / set_phase /
// set_sync / stop_capture) are expected to be marshalled onto the same
// thread by the caller.
#pragma once

#include "../core/fax.hpp"
#include "../core/hooks.hpp"
#include "../core/phasing.hpp"
#include "preview.hpp"
#include "tone_stream.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

namespace nova {

// The eight states of docs/05 §4, in protocol order. The names returned by
// session_state_name are the status-line strings; gui/nova-gui.cpp's
// LiveState is the display twin of this enum and keeps the same order.
enum class SessionState {
    kIdle,
    kReady,
    kStartTone,
    kPhasing,
    kDrawingPreview,
    kStopTone,
    kDecoding,
    kSaved,
};

const char* session_state_name(SessionState s);

struct SessionOptions {
    ToneOptions tones;
    PhasingOptions phasing;

    // Pre-roll kept while monitoring, seconds of video. Bounds the
    // retained store when no transmission is in progress, and lets the
    // snapshot start at the tone's true t_start rather than at the
    // detection event, which is emitted `min_start_sec` late by design
    // [docs/05 §5].
    double preroll_sec = 10.0;
    // Longest wait for a phasing interval after the start tone ends before
    // drawing without one. Phasing is ~30 s [WMO §5.2.3] and the batch
    // detector's own duration cap is 60 s [PhasingOptions::max_sec]; past
    // that, waiting costs picture the preview could have drawn.
    double phasing_wait_sec = 70.0;
    // Page cap [docs/05 §4, §12 item 3]: the guard for a MISSED stop
    // tone, not a format limit — a real transmission is ended by its stop
    // tone. 90 minutes is past the longest transmission in the library
    // (61 min), so the cap can only ever fire on a signal whose stop tone
    // was never heard.
    double max_picture_sec = 5400.0;

    DecodeHooks hooks;
};

// What one push() (or flush()) produced. The GUI queue's RowsDrawn and
// StateChanged messages [docs/05 §2.3] are these two vectors.
struct SessionOutput {
    std::vector<PreviewRow> rows;
    std::vector<SessionState> entered;  // states entered, in order
    bool decode_requested = false;
};

class LiveSession {
public:
    LiveSession(int fs, const SessionOptions& opt = SessionOptions());

    // The batch-decode handoff [docs/05 §3]: the retained store, frozen at
    // the end of the transmission, its absolute start in the session's
    // stream (the offset that relates a PreviewRow's renderer-local
    // positions to the saved image's raw stream), plus the DecodeOptions
    // to run with (IOC filled in when the operator forced it; otherwise
    // auto — the snapshot carries the start tone). §7.1's
    // phase_anchor_hint / clock_ppm_fallback join this handoff when those
    // fields exist; the live overrides that produced them are kept here
    // meanwhile (phase_set_/sync_ppm_).
    using DecodeCallback =
        std::function<void(std::shared_ptr<const std::vector<float>>,
                           long long snapshot_start, const DecodeOptions&)>;
    void set_decode_callback(DecodeCallback cb) { on_decode_ = std::move(cb); }

    // --- operator controls [docs/05 §4, §7] -------------------------------
    // All mutators return what happened, the way push() does: the states
    // entered, in order. (Rows and decode requests only ever come out of
    // push/flush.)
    SessionOutput start_capture();  // IDLE -> READY. Ignored elsewhere.
    // The Stop button. From DRAWING — PREVIEW it is the operator stop of
    // §4: the same freeze-decode-save path a stop tone takes, entered
    // straight into DECODING (there is no tone to wait out). From READY,
    // START TONE or PHASING no picture line has arrived, so there is
    // nothing to hold: back to IDLE. From STOP TONE the decode is already
    // running; Stop just leaves for DECODING. From DECODING/SAVED it ends
    // capture (IDLE).
    SessionOutput stop_capture();
    // Forced start [docs/04 Finding 2]: READY (or SAVED) -> DRAWING —
    // PREVIEW with operator-supplied IOC and rate, skipping the start
    // tone entirely. Ignored in every other state, as §4 specifies.
    SessionOutput force_start(int ioc, double lpm);
    // The live override surface, forwarded to the preview when drawing
    // [docs/05 §7]; remembered either way, because these values seed the
    // batch re-decode of THIS transmission once §7.1's fields exist.
    void set_phase(double frac);
    void set_sync(double ppm);

    // Feed demodulated video at the construction rate — the same domain
    // every other live component reads. Ignored while IDLE (not
    // capturing).
    SessionOutput push(const float* video, std::size_t n);
    SessionOutput push(const std::vector<float>& video) {
        return push(video.data(), video.size());
    }
    // End of stream. A drawing session ends its transmission here exactly
    // as at an operator stop; a session still inside an opening draws what
    // arrived after the tone's end, if anything, and ends that.
    SessionOutput flush();

    // Thread 3 reporting back [docs/05 §2.3's BatchDone / BatchFailed].
    // batch_done carries the result so the status panel can finally show
    // the clock and timebase figures [§4: they stay blank until the batch
    // decode produces them].
    SessionOutput batch_done(const DecodeResult& res);
    SessionOutput batch_failed(DecodeErrorKind kind);

    // --- what the status panel may ask ------------------------------------
    SessionState state() const { return state_; }
    // IOC of the transmission in progress (from the start tone or the
    // operator); 0 when unknown.
    int ioc() const { return ioc_; }
    // The preview, while one exists (DRAWING — PREVIEW onwards, until the
    // next transmission's opening replaces it).
    const StreamPreview* preview() const { return preview_.get(); }
    // Absolute stream position where drawing began — the offset between a
    // PreviewRow's start_sample (renderer-local) and the retained stream.
    long long draw_start_sample() const { return draw_start_; }
    // The decode's verdict, valid in SAVED.
    const DecodeResult* saved_result() const {
        return have_result_ ? &result_ : nullptr;
    }
    double consumed_sec() const {
        return static_cast<double>(total_in_) / fs_;
    }

private:
    void enter(SessionState s, SessionOutput& out);
    void begin_opening(const ToneEvent& e, SessionOutput& out);
    void begin_drawing(long long draw_start, const PreviewOptions& popts,
                       SessionOutput& out);
    void end_transmission(long long stop_sample, bool via_tone,
                          SessionOutput& out);
    void feed_preview(long long upto, SessionOutput& out);
    void watch_step(SessionOutput& out);
    void trim_preroll();
    void apply_batch_result(SessionOutput& out);

    int fs_;
    SessionOptions opt_;

    SessionState state_ = SessionState::kIdle;
    StreamToneDetector tones_;
    std::unique_ptr<StreamPreview> preview_;

    // The retained store (§3). Absolute sample index of retained_[0] is
    // retained_base_; while no transmission is in progress it is trimmed
    // to the pre-roll.
    std::vector<float> retained_;
    long long retained_base_ = 0;
    long long total_in_ = 0;
    bool in_transmission_ = false;

    // The opening in progress.
    int ioc_ = 0;
    bool forced_ = false;
    ToneKind tone_kind_ = ToneKind::kStartIOC576;
    bool tone_end_known_ = false;
    double tone_end_sec_ = 0.0;
    long long watch_from_ = 0;      // watcher slice start, absolute
    long long next_watch_at_ = 0;   // absolute grid, fs_ apart

    // The picture in progress.
    long long draw_start_ = 0;
    long long fed_up_to_ = 0;       // preview has been fed to here, absolute
    long long cap_sample_ = 0;      // the page cap, absolute

    // Operator overrides, remembered for the batch handoff.
    bool phase_set_ = false;
    double phase_frac_ = 0.0;
    bool sync_set_ = false;
    double sync_ppm_ = 0.0;

    // The decode handoff.
    DecodeCallback on_decode_;
    DecodeResult result_;
    bool have_result_ = false;
    bool batch_reported_ = false;   // batch_done/failed already arrived
    bool batch_ok_ = false;
    DecodeErrorKind batch_error_ = DecodeErrorKind::kEmptyInput;
    bool pending_start_ = false;    // a start tone qualified mid-DECODING
    ToneEvent pending_tone_{};

    // The decode handoff may complete re-entrantly (a callback that runs
    // decode_fax inline and calls batch_done from inside push's own
    // end_transmission). Re-entrant state changes are recorded in the
    // OUTER call's output, or the event order inverts.
    SessionOutput* active_out_ = nullptr;

    bool flushed_ = false;
};

}  // namespace nova
