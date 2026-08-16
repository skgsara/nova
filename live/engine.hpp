// engine.hpp — the wiring of docs/05 §2: the audio ring, the streaming
// front end, the live session and the batch decode, joined into the four
// threads the shell needs, with **no FLTK and no RtAudio**.
//
// Why this is in `nova-live` and not in `gui/nova-gui.cpp`. Everything
// between the sound card and the saved PNG can be wrong about a signal,
// and §1's rule is that anything which can be wrong about a signal must
// be drivable by a test with a fixture instead of a sound card. The GUI
// is then what §1 asks it to be: widgets, and the RtAudio callback that
// calls `push_audio`. `tests/test_live_engine.cpp` drives this class with
// a real producer thread over a real recording and never opens a window.
//
// The threads [docs/05 §2], and who owns what:
//
//   1  the caller's realtime audio callback -> `push_audio` -> the ring.
//      The only lock-free path; it allocates nothing (`live_ring` counts).
//   2  the engine's own thread: ring -> StreamResampler -> StreamDemod ->
//      LiveSession::push -> messages. It is the ONLY thread that touches
//      the session, which is what `session.hpp` asks for when it says the
//      operator controls must be marshalled onto one thread.
//   3  one per completed transmission: `decode_fax` over the frozen
//      snapshot, then the result into the batch inbox. It shares no
//      writable state with thread 2 — the snapshot is a
//      `shared_ptr<const vector<float>>` [§3].
//   4  the caller's GUI thread: `drain()`, `copy_image()`, and the
//      operator controls. It never touches the session, the preview or
//      the ring.
//
// **§2.3 said SPSC and that was wrong by one producer.** The document has
// thread 2 AND thread 3 pushing GUI messages, which is two producers on
// one queue. Rather than build a multi-producer queue, everything is
// funnelled through thread 2: thread 3 posts its result to a one-slot
// inbox, thread 2 picks it up, saves, calls `batch_done` and emits. The
// GUI queue then really is single-producer, `LiveSession` really is
// owned by one thread, and the observable event order is the session's
// own — which is the property session 22 had to fix a re-entrant
// `batch_done` to get.
//
// The three queues are mutex-guarded, and that is not a violation of §2's
// "never block": the no-lock rule is about thread 1, the realtime one,
// which touches only the ring. Threads 2, 3 and 4 may take an uncontended
// mutex for the length of a vector append.
//
// Thread 2 polls the ring rather than waiting on a condition variable,
// because the only thing that could signal it is thread 1, and a realtime
// callback may not touch a condvar. The poll interval is a latency floor,
// not a throughput limit: one wake drains everything the ring holds.
#pragma once

#include "../core/fax.hpp"
#include "../core/hooks.hpp"
#include "../core/image.hpp"
#include "png.hpp"
#include "ring.hpp"
#include "session.hpp"
#include "stream.hpp"

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace nova {

// The GUI queue's message kinds [docs/05 §2.3], plus the two the save
// path needs — §2.3 predates §8.5's decision that the decode completing
// is what writes the file.
enum class EngineMsg {
    kStateChanged,
    kRowsDrawn,
    kStats,
    kBatchProgress,
    kBatchDone,
    kBatchFailed,
    kSaved,
    kSaveFailed,
};

const char* engine_msg_name(EngineMsg m);

// One message. A single struct rather than a variant: every consumer is
// a switch in one function in the GUI, and the fields a kind does not use
// cost a pointer each.
struct EngineMessage {
    EngineMsg kind = EngineMsg::kStats;

    // kStateChanged
    SessionState state = SessionState::kIdle;
    int ioc = 0;

    // kRowsDrawn — the rows themselves, so the GUI can show the operator
    // marks §7 asks for, plus the totals the status line reads.
    std::vector<PreviewRow> rows;
    int rows_total = 0;
    int locked_rows = 0;
    int reacquired_rows = 0;

    // kStats — the level meter and the honesty counter [§2.1].
    double level_dbfs = -120.0;
    unsigned long long overruns = 0;
    double consumed_sec = 0.0;

    // kBatchProgress — the nine decode stages [§8].
    std::string stage;
    double fraction = 0.0;

    // kBatchDone / kBatchFailed
    std::shared_ptr<const DecodeResult> result;
    DecodeErrorKind error = DecodeErrorKind::kEmptyInput;

    // kSaved / kSaveFailed
    std::string path;
    std::string detail;
};

struct EngineOptions {
    SessionOptions session;
    int internal_rate = 8000;      // cli/nova-decode.cpp: kInternalRate
    double demod_center = 1900.0;
    double demod_deviation = 400.0;

    // 0 means 4 s at the capture rate [docs/05 §2.1].
    std::size_t ring_capacity = 0;

    // Where a completed decode writes its PNG [§8.5 item 1]. Empty means
    // do not save — the engine's own screamer uses that when it wants the
    // decode and not a file.
    std::string image_folder;

    // The clock, injected. `nova-live` has "no real clock" [§1] precisely
    // so a test can pin a filename; the GUI passes the system clock.
    // Returns a UTC stamp of the form 20260814T031544Z [§8.5 item 5].
    std::function<std::string()> utc_now;

    // How long thread 2 sleeps when the ring is dry.
    int poll_ms = 5;
};

// --- the filename rules of §8.5 item 5, as pure functions ------------------
// Free functions rather than private members, because the sanitizing rule
// is a claim about a dozen awkward strings and the cheapest way to defend
// a dozen awkward strings is to call the function with them.

// Anything in \ / : * ? " < > | and every run of whitespace becomes a
// single '-', the result is trimmed and capped at 32 characters, and if
// nothing survives the label is treated as blank [§8.5 item 5].
std::string sanitize_label(const std::string& label);

// "20260813T220417Z.png", or "20260813T220417Z-JMH.png" when the
// sanitized label is non-empty. The timestamp is first and always, so
// chronological order is alphabetical order.
std::string image_filename(const std::string& utc_stamp,
                           const std::string& label);

// The decode QA that goes into the PNG's tEXt chunks [§8.3 item 7]. The
// Furunos printing `Phase OK` / `Phase NG` on every chart are the
// precedent: the header tells the truth about how the picture was
// obtained. `phase_operator` / `sync_operator` record §8.5 item 3's
// requirement that a re-render says the values were the operator's — they
// say the operator SUPPLIED one; whether the decode used it is the
// result's own `anchor_from_hint` / `clock_from_fallback`, and under §7.1
// those two answers routinely differ for SYNC.
std::vector<PngText> decode_qa(const DecodeResult& r, const std::string& label,
                               bool phase_operator, bool sync_operator);

// The two retained raw streams of docs/05 §3, read by thread 4. The rule
// is stated by ROLE rather than by recency: the transmission currently
// being RECEIVED, and the image currently DISPLAYED. Usually they are the
// same object and only one exists; they diverge in the case §3 was written
// for — the operator is adjusting the chart that just arrived when the next
// transmission starts — and a third can never accumulate, because
// "currently displayed" is one image by definition.
// Two facts, kept apart on purpose. **Retention and reachability are not
// the same question**, and building item 3 is what showed it: §3's rule
// ("keep the stream behind the image the operator may be adjusting") and
// §8.2's rule ("a transmission arriving mid-edit does not take the pane")
// are one decision seen from two sides, and §8.2 is ROADMAP item 6, not
// built. So the stream is retained by role exactly as §3 says, and while
// the next transmission's PREVIEW owns the pane it is correctly not
// offered — because the picture the operator is looking at then is the
// preview, not the chart. `on_pane` is the half item 6 will change; the
// retention above it does not move when it does.
struct RetainedVideo {
    // The frozen stream behind the most recently decoded image — §3's
    // "the image currently displayed", which it is except in the window
    // item 6 will close. Null when there is no such image.
    std::shared_ptr<const std::vector<float>> decoded;
    // Absolute sample index of decoded->front() in the session's stream,
    // as the session handed it over: the offset that relates a PreviewRow's
    // renderer-local positions to this stream.
    long long decoded_start = 0;
    // What produced that image, kept verbatim. A correction re-decodes THIS
    // with the two §7.1 fields changed; Auto re-decodes it with them
    // cleared. Keeping the record faithful is what lets both be derived
    // from it rather than reconstructed.
    DecodeOptions decoded_options;
    // Is that image the one the pane is showing? False while a provisional
    // preview owns the pane.
    bool on_pane = false;
    // Samples held for the transmission being received. Not frozen — it is
    // still growing — and bounded by the pre-roll while monitoring.
    std::size_t receiving_samples = 0;

    // Why a correction cannot be offered, in the operator's words. §3:
    // manual adjustment is "not offered and then found not to work", so a
    // control that cannot act is disabled WITH THE REASON rather than
    // silently inert. Empty when it can.
    std::string unavailable_reason;

    bool can_correct() const { return decoded != nullptr && on_pane; }
    // What §3's cost analysis counts: 4 bytes a sample, both roles.
    std::size_t bytes() const {
        return (decoded ? decoded->size() : 0) * sizeof(float) +
               receiving_samples * sizeof(float);
    }
};

// The operator's two corrections as the panel holds them: each either
// typed or blank [docs/05 §8.5 item 6 — "PHASE and SYNC reset to
// measured-or-blank for every transmission"]. Blank means "as measured",
// so **Auto is not a third mode: it is the empty correction.**
struct Correction {
    bool phase_set = false;
    double phase_frac = 0.0;   // fraction of a line, as §7.1's seed
    bool sync_set = false;
    double sync_ppm = 0.0;     // line-rate trim, as §7.1's fallback
};

class LiveEngine {
public:
    LiveEngine(int capture_rate, const EngineOptions& opt);
    ~LiveEngine();

    LiveEngine(const LiveEngine&) = delete;
    LiveEngine& operator=(const LiveEngine&) = delete;

    // --- thread 1: the realtime audio callback -----------------------------
    // Allocation-free, lock-free, never throws. What does not fit is
    // counted, not hidden [§2.1]. Returns the number of samples accepted;
    // a realtime callback has nothing to do with the shortfall but a test
    // feeding a fixture does, and so does anything that wants to know the
    // loss was ours rather than the band's.
    std::size_t push_audio(const float* in, std::size_t n) {
        return ring_.write(in, n);
    }

    // --- thread 4: the operator ---------------------------------------------
    // All queued for thread 2, never executed on the caller's thread, so
    // the session has exactly one owner.
    void start_capture();
    void stop_capture();
    void force_start(int ioc, double lpm);
    void set_phase(double frac);
    void set_sync(double ppm);
    // The label reaches the filename at the next save and the PNG text at
    // every save [§8.5 item 5]. Nova never renames a file already written.
    void set_label(const std::string& label);
    // Where the NEXT completed decode writes [§8.5 item 1]. Queued like
    // every other control, because thread 2 is what reads it at save
    // time. Files already written keep their names: Nova never renames.
    void set_image_folder(const std::string& folder);

    // --- thread 4: what the widgets read ------------------------------------
    std::vector<EngineMessage> drain();
    // The picture, copied under the image lock so the caller can paint
    // from its own copy with nothing held. Returns false while there is
    // no picture. Rows are appended and never revised, so this is a
    // snapshot of a prefix, never a torn image.
    bool copy_image(Image* out);

    // --- thread 4: the post-decode correction [docs/05 §8.5 items 2-4] ------
    // Re-render the image on the pane from the raw stream retained behind
    // it [§3], and OVERWRITE the file it was saved to — one transmission,
    // one file [§8.5 item 2], which is also why there is no Save button
    // [item 3]. Apply sends the operator's values; Auto sends `{}`.
    //
    // **This is the one decode `LiveSession` does not own** [Sara, session
    // 27]. Every other decode is a state change the machine made; this one
    // is the operator asking for the same transmission again, and the
    // machine stays in SAVED throughout — `batch_done` from SAVED is
    // already a no-op, so the state cannot be corrupted by it. What is
    // shared with the automatic path is everything that matters: the same
    // one-slot batch inbox, the same thread 3, the same collect-save-post
    // tail. The alternative — a re-decode state on the session — was
    // weighed and rejected as more surface for a decode that changes no
    // state [ROADMAP M4 item 4].
    //
    // Ignored when there is nothing to re-render (`retained_video()`
    // reports why) or while any decode is already running: the shell is
    // serialized, one decode at a time [§8.3 item 4].
    void redecode(const Correction& c);
    // True while a re-decode is running. The session is in SAVED and says
    // so, which is the truth; this is the other truth the shell needs, so
    // the progress bar can move and the transport can hold still.
    bool redecoding() const {
        return redecoding_.load(std::memory_order_acquire);
    }

    // The retained raw video [§3], by role. Safe from thread 4 at any
    // time: the displayed snapshot is immutable once frozen, so this hands
    // back a shared_ptr to it rather than a copy of 38 MB, and taking that
    // reference is what guarantees a correction cannot have the stream
    // pulled out from under it mid-decode.
    RetainedVideo retained_video() const;

    // --- thread 4: the background buffer [§8.2, ROADMAP M4 item 6] ----------
    // **The edit holds the pane.** While the operator is correcting a
    // decoded chart, a transmission that arrives does not take the screen
    // from them: its rows grow a SECOND image behind the one being
    // corrected, and the shell shows a compact receiving indicator instead
    // of swapping the pane [§8.2].
    //
    // The shell owns the predicate, not the engine: "an edit is in
    // progress" is a fact about typed boxes and clicks [§8.5 item 4], which
    // only thread 4 can see. The engine is told, and the telling is an
    // atomic rather than a queued command because it is read by thread 2 on
    // every batch of rows and must never be one tick stale — a tick late
    // here is the operator's picture already gone.
    void set_pane_held(bool held);
    bool pane_held() const {
        return pane_held_.load(std::memory_order_acquire);
    }

    // What the receiving indicator shows [§8.2: state, line count,
    // thumbnail]. `rows`/`width` describe the buffered picture; `complete`
    // means its own decode has finished and is parked whole, waiting to be
    // brought forward.
    struct Background {
        bool active = false;
        int rows = 0;
        int width = 0;
        bool complete = false;
    };
    Background background() const;
    // The buffered picture itself, for the indicator's thumbnail. False
    // while nothing is buffered.
    bool copy_background_image(Image* out);

    // The announced swap [§8.2]: the buffered picture comes forward when
    // the edit ends, by Apply, by Auto, or because the operator clicked the
    // indicator. Queued like every other control, and a no-op when there is
    // nothing buffered — ask `background().active` first if the answer
    // matters.
    //
    // **Three things travel with the picture, and every one of them would
    // be a defect on its own.** A completed background decode carries a raw
    // snapshot (`retained_video`'s displayed role) and a saved path
    // (`saved_path_`, which a re-render OVERWRITES [§8.5 item 2]), and the
    // picture it is waiting behind carries its own of each. Left to the
    // default, a transmission finishing mid-edit takes all three: the
    // operator's Apply then re-decodes from a stream that is not theirs and
    // writes the result over the newly received chart's PNG. So image,
    // snapshot and path are parked together and promoted together, which is
    // what keeps `retained_video()` and `saved_path_` describing the picture
    // actually on the pane at every instant.
    void promote_background();

    unsigned long long overruns() const { return ring_.overruns(); }
    std::size_t ring_capacity() const { return ring_.capacity(); }

    // --- lifecycle ----------------------------------------------------------
    // Spawns thread 2. The session stays IDLE until start_capture().
    void run();
    // End of stream: no more audio will arrive. Thread 2 drains the ring,
    // flushes the session, waits for any batch decode, and stops. Joins
    // both threads. Idempotent; the destructor calls it.
    void shutdown();

private:
    enum class CmdKind { kStart, kStop, kForce, kPhase, kSync, kLabel,
                         kFolder, kRedecode, kPromote };
    struct Cmd {
        CmdKind kind = CmdKind::kStart;
        int ioc = 0;
        double a = 0.0;
        std::string text;
        Correction correction;
    };

    void thread2();
    void run_commands();
    // Thread 2's half of `promote_background` [§8.2]. It runs here and not
    // on thread 4 because the picture does not travel alone: `saved_path_`
    // travels with it, and that is thread 2's, written where the file is
    // written.
    void do_promote_background();
    void emit(const SessionOutput& out);
    void post(EngineMessage m);
    void begin_batch(std::shared_ptr<const std::vector<float>> snap,
                     long long start, const DecodeOptions& dopt);
    void start_redecode(const Correction& c);
    void collect_batch();
    void append_display_rows();
    std::string save_image(const DecodeResult& r, bool overwrite);

    int capture_rate_;
    EngineOptions opt_;

    AudioRing ring_;
    StreamResampler resamp_;
    StreamDemod demod_;
    LiveSession session_;

    std::thread t2_;
    std::thread t3_;
    std::atomic<bool> stopping_{false};
    std::atomic<bool> running_{false};

    std::mutex cmd_mu_;
    std::vector<Cmd> cmds_;

    std::mutex out_mu_;
    std::vector<EngineMessage> outbox_;

    // Thread 3 -> thread 2. One slot: the shell is serialized, one
    // transmission decoding at a time [§8.3 item 4, the Start button dead
    // during DECODING].
    std::mutex batch_mu_;
    bool batch_ready_ = false;
    bool batch_ok_ = false;
    DecodeResult batch_result_;
    DecodeErrorKind batch_error_ = DecodeErrorKind::kEmptyInput;
    std::atomic<bool> batch_running_{false};

    // Thread 3 -> thread 2, for the progress bar. Kept out of the outbox
    // so the claim above — one producer on the GUI queue — stays true. It
    // is a slot rather than a queue on purpose: progress is a level, not
    // an event, and coalescing nine stages down to whatever thread 2 saw
    // last is exactly right for a bar repainted at 20 Hz.
    std::mutex prog_mu_;
    std::string prog_stage_;
    double prog_frac_ = 0.0;
    bool prog_new_ = false;

    // Mutable because `retained_video` is a const question — "what is on
    // the pane, and does it have a stream behind it" — that must still take
    // the lock to ask it.
    mutable std::mutex img_mu_;
    Image display_;
    // Identity of the preview `display_` was grown from. A new
    // transmission replaces the preview and restarts its image at row 0,
    // so the pane must start over rather than append to the old chart.
    const StreamPreview* display_src_ = nullptr;

    // §8.2's background buffer, guarded by `img_mu_` exactly as `display_`
    // is — one lock for "what pictures exist", so no order between them can
    // be got wrong. `background_complete_` distinguishes a preview still
    // growing from a finished decode parked whole: the indicator says
    // different things about the two, and only the second has a snapshot
    // parked with it.
    Image background_;
    const StreamPreview* background_src_ = nullptr;
    bool background_complete_ = false;
    // Set by thread 4, read by thread 2 on every batch of rows [see
    // set_pane_held].
    std::atomic<bool> pane_held_{false};

    // The retained video of §3. `pending_*` is the snapshot the decode
    // running right now was started from; it becomes `displayed_*` at the
    // moment that decode's image takes the pane, and the snapshot the
    // OUTGOING image was decoded from is released there and only there —
    // which is the whole of §3's "when the operator moves on, the older
    // snapshot is released", because moving on is what puts another image
    // on screen.
    //
    // Written by thread 2 (begin_batch, collect_batch), read by thread 4
    // (retained_video). The mutex is held for a shared_ptr copy, never
    // across a decode: §2's no-block rule is about thread 1.
    mutable std::mutex retain_mu_;
    std::shared_ptr<const std::vector<float>> pending_snap_;
    long long pending_start_ = 0;
    DecodeOptions pending_options_;
    std::shared_ptr<const std::vector<float>> displayed_snap_;
    long long displayed_start_ = 0;
    DecodeOptions displayed_options_;
    // The third role, and it exists only while the pane is held: the
    // snapshot of a transmission whose decode FINISHED behind an edit
    // [§8.2]. It is not `displayed_*` because its picture is not displayed,
    // and it is not `pending_*` because nothing is decoding it any more.
    // Promotion moves it to `displayed_*` in the same breath as its image
    // reaches the pane [see promote_background].
    std::shared_ptr<const std::vector<float>> parked_snap_;
    long long parked_start_ = 0;
    DecodeOptions parked_options_;
    // The parked picture's file, thread 2's like `saved_path_` itself. It
    // is the third thing that travels with a picture [see
    // promote_background] and the one whose loss is destructive rather than
    // merely wrong: an Apply against a stale `saved_path_` overwrites a
    // chart that was received correctly.
    std::string parked_saved_path_;
    // Thread 2 publishes the size of the store the SESSION is still
    // growing; thread 4 may not touch the session to ask.
    std::atomic<std::size_t> receiving_samples_{0};

    // Thread 2 only, after the commands are drained.
    std::string label_;
    bool phase_operator_ = false;
    bool sync_operator_ = false;
    // The file the image on the pane was written to. A re-render
    // OVERWRITES it — one transmission, one file [§8.5 item 2] — and Nova
    // never renames, so a label typed after the automatic save reaches the
    // PNG's text chunks on the next Apply and the name does not move
    // [§8.5 item 5].
    std::string saved_path_;
    // Set while the decode in flight is a re-render rather than a
    // transmission's own. Thread 2 writes it, `collect_batch` reads it to
    // choose the filename; the atomic below is the same fact for thread 4.
    bool batch_is_redecode_ = false;
    std::atomic<bool> redecoding_{false};
};

}  // namespace nova
