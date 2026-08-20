// engine.cpp — see engine.hpp for the thread map and for why §2.3's
// "SPSC" needed one correction.
#include "engine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <limits>
#include <stdexcept>

namespace nova {

const char* engine_msg_name(EngineMsg m) {
    switch (m) {
        case EngineMsg::kStateChanged: return "StateChanged";
        case EngineMsg::kRowsDrawn: return "RowsDrawn";
        case EngineMsg::kStats: return "Stats";
        case EngineMsg::kBatchProgress: return "BatchProgress";
        case EngineMsg::kBatchDone: return "BatchDone";
        case EngineMsg::kBatchFailed: return "BatchFailed";
        case EngineMsg::kSaved: return "Saved";
        case EngineMsg::kSaveFailed: return "SaveFailed";
    }
    return "?";
}

// --- §8.5 item 5, as pure functions ---------------------------------------

std::string sanitize_label(const std::string& label) {
    static const std::string kBad = "\\/:*?\"<>|";
    std::string out;
    bool pending_dash = false;
    for (const unsigned char c : label) {
        // Control characters are not "whitespace" to isspace() in every
        // locale, and a newline in a filename is worse than a space.
        const bool bad = c < 0x20 || c == 0x7f || std::isspace(c) ||
                         kBad.find(static_cast<char>(c)) != std::string::npos;
        if (bad) {
            pending_dash = !out.empty();
            continue;
        }
        if (pending_dash) {
            out.push_back('-');
            pending_dash = false;
        }
        out.push_back(static_cast<char>(c));
        if (out.size() >= 32) break;
    }
    // A run at the end leaves `pending_dash` set and is simply dropped:
    // trimming is what §8.5 asks for, and a trailing '-' is not a name.
    return out;
}

std::string image_filename(const std::string& utc_stamp,
                           const std::string& label) {
    const std::string clean = sanitize_label(label);
    if (clean.empty()) return utc_stamp + ".png";
    return utc_stamp + "-" + clean + ".png";
}

namespace {

std::string fmt(const char* f, double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, f, v);
    return buf;
}
std::string fmti(int v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d", v);
    return buf;
}

const char* dead_sector_name(DeadSector d) {
    switch (d) {
        case DeadSector::kBlackPulse: return "black pulse";
        case DeadSector::kWhiteOnly: return "white only";
    }
    return "?";
}

}  // namespace

std::vector<PngText> decode_qa(const DecodeResult& r, const std::string& label,
                               bool phase_operator, bool sync_operator) {
    std::vector<PngText> t;
    t.push_back(PngText{"Software", "Nova (WEFAX decoder)"});
    if (!label.empty()) t.push_back(PngText{"Title", label});
    t.push_back(PngText{"Nova:IOC", fmti(r.ioc)});
    t.push_back(PngText{"Nova:LPM", fmti(r.lpm)});
    t.push_back(PngText{"Nova:LinePeriod", fmt("%.6f s", r.line_period_s)});
    t.push_back(PngText{"Nova:Clock", fmt("%+.1f ppm", r.clock_ppm)});
    t.push_back(PngText{"Nova:Lines", fmti(r.lines)});
    t.push_back(PngText{"Nova:LockedLines", fmti(r.locked_lines)});
    t.push_back(PngText{"Nova:DeadSector", dead_sector_name(r.dead_sector)});
    t.push_back(PngText{"Nova:PerLineSync", r.per_line_sync ? "yes" : "no"});
    // The straightness of the dead-sector edge in the finished picture:
    // the first thing an operator sees, so the first QA number worth
    // carrying [core/fax.hpp on place_rms_px].
    t.push_back(PngText{"Nova:PlacementRMS", fmt("%.2f px", r.place_rms_px)});
    t.push_back(PngText{"Nova:Seams", fmti(r.seams)});
    t.push_back(PngText{"Nova:Phasing", r.phasing_found ? "found" : "none"});
    t.push_back(PngText{"Nova:Anchor", r.anchor_from_hint
                                    ? "operator hint, refined"
                                    : (r.anchor_from_phasing
                                           ? "phasing interval"
                                           : (r.per_line_sync ? "image lines"
                                                              : "unanchored"))});
    // §8.5 item 3: a re-render must say the values were the OPERATOR's, or
    // the file claims a provenance its pixels no longer have. Which cuts
    // both ways, and §7.1 is why: the operator SUPPLYING a value is not the
    // same as the decode USING it, and for SYNC the two usually differ —
    // the fit outranks a typed ppm wherever it has a baseline. So the
    // supplied flags decide whether there was an operator in the loop at
    // all, and the result's own provenance decides what the pixels owe to
    // them.
    t.push_back(PngText{
        "Nova:Phase", !phase_operator ? "measured"
                                      : (r.anchor_from_hint
                                             ? "operator hint, refined"
                                             : "operator (not used)")});
    t.push_back(PngText{
        "Nova:Sync", !sync_operator ? "measured"
                                    : (r.clock_from_fallback
                                           ? "operator (no fit baseline)"
                                           : "measured (operator outranked)")});
    t.push_back(PngText{"Nova:Standards",
                        "WMO-No. 386 Vol. I Part III 5; ISO 9876:2015 4.2"});
    return t;
}

namespace {

std::string system_utc_stamp() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y%m%dT%H%M%SZ", &tm);
    return buf;
}

std::size_t default_ring_capacity(int capture_rate) {
    // 4 s [docs/05 §2.1]: orders of magnitude more than a callback period,
    // so an overrun means thread 2 is wedged, not that the buffer is tight.
    return static_cast<std::size_t>(4 * (capture_rate > 0 ? capture_rate
                                                          : 48000));
}

}  // namespace

// ---------------------------------------------------------------------------

LiveEngine::LiveEngine(int capture_rate, const EngineOptions& opt)
    : capture_rate_(capture_rate),
      opt_(opt),
      ring_(opt.ring_capacity ? opt.ring_capacity
                              : default_ring_capacity(capture_rate)),
      resamp_(capture_rate, opt.internal_rate),
      demod_(opt.internal_rate, opt.demod_center, opt.demod_deviation),
      session_(opt.internal_rate, opt.session) {
    if (!opt_.utc_now) opt_.utc_now = system_utc_stamp;
    session_.set_decode_callback(
        [this](std::shared_ptr<const std::vector<float>> snap, long long start,
               const DecodeOptions& d) {
            begin_batch(std::move(snap), start, d);
        });
}

LiveEngine::~LiveEngine() { shutdown(); }

// --- thread 4 -------------------------------------------------------------

void LiveEngine::start_capture() {
    std::lock_guard<std::mutex> g(cmd_mu_);
    Cmd c;
    c.kind = CmdKind::kStart;
    cmds_.push_back(std::move(c));
}
void LiveEngine::stop_capture() {
    std::lock_guard<std::mutex> g(cmd_mu_);
    Cmd c;
    c.kind = CmdKind::kStop;
    cmds_.push_back(std::move(c));
}
void LiveEngine::force_start(int ioc, double lpm) {
    std::lock_guard<std::mutex> g(cmd_mu_);
    Cmd c;
    c.kind = CmdKind::kForce;
    c.ioc = ioc;
    c.a = lpm;
    cmds_.push_back(std::move(c));
}
void LiveEngine::set_phase(double frac) {
    std::lock_guard<std::mutex> g(cmd_mu_);
    Cmd c;
    c.kind = CmdKind::kPhase;
    c.a = frac;
    cmds_.push_back(std::move(c));
}
void LiveEngine::set_sync(double ppm) {
    std::lock_guard<std::mutex> g(cmd_mu_);
    Cmd c;
    c.kind = CmdKind::kSync;
    c.a = ppm;
    cmds_.push_back(std::move(c));
}
void LiveEngine::redecode(const Correction& c) {
    // Raised HERE, on the operator's thread, rather than when thread 2 gets
    // to it: the shell has to grey the transport and start the progress bar
    // on the click, not one poll later, and a request accepted is already a
    // re-render in progress as far as the operator is concerned. Thread 2
    // lowers it again if it turns out there is nothing to re-render.
    redecoding_.store(true, std::memory_order_release);
    std::lock_guard<std::mutex> g(cmd_mu_);
    Cmd cmd;
    cmd.kind = CmdKind::kRedecode;
    cmd.correction = c;
    cmds_.push_back(std::move(cmd));
}
void LiveEngine::set_label(const std::string& label) {
    std::lock_guard<std::mutex> g(cmd_mu_);
    Cmd c;
    c.kind = CmdKind::kLabel;
    c.text = label;
    cmds_.push_back(std::move(c));
}

void LiveEngine::set_image_folder(const std::string& folder) {
    std::lock_guard<std::mutex> g(cmd_mu_);
    Cmd c;
    c.kind = CmdKind::kFolder;
    c.text = folder;
    cmds_.push_back(std::move(c));
}

std::vector<EngineMessage> LiveEngine::drain() {
    std::vector<EngineMessage> out;
    std::lock_guard<std::mutex> g(out_mu_);
    out.swap(outbox_);
    return out;
}

bool LiveEngine::copy_image(Image* out) {
    if (!out) return false;
    std::lock_guard<std::mutex> g(img_mu_);
    if (display_.width <= 0 || display_.height <= 0) return false;
    *out = display_;
    return true;
}

RetainedVideo LiveEngine::retained_video() const {
    // Is the picture on the pane a DECODED one? `display_src_` names the
    // preview a provisional picture is being grown from, and collect_batch
    // clears it when a decode's image takes the pane. A preview has no
    // frozen stream behind it BY CONSTRUCTION — its stream is still being
    // received — and its corrections are §7's live, forward-only ones, not
    // §7.1's re-decode. Reporting the previous transmission's snapshot
    // while a new preview is on screen would offer to re-decode a chart the
    // operator is not looking at, which is the one mistake this whole
    // by-role rule exists to prevent.
    //
    // The two locks are taken in sequence and never nested, here or in
    // collect_batch, so no order between them exists to get wrong.
    RetainedVideo v;
    bool any_image = false;
    {
        std::lock_guard<std::mutex> g(img_mu_);
        any_image = display_.width > 0 && display_.height > 0;
        v.on_pane = any_image && display_src_ == nullptr;
    }
    {
        std::lock_guard<std::mutex> g(retain_mu_);
        v.decoded = displayed_snap_;
        v.decoded_start = displayed_start_;
        v.decoded_options = displayed_options_;
    }
    v.receiving_samples = receiving_samples_.load(std::memory_order_acquire);
    // §3: not offered and then found not to work. The three ways a
    // correction can be impossible are different facts about the operator's
    // situation, so they are different sentences.
    if (!v.can_correct()) {
        v.unavailable_reason =
            !any_image ? "no decoded image yet"
                       : (!v.on_pane
                              ? "receiving — this picture is provisional"
                              // §3's own words, for the case only the
                              // not-yet-built folder-open path can reach:
                              // an image whose snapshot has been released
                              // because the operator moved on from it.
                              : "raw stream no longer retained");
    }
    return v;
}

// --- lifecycle ------------------------------------------------------------

void LiveEngine::run() {
    if (running_.exchange(true)) return;
    t2_ = std::thread([this] { thread2(); });
}

void LiveEngine::shutdown() {
    if (!running_.load()) {
        if (t3_.joinable()) t3_.join();
        return;
    }
    stopping_.store(true, std::memory_order_release);
    if (t2_.joinable()) t2_.join();
    if (t3_.joinable()) t3_.join();
    running_.store(false);
}

// --- thread 2 -------------------------------------------------------------

void LiveEngine::post(EngineMessage m) {
    std::lock_guard<std::mutex> g(out_mu_);
    outbox_.push_back(std::move(m));
}

void LiveEngine::run_commands() {
    std::vector<Cmd> local;
    {
        std::lock_guard<std::mutex> g(cmd_mu_);
        local.swap(cmds_);
    }
    for (const Cmd& c : local) {
        switch (c.kind) {
            case CmdKind::kStart: emit(session_.start_capture()); break;
            case CmdKind::kStop: emit(session_.stop_capture()); break;
            case CmdKind::kForce:
                emit(session_.force_start(c.ioc, c.a));
                break;
            case CmdKind::kPhase:
                session_.set_phase(c.a);
                phase_operator_ = true;
                break;
            case CmdKind::kSync:
                session_.set_sync(c.a);
                sync_operator_ = true;
                break;
            case CmdKind::kLabel: label_ = c.text; break;
            case CmdKind::kFolder: opt_.image_folder = c.text; break;
            case CmdKind::kRedecode: start_redecode(c.correction); break;
            case CmdKind::kPromote: do_promote_background(); break;
        }
    }
}

// The operator's Apply or Auto [docs/05 §8.5 items 2-4]. Thread 2.
void LiveEngine::start_redecode(const Correction& c) {
    std::shared_ptr<const std::vector<float>> snap;
    DecodeOptions base;
    {
        std::lock_guard<std::mutex> g(retain_mu_);
        snap = displayed_snap_;
        base = displayed_options_;
    }
    // Nothing to re-render, or something already decoding. Both are
    // states the shell greys the buttons in [§3, §8.3 item 4], so
    // reaching here is a race, not a click on a dead control — and the
    // engine still declines rather than starting a second decode.
    if (!snap || batch_running_.load(std::memory_order_acquire)) {
        redecoding_.store(false, std::memory_order_release);
        return;
    }

    // The record of what produced the picture, with ONLY the operator's
    // two fields replaced. Everything else — the forced IOC, the hooks,
    // the segmentation — is what it was, because a correction corrects
    // two things and must not quietly re-decide the rest.
    DecodeOptions d = base;
    // "As measured" is the absence of a value, not a value: §7.1's own
    // sentinels, which is why Auto needs no third mode. -1 is "no hint"
    // because column 0 is a legal anchor; NaN is "no fallback" because
    // 0 ppm is a legal clock.
    d.phase_anchor_hint = c.phase_set ? c.phase_frac : -1.0;
    d.clock_ppm_fallback =
        c.sync_set ? c.sync_ppm : std::numeric_limits<double>::quiet_NaN();
    // §8.5 item 3: a re-render's PNG says the values were the operator's.
    // They say the operator SUPPLIED one; whether the decode USED it is
    // the result's own anchor_from_hint / clock_from_fallback, and under
    // §7.1 those two answers routinely differ for SYNC.
    phase_operator_ = c.phase_set;
    sync_operator_ = c.sync_set;

    batch_is_redecode_ = true;
    long long start = 0;
    {
        std::lock_guard<std::mutex> g(retain_mu_);
        start = displayed_start_;
    }
    begin_batch(snap, start, d);
}

// Grow `dst` from the preview `p`'s image, restarting it when the preview
// is a different one than the rows already there came from. Split out of
// `append_display_rows` when §8.2's background buffer gave it a second
// destination: the arithmetic is delicate enough that two copies of it
// would be two chances to get the prefix wrong.
//
// Caller holds `img_mu_`.
static void append_rows_locked(Image* dst, const StreamPreview** dst_src,
                               const StreamPreview* p, const Image& src) {
    if (p != *dst_src || dst->width != src.width) {
        *dst = Image{};
        dst->width = src.width;
        dst->height = 0;
        *dst_src = p;
    }
    if (src.height <= dst->height || src.width <= 0) return;
    const std::size_t w = static_cast<std::size_t>(src.width);
    dst->px.insert(dst->px.end(),
                   src.px.begin() + static_cast<std::ptrdiff_t>(
                                        w * static_cast<std::size_t>(
                                                dst->height)),
                   src.px.begin() + static_cast<std::ptrdiff_t>(
                                        w * static_cast<std::size_t>(
                                                src.height)));
    dst->height = src.height;
}

void LiveEngine::append_display_rows() {
    const StreamPreview* p = session_.preview();
    if (!p) return;
    const Image& src = p->image();
    std::lock_guard<std::mutex> g(img_mu_);
    // §8.2: the edit holds the pane. The rows go to the background buffer
    // only when taking the pane would DISPLACE something — a decoded
    // picture, which is the only kind an edit can be correcting. A held
    // pane showing a provisional preview displaces nothing (that preview is
    // this same transmission), so the hold does not apply and the rows land
    // where they always did. Getting this test wrong in the permissive
    // direction freezes the live view for an operator who has merely typed
    // in a box, which is the opposite of the protection asked for.
    const bool displaces_decoded =
        display_src_ == nullptr && display_.width > 0 && display_.height > 0;
    // **A buffer that exists keeps the pane held, whatever the edit is
    // doing** [§8.2, Sara, session 30]. The pane changes hands ONLY when
    // the operator clicks the indicator — that is the whole of the decision,
    // and without this second term it would not survive the edit ending:
    // the operator presses Apply, `pane_held_` goes false, and the very next
    // batch of rows takes the pane that the indicator is still offering. The
    // buffered picture would vanish into the pane by itself, which is the
    // interruption §8.2 exists to prevent, arriving one Apply late.
    const bool buffering = background_.width > 0 && background_.height > 0;
    if ((pane_held_.load(std::memory_order_acquire) || buffering) &&
        displaces_decoded) {
        append_rows_locked(&background_, &background_src_, p, src);
        // A preview that is still growing is not a parked decode, and if a
        // parked decode were somehow overwritten by one, the indicator
        // would call a whole chart a partial one.
        background_complete_ = false;
        return;
    }
    append_rows_locked(&display_, &display_src_, p, src);
}

void LiveEngine::set_pane_held(bool held) {
    pane_held_.store(held, std::memory_order_release);
}

LiveEngine::Background LiveEngine::background() const {
    std::lock_guard<std::mutex> g(img_mu_);
    Background b;
    b.active = background_.width > 0 && background_.height > 0;
    b.rows = background_.height;
    b.width = background_.width;
    b.complete = b.active && background_complete_;
    return b;
}

bool LiveEngine::copy_background_image(Image* out) {
    if (!out) return false;
    std::lock_guard<std::mutex> g(img_mu_);
    if (background_.width <= 0 || background_.height <= 0) return false;
    *out = background_;
    return true;
}

void LiveEngine::promote_background() {
    std::lock_guard<std::mutex> g(cmd_mu_);
    Cmd c;
    c.kind = CmdKind::kPromote;
    cmds_.push_back(std::move(c));
}

void LiveEngine::do_promote_background() {
    // The two locks are taken in sequence and never nested, here as in
    // `collect_batch` and `retained_video`, so no order between them exists
    // to get wrong.
    bool had_parked_snapshot = false;
    int promoted_rows = 0;
    {
        std::lock_guard<std::mutex> g(img_mu_);
        if (background_.width <= 0 || background_.height <= 0) return;
        promoted_rows = background_.height;
        display_ = std::move(background_);
        // A parked decode is a finished picture and has no preview growing
        // it; a buffered preview is still being grown by `background_src_`,
        // and the pane must go on appending to it from where it is.
        display_src_ = background_complete_ ? nullptr : background_src_;
        had_parked_snapshot = background_complete_;
        background_ = Image{};
        background_src_ = nullptr;
        background_complete_ = false;
    }
    {
        std::lock_guard<std::mutex> g(retain_mu_);
        if (had_parked_snapshot) {
            // The picture coming forward is the one on disk, and this is
            // where its stream becomes the correctable one — the same
            // handover `collect_batch` performs, deferred to the moment the
            // operator actually gets the picture.
            displayed_snap_ = std::move(parked_snap_);
            displayed_start_ = parked_start_;
            displayed_options_ = std::move(parked_options_);
            // ...and so does the file it was written to, or the operator's
            // next Apply overwrites the chart they were correcting with the
            // one that just arrived [§8.5 item 2].
            saved_path_ = parked_saved_path_;
        } else {
            // A provisional preview has no frozen stream by construction,
            // so what comes forward is correctable by nothing. Releasing
            // the outgoing snapshot here is §3's "the operator moved on":
            // holding it would offer a re-decode of a chart no longer on
            // the pane, which is the one mistake the by-role rule exists to
            // prevent.
            displayed_snap_.reset();
            displayed_options_ = DecodeOptions{};
            displayed_start_ = 0;
            // A provisional picture is on no disk yet, so there is no file
            // for a re-render to overwrite. Leaving the old path here would
            // aim the next Apply at the previous transmission's PNG.
            saved_path_.clear();
        }
        parked_snap_.reset();
        parked_saved_path_.clear();
    }

    // **A promotion has to ANNOUNCE itself** [session 31]. Everything else
    // thread 4 learns, it learns from this queue, and until now this was the
    // one change to the pane that arrived silently. The click was therefore
    // structurally unable to work, not merely racy: `cb_recv` queues the
    // promotion and then calls `apply_state` on the spot, so the shell asked
    // the question before thread 2 could have answered it — and with nothing
    // to make it ask again, the pane kept the old chart and the indicator
    // stayed lit. On a transmission that has FINISHED there is no later
    // batch of rows to hide the defect behind, so it lasts forever.
    //
    // kRowsDrawn rather than a message of its own, because that is what
    // literally happened: the picture on the pane is a different picture and
    // has a different number of rows, which is also what the line counter
    // should now be naming. `m.rows` stays empty — no NEW rows were drawn,
    // and the shell re-copies the whole image on any row message.
    EngineMessage m;
    m.kind = EngineMsg::kRowsDrawn;
    m.rows_total = promoted_rows;
    post(std::move(m));
}

void LiveEngine::emit(const SessionOutput& out) {
    if (!out.rows.empty()) {
        append_display_rows();
        EngineMessage m;
        m.kind = EngineMsg::kRowsDrawn;
        m.rows = out.rows;
        const StreamPreview* p = session_.preview();
        m.rows_total = p ? p->rows() : 0;
        m.locked_rows = p ? p->locked_rows() : 0;
        m.reacquired_rows = p ? p->reacquired_rows() : 0;
        post(std::move(m));
    }
    for (const SessionState s : out.entered) {
        EngineMessage m;
        m.kind = EngineMsg::kStateChanged;
        m.state = s;
        m.ioc = session_.ioc();
        post(std::move(m));
    }
}

void LiveEngine::begin_batch(std::shared_ptr<const std::vector<float>> snap,
                             long long start, const DecodeOptions& dopt) {
    // Called on thread 2, from inside session_.push. One decode at a time
    // [§8.3 item 4]; a previous thread is joined before another starts.
    if (t3_.joinable()) t3_.join();
    {
        std::lock_guard<std::mutex> g(batch_mu_);
        batch_ready_ = false;
    }
    // §3: hold the snapshot this decode is running over. It is not the
    // DISPLAYED one yet — the image on screen is still the previous
    // transmission's, and so is the stream a correction would re-decode —
    // it becomes displayed when its image takes the pane in collect_batch.
    {
        std::lock_guard<std::mutex> g(retain_mu_);
        pending_snap_ = snap;
        pending_start_ = start;
        pending_options_ = dopt;
    }
    batch_running_.store(true, std::memory_order_release);
    const int fs = opt_.internal_rate;
    t3_ = std::thread([this, snap, dopt, fs] {
        DecodeOptions d = dopt;
        d.hooks.progress = [this](const char* stage, double fraction) {
            std::lock_guard<std::mutex> g(prog_mu_);
            prog_stage_ = stage ? stage : "";
            prog_frac_ = fraction;
            prog_new_ = true;
        };
        try {
            DecodeResult r = decode_fax(*snap, fs, d);
            std::lock_guard<std::mutex> g(batch_mu_);
            batch_result_ = std::move(r);
            batch_ok_ = true;
            batch_ready_ = true;
        } catch (const DecodeError& e) {
            std::lock_guard<std::mutex> g(batch_mu_);
            batch_error_ = e.kind();
            batch_ok_ = false;
            batch_ready_ = true;
        } catch (const std::exception&) {
            std::lock_guard<std::mutex> g(batch_mu_);
            batch_error_ = DecodeErrorKind::kEmptyInput;
            batch_ok_ = false;
            batch_ready_ = true;
        }
        batch_running_.store(false, std::memory_order_release);
    });
}

std::string LiveEngine::save_image(const DecodeResult& r, bool overwrite) {
    // §8.5 item 2: a re-render overwrites the file the transmission was
    // saved to. One transmission, one file — hunting for the right PHASE
    // across five Applies writes the same path five times, which costs
    // nothing, where a new file per Apply would turn one chart into five
    // near-identical ones the operator then has to weed. Nova never
    // renames [item 5], so a label typed after the automatic save reaches
    // the text chunks here and the name stays where it was.
    std::string path;
    if (overwrite && !saved_path_.empty()) {
        path = saved_path_;
    } else {
        const std::string name = image_filename(opt_.utc_now(), label_);
        std::string dir = opt_.image_folder;
        if (!dir.empty() && dir.back() == '/') dir.pop_back();
        path = dir.empty() ? name : dir + "/" + name;
    }
    write_png(path, r.img, decode_qa(r, label_, phase_operator_,
                                     sync_operator_));
    return path;
}

// --- collect_batch's stages -------------------------------------------------
// collect_batch is four nearly-sequential concerns — read the batch result,
// decide whether to park it, save the image, hand it to the pane. They are
// file-local statics, as `append_rows_locked` is, so the member state
// arrives as parameters. Thread 2 throughout.

// Thread 3's one-slot result inbox [§8.3 item 4]. False when nothing has
// reported; on a failed decode the result slot is left untouched and `err`
// carries the kind instead.
static bool take_batch_result(std::mutex& mu, bool& ready, bool& ok_flag,
                              DecodeResult& result, DecodeErrorKind& error,
                              bool* ok, DecodeResult* res,
                              DecodeErrorKind* err) {
    std::lock_guard<std::mutex> g(mu);
    if (!ready) return false;
    *ok = ok_flag;
    if (*ok) *res = std::move(result);
    else *err = error;
    ready = false;
    return true;
}

// A decode that produced no image puts no image on the pane, so §3's
// displayed snapshot does not change hands — the operator is still looking
// at the previous chart and may still correct it. The failed transmission's
// stream has nothing to be the stream OF, and is released.
static EngineMessage batch_failed_message(
    std::mutex& retain_mu,
    std::shared_ptr<const std::vector<float>>& pending_snap,
    DecodeErrorKind err) {
    {
        std::lock_guard<std::mutex> g(retain_mu);
        pending_snap.reset();
    }
    EngineMessage m;
    m.kind = EngineMsg::kBatchFailed;
    m.error = err;
    return m;
}

// A RE-DECODE is excluded and must be: it is the operator's own Apply,
// whose entire purpose is to replace the displayed picture. Parking it
// would hide the result of the button they just pressed behind an
// indicator offering to show them their own correction.
static bool batch_should_park(bool was_redecode, std::mutex& img_mu,
                              const Image& background,
                              const std::atomic<bool>& pane_held,
                              const StreamPreview* display_src,
                              const Image& display) {
    if (was_redecode) return false;
    std::lock_guard<std::mutex> g(img_mu);
    // The same two-term test `append_display_rows` uses, and for the
    // same reason: a buffer that exists keeps the pane held until the
    // operator promotes it [§8.2, Sara, session 30]. A transmission
    // whose rows were diverted must not hand its finished picture to
    // the pane merely because the edit ended while it was decoding.
    const bool buffering = background.width > 0 && background.height > 0;
    return (pane_held.load(std::memory_order_acquire) || buffering) &&
           display_src == nullptr && display.width > 0 &&
           display.height > 0;
}

// §3's retained snapshot changes hands, and it does so BEFORE the image
// it belongs to reaches the pane. The order is the load-bearing part,
// exactly as it is for write-then-SAVED above: thread 4 may look
// between these two blocks. In THIS order it sees the new stream with
// the old picture still on the pane, which reads as "provisional" and
// offers nothing — harmless. In the other order it would see the new
// picture on the pane backed by the PREVIOUS transmission's stream, and
// a correction taken in that instant would re-decode the wrong
// transmission (or, on the first decode of a session, no stream at all,
// which is the one state §3 reserves for a snapshot genuinely
// released).
//
// The outgoing image's stream is released HERE and nowhere else: an
// operator correcting the chart that just arrived keeps its raw stream
// for as long as it is the one on screen, however long they take, and
// the next transmission merely ARRIVING does not take it away — that
// arrival is a `pending_` snapshot until its own picture replaces this
// one.
// §8.2, ROADMAP M4 item 6: a transmission that FINISHES behind an edit
// does not take the pane either, and this is the case with teeth in it.
// The paragraph above is the invariant — the outgoing stream is released
// when the incoming picture replaces it — so a decode that completes
// mid-edit would release the very stream the operator's Apply re-decodes
// from, and the correction surface would die under their hands with no
// visible cause. Deferring the whole handover is what prevents it: image
// and snapshot are parked TOGETHER and promoted TOGETHER, so
// `retained_video()` describes the picture actually on the pane at every
// instant, which is the invariant the correction surface is built on.
static void park_finished_batch(
    std::mutex& retain_mu,
    std::shared_ptr<const std::vector<float>>& pending_snap,
    long long pending_start, DecodeOptions& pending_options,
    std::shared_ptr<const std::vector<float>>& parked_snap,
    long long& parked_start, DecodeOptions& parked_options,
    std::mutex& img_mu, Image& background,
    const StreamPreview*& background_src, bool* background_complete,
    const Image& img) {
    {
        std::lock_guard<std::mutex> g(retain_mu);
        parked_snap = std::move(pending_snap);
        parked_start = pending_start;
        parked_options = std::move(pending_options);
        pending_snap.reset();
    }
    {
        std::lock_guard<std::mutex> g(img_mu);
        background = img;
        // A finished decode is grown by no preview: promotion must not
        // append the next transmission's rows onto this chart.
        background_src = nullptr;
        *background_complete = true;
    }
}

static void display_finished_batch(
    std::mutex& retain_mu,
    std::shared_ptr<const std::vector<float>>& pending_snap,
    long long pending_start, DecodeOptions& pending_options,
    std::shared_ptr<const std::vector<float>>& displayed_snap,
    long long& displayed_start, DecodeOptions& displayed_options,
    std::mutex& img_mu, Image& display, const StreamPreview*& display_src,
    const Image& img) {
    {
        std::lock_guard<std::mutex> g(retain_mu);
        displayed_snap = std::move(pending_snap);
        displayed_start = pending_start;
        displayed_options = std::move(pending_options);
        pending_snap.reset();
    }
    // The saved image takes the pane from the provisional one. This is
    // the announced swap of §8.2 — the two pictures differ, and the one
    // on screen after a decode is the one on disk.
    {
        std::lock_guard<std::mutex> g(img_mu);
        display = img;
        display_src = nullptr;
    }
}

// The decode's own message first, then the save's: the shell reads them in
// the order they happened.
static std::vector<EngineMessage> batch_result_messages(
    const DecodeResult& res, const std::string& saved_path,
    const std::string& save_error) {
    std::vector<EngineMessage> out;
    EngineMessage done;
    done.kind = EngineMsg::kBatchDone;
    done.result = std::make_shared<const DecodeResult>(res);
    out.push_back(std::move(done));
    if (!save_error.empty()) {
        EngineMessage m;
        m.kind = EngineMsg::kSaveFailed;
        m.detail = save_error;
        out.push_back(std::move(m));
    } else if (!saved_path.empty()) {
        EngineMessage m;
        m.kind = EngineMsg::kSaved;
        m.path = saved_path;
        out.push_back(std::move(m));
    }
    return out;
}

// collect_batch's `redecoding_` guard; see the comment where it is
// declared in that function.
struct LowerWhenSaved {
    std::atomic<bool>* flag;
    ~LowerWhenSaved() { flag->store(false, std::memory_order_release); }
};

void LiveEngine::collect_batch() {
    bool ok = false;
    DecodeResult res;
    DecodeErrorKind err = DecodeErrorKind::kEmptyInput;
    if (!take_batch_result(batch_mu_, batch_ready_, batch_ok_, batch_result_,
                           batch_error_, &ok, &res, &err))
        return;

    // Whose decode was that — a transmission's own, or the operator asking
    // for the same one again [§8.5 items 2-4]?
    //
    // **`redecoding_` is lowered when the FILE has been written, not when
    // the decode finished**, and the difference is not cosmetic: the flag
    // is what greys Apply and holds the transport still, so lowering it
    // early would re-arm the button while the PNG was still being written
    // and let a second Apply in on top of it — the "one decode at a time"
    // rule [§8.3 item 4] broken in the one place it is easiest not to
    // notice. It is the same lesson as §8.5 item 1's write-then-SAVED: the
    // operator-visible signal comes after the file, never before it.
    const bool was_redecode = batch_is_redecode_;
    batch_is_redecode_ = false;
    // Lowered on every exit below, and only after the save.
    const LowerWhenSaved lower_when_saved{&redecoding_};

    if (!ok) {
        post(batch_failed_message(retain_mu_, pending_snap_, err));
        emit(session_.batch_failed(err));
        return;
    }

    // §8.5 item 1: the decode completing is what writes the file, before
    // any editing is possible. The order is write-then-batch_done, so the
    // status line cannot read SAVED over a file that is not there.
    // §8.2 / ROADMAP M4 item 6: is this decode arriving BEHIND an edit? The
    // question has to be asked here, above the save, because the file this
    // transmission is written to is one of the three things that must be
    // parked with it rather than handed over [see promote_background]. The
    // picture is still saved either way — §8.2's "nothing is lost by
    // waiting" is about the pane, never about the disk.
    const bool park = batch_should_park(was_redecode, img_mu_, background_,
                                        pane_held_, display_src_, display_);
    std::string saved_path;
    std::string save_error;
    if (!opt_.image_folder.empty()) {
        try {
            saved_path = save_image(res, was_redecode);
            if (park) {
                parked_saved_path_ = saved_path;
            } else {
                saved_path_ = saved_path;
            }
        } catch (const std::exception& e) {
            save_error = e.what();
        }
    }

    // `park` was decided above the save, because the saved path is parked
    // with the picture rather than handed over.
    if (park)
        park_finished_batch(retain_mu_, pending_snap_, pending_start_,
                            pending_options_, parked_snap_, parked_start_,
                            parked_options_, img_mu_, background_,
                            background_src_, &background_complete_, res.img);
    else
        display_finished_batch(retain_mu_, pending_snap_, pending_start_,
                               pending_options_, displayed_snap_,
                               displayed_start_, displayed_options_, img_mu_,
                               display_, display_src_, res.img);

    for (EngineMessage& m :
         batch_result_messages(res, saved_path, save_error))
        post(std::move(m));

    emit(session_.batch_done(res));
    // Every transmission starts from measured-or-blank [§8.5 item 6]:
    // there is no memory between transmissions, so the provenance flags
    // reset with the transmission that earned them.
    phase_operator_ = false;
    sync_operator_ = false;
}

void LiveEngine::thread2() {
    std::vector<float> block(4096);
    double peak = 0.0;
    long long since_stats = 0;
    const long long stats_every = capture_rate_ / 10;  // ~10 Hz

    for (;;) {
        run_commands();
        collect_batch();

        {
            std::lock_guard<std::mutex> g(prog_mu_);
            if (prog_new_) {
                EngineMessage m;
                m.kind = EngineMsg::kBatchProgress;
                m.stage = prog_stage_;
                m.fraction = prog_frac_;
                post(std::move(m));
                prog_new_ = false;
            }
        }

        const std::size_t got = ring_.read(block.data(), block.size());
        if (got == 0) {
            const bool quitting = stopping_.load(std::memory_order_acquire);
            if (quitting && !batch_running_.load(std::memory_order_acquire))
                break;
            std::this_thread::sleep_for(
                std::chrono::milliseconds(opt_.poll_ms));
            continue;
        }

        for (std::size_t i = 0; i < got; i++)
            peak = std::max(peak, static_cast<double>(std::fabs(block[i])));
        since_stats += static_cast<long long>(got);

        const std::vector<float> mono = resamp_.push(block.data(), got);
        if (!mono.empty()) {
            const std::vector<float> video = demod_.push(mono);
            if (!video.empty()) emit(session_.push(video));
        }
        // §3's FIRST retained snapshot, published for thread 4: thread 2
        // owns the session and thread 4 may not ask it anything, so the
        // one number the cost of the store can be read from crosses here.
        receiving_samples_.store(session_.retained_samples(),
                                 std::memory_order_release);

        if (since_stats >= stats_every) {
            EngineMessage m;
            m.kind = EngineMsg::kStats;
            m.level_dbfs = peak > 0.0 ? 20.0 * std::log10(peak) : -120.0;
            m.overruns = ring_.overruns();
            m.consumed_sec = session_.consumed_sec();
            post(std::move(m));
            since_stats = 0;
            peak = 0.0;
        }
    }

    // End of stream. The resampler's tail first — those are real samples
    // the batch call would have produced [stream.hpp on flush()] — then
    // the session's own flush, which ends any transmission in progress
    // exactly as an operator Stop would.
    const std::vector<float> tail = resamp_.flush();
    if (!tail.empty()) {
        const std::vector<float> video = demod_.push(tail);
        if (!video.empty()) emit(session_.push(video));
    }
    emit(session_.flush());

    // A flush that ended a transmission started a decode; the caller asked
    // to shut down, and dropping the last chart on the floor is exactly
    // the failure §8.3 item 6 removed.
    while (batch_running_.load(std::memory_order_acquire))
        std::this_thread::sleep_for(std::chrono::milliseconds(opt_.poll_ms));
    collect_batch();
}

}  // namespace nova
