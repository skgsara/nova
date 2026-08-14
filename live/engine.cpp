// engine.cpp — see engine.hpp for the thread map and for why §2.3's
// "SPSC" needed one correction.
#include "engine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
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
        [this](std::shared_ptr<const std::vector<float>> snap, long long,
               const DecodeOptions& d) { begin_batch(std::move(snap), d); });
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
        }
    }
}

void LiveEngine::append_display_rows() {
    const StreamPreview* p = session_.preview();
    if (!p) return;
    const Image& src = p->image();
    std::lock_guard<std::mutex> g(img_mu_);
    if (p != display_src_ || display_.width != src.width) {
        display_ = Image{};
        display_.width = src.width;
        display_.height = 0;
        display_src_ = p;
    }
    if (src.height <= display_.height || src.width <= 0) return;
    const std::size_t w = static_cast<std::size_t>(src.width);
    display_.px.insert(display_.px.end(),
                       src.px.begin() + static_cast<std::ptrdiff_t>(
                                            w * static_cast<std::size_t>(
                                                    display_.height)),
                       src.px.begin() + static_cast<std::ptrdiff_t>(
                                            w * static_cast<std::size_t>(
                                                    src.height)));
    display_.height = src.height;
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
                             const DecodeOptions& dopt) {
    // Called on thread 2, from inside session_.push. One decode at a time
    // [§8.3 item 4]; a previous thread is joined before another starts.
    if (t3_.joinable()) t3_.join();
    {
        std::lock_guard<std::mutex> g(batch_mu_);
        batch_ready_ = false;
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

std::string LiveEngine::save_image(const DecodeResult& r) {
    const std::string name = image_filename(opt_.utc_now(), label_);
    std::string dir = opt_.image_folder;
    if (!dir.empty() && dir.back() == '/') dir.pop_back();
    const std::string path = dir.empty() ? name : dir + "/" + name;
    write_png(path, r.img, decode_qa(r, label_, phase_operator_,
                                     sync_operator_));
    return path;
}

void LiveEngine::collect_batch() {
    bool ready = false, ok = false;
    DecodeResult res;
    DecodeErrorKind err = DecodeErrorKind::kEmptyInput;
    {
        std::lock_guard<std::mutex> g(batch_mu_);
        if (batch_ready_) {
            ready = true;
            ok = batch_ok_;
            if (ok) res = std::move(batch_result_);
            else err = batch_error_;
            batch_ready_ = false;
        }
    }
    if (!ready) return;

    if (!ok) {
        EngineMessage m;
        m.kind = EngineMsg::kBatchFailed;
        m.error = err;
        post(std::move(m));
        emit(session_.batch_failed(err));
        return;
    }

    // §8.5 item 1: the decode completing is what writes the file, before
    // any editing is possible. The order is write-then-batch_done, so the
    // status line cannot read SAVED over a file that is not there.
    std::string saved_path;
    std::string save_error;
    if (!opt_.image_folder.empty()) {
        try {
            saved_path = save_image(res);
        } catch (const std::exception& e) {
            save_error = e.what();
        }
    }

    // The saved image takes the pane from the provisional one. This is the
    // announced swap of §8.2 — the two pictures differ, and the one on
    // screen after a decode is the one on disk.
    {
        std::lock_guard<std::mutex> g(img_mu_);
        display_ = res.img;
        display_src_ = nullptr;
    }

    EngineMessage done;
    done.kind = EngineMsg::kBatchDone;
    done.result = std::make_shared<const DecodeResult>(res);
    post(std::move(done));

    if (!save_error.empty()) {
        EngineMessage m;
        m.kind = EngineMsg::kSaveFailed;
        m.detail = save_error;
        post(std::move(m));
    } else if (!saved_path.empty()) {
        EngineMessage m;
        m.kind = EngineMsg::kSaved;
        m.path = saved_path;
        post(std::move(m));
    }

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
