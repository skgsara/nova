#include "preview.hpp"

#include <algorithm>
#include <cmath>

namespace nova {
namespace {

// Video released from the front of the buffer in chunks this big, so a
// 20-minute transmission does not memmove its history once per row. The
// renderer keeps two line periods behind the next row, which is far more
// than the template and its search window can reach (0.12 of a line);
// the slack is there so the trim point can be computed from the next row
// alone, without knowing what the period will do next.
constexpr long long kTrimChunk = 65536;
constexpr double kKeepLines = 2.0;

// Fewest lines an end-of-stream acquisition will accept. A consistency
// profile over four lines quantizes to 0.25, which is coarse enough that
// the `kFaxPulseConsistency` cut at 0.40 becomes a two-way decision
// between 0.25 and 0.50 — usable for "is there a pulse at all", not much
// more. Below four it is not a profile, it is a guess, and a short
// transmission is better drawn on the seeded clock than on a wrong anchor.
constexpr int kMinAcqLines = 4;

}  // namespace

StreamPreview::StreamPreview(int fs, const PreviewOptions& opt)
    : fs_(fs), opt_(opt) {
    width_ = (opt_.ioc == 288) ? 905 : 1810;
    nominal_ = fs_ * 60.0 / opt_.lpm;
    period_ = nominal_ * (1.0 + opt_.clock_ppm * 1e-6);
    img_.width = width_;
    img_.height = 0;
    row_.assign(static_cast<std::size_t>(width_), 0);
}

void StreamPreview::set_phase_anchor(double frac) {
    // The operator reports where the dead sector IS, as a fraction of the
    // drawn width [docs/05 §7]. The renderer draws the dead sector at
    // column 0, so a report of `frac` says the row start belongs
    // `frac * period` further into the signal.
    frac -= std::floor(frac);
    next_start_ += frac * period_;
    pending_phase_mark_ = true;
    dlog(opt_.hooks, LogTopic::kInfo,
         "preview: PHASE %.4f -> next row starts %.1f samples later",
         frac, frac * period_);
}

void StreamPreview::set_clock_ppm(double ppm) {
    opt_.clock_ppm = ppm;
    period_ = nominal_ * (1.0 + ppm * 1e-6);
    // The EMA's baseline was measured against the old period; a trim is
    // the operator saying the old one was wrong, so it does not get to
    // carry a span across the change.
    have_prev_lock_ = false;
    pending_sync_mark_ = true;
    dlog(opt_.hooks, LogTopic::kInfo,
         "preview: SYNC %+.1f ppm -> period %.3f samples", ppm, period_);
}

namespace {

// The same across-line consistency profiles the batch path builds
// [core/fax.cpp `stage_dead_sector`]: the dead sector is the one part
// of the line that looks the same on EVERY line [WMO §5.1.3.3], and
// picture content does not — a chart border is dark on many lines,
// never on all of them. Over `acq_lines` lines rather than 120, for
// the reason in the header: the profile is stacked on the SEEDED
// period, and a long stack smears the pulse by the rate error.
//
// No phasing skip here. The batch path steps over ~30 s of phasing
// first because the onset gate lands on it; this renderer is handed
// the picture by the live state machine [docs/05 §4], so the lines it
// is looking at are already image lines.
void build_consistency_profiles(const std::vector<float>& buf, int lines,
                                double period, int plen,
                                const DecodeHooks& hooks,
                                std::vector<double>* dark_frac,
                                std::vector<double>* white_frac) {
    dark_frac->assign(plen, 0.0);
    white_frac->assign(plen, 0.0);
    for (int l = 0; l < lines; l++) {
        throw_if_cancelled(hooks, "preview-acquire");
        const double base = l * period;
        for (int i = 0; i < plen; i++) {
            const float x = fax_lerp_at(buf, base + i);
            if (x < kFaxDarkLevel) (*dark_frac)[i] += 1.0;
            if (x > kFaxWhiteLevel) (*white_frac)[i] += 1.0;
        }
    }
    for (int i = 0; i < plen; i++) {
        (*dark_frac)[i] /= lines;
        (*white_frac)[i] /= lines;
    }
}

// Both anchors score a SHAPE, not a level, for the reason session 4
// measured: a full-disk satellite image carries black space at both
// line margins, dark on 100% of lines over hundreds of samples, so
// "darkest window" lands anywhere inside that band. What identifies
// the pulse is that black is followed immediately by white
// [WMO §5.1.3.3]; what identifies a white-only dead sector is the
// RISING EDGE into consistent whiteness [WMO §5.2.3.4 puts the
// phasing reference at exactly that edge]. Also reports the two window
// means the caller turns into the dead-sector consistency.
void score_dead_sector(const std::vector<double>& dark_frac,
                       const std::vector<double>& white_frac, int plen,
                       double* pulse_shape, int* pulse_at,
                       double* pulse_cons, double* white_shape,
                       int* white_at, double* white_cons) {
    // Wrapped window mean; the dead sector straddles the line boundary,
    // so every window here wraps.
    auto win_mean = [&](const std::vector<double>& f, int at, int win) {
        double s = 0.0;
        for (int j = 0; j < win; j++) s += f[((at + j) % plen + plen) % plen];
        return s / win;
    };
    const int pulse_w = std::max(2, static_cast<int>(kFaxPulseFrac * plen));
    const int dead_w = std::max(2, static_cast<int>(kFaxDeadFrac * plen));
    *pulse_shape = -1.0;
    *white_shape = -1.0;
    *pulse_at = 0;
    *white_at = 0;
    for (int i = 0; i < plen; i++) {
        const double s = std::min(win_mean(dark_frac, i, pulse_w),
                                  win_mean(white_frac, i + pulse_w, pulse_w));
        if (s > *pulse_shape) {
            *pulse_shape = s;
            *pulse_at = i;
        }
        const double e = win_mean(white_frac, i, dead_w) -
                         win_mean(white_frac, i - dead_w, dead_w);
        if (e > *white_shape) {
            *white_shape = e;
            *white_at = i;
        }
    }
    *pulse_cons = win_mean(dark_frac, *pulse_at, pulse_w);
    *white_cons = win_mean(white_frac, *white_at, dead_w);
}

// Re-acquisition, exactly as `stage_track` does it: after a run of
// misses, sweep half a line at a coarse step instead of the ±narrow
// window. Without it a dropout that moves the sync outside the
// narrow window is permanent — the tracker coasts to the end of the
// transmission, and every row after the dropout is torn. Measured
// on `himawari-kiwisdr-dropout-120s` before this was added: 140
// locked rows of 238, against 232 of 240 for the batch path.
double search_row_sync(const std::vector<float>& buf, double next_start,
                       double base, double span, double pulse, bool reacq,
                       double* score) {
    return fax_best_sync(buf, next_start - span - base,
                         next_start + span - base, pulse, score,
                         reacq ? kFaxReacqStep : 1.0) +
           base;
}

void render_row_pixels(const std::vector<float>& buf, double base,
                       double start, double period, int width,
                       std::vector<uint8_t>* row, Image* img) {
    for (int j = 0; j < width; j++) {
        const double pos = start + period * j / width - base;
        (*row)[j] = static_cast<uint8_t>(
            std::lround(fax_lerp_at(buf, pos) * 255.0f));
    }
    img->px.insert(img->px.end(), row->begin(), row->end());
    img->height++;
}

}  // namespace

// --- acquisition: the one backward look, taken before anything is drawn ---
bool StreamPreview::try_acquire(int lines) {
    const int plen = static_cast<int>(period_);
    if (plen < 8 || lines < kMinAcqLines) return false;
    const long long need =
        static_cast<long long>(std::ceil(lines * period_)) + plen + 2;
    if (total_in_ < need) return false;

    std::vector<double> dark_frac, white_frac;
    build_consistency_profiles(buf_, lines, period_, plen, opt_.hooks,
                               &dark_frac, &white_frac);

    double pulse_shape = 0.0, white_shape = 0.0;
    double pulse_cons = 0.0, white_cons = 0.0;
    int pulse_at = 0, white_at = 0;
    score_dead_sector(dark_frac, white_frac, plen, &pulse_shape, &pulse_at,
                      &pulse_cons, &white_shape, &white_at, &white_cons);

    has_pulse_ = pulse_shape >= kFaxPulseConsistency;
    dead_ = has_pulse_ ? DeadSector::kBlackPulse : DeadSector::kWhiteOnly;
    dead_cons_ = has_pulse_ ? pulse_cons : white_cons;
    next_start_ = has_pulse_ ? pulse_at : white_at;
    // A white-only station has no per-line phase in its image lines, so
    // the rising edge found above is the picture's own white margin as
    // often as it is the dead sector. Where the caller watched a phasing
    // interval, that is the anchor [WMO §5.2.3.4] — the same substitution
    // `decode_fax` makes, and for the same reason.
    bool from_phasing = false;
    if (!has_pulse_ && opt_.phase_anchor >= 0.0) {
        const double f = opt_.phase_anchor - std::floor(opt_.phase_anchor);
        next_start_ = f * period_;
        from_phasing = true;
    }

    // Row 0 must not read behind the start of the stream: `fax_lerp_at`
    // clamps rather than failing, so an under-run would be a silently
    // wrong row instead of an error. Give up whole lines until the
    // template's reach fits, which costs at most one.
    while (next_start_ - search_span() - template_margin() < 0.0)
        next_start_ += period_;

    acquired_ = true;
    anchor_from_phasing_ = from_phasing;
    dlog(opt_.hooks, LogTopic::kInfo,
         "preview: acquired over %d lines - %s (shape %.2f/%.2f, "
         "consistency %.2f) anchor %.1f from %s, period %.2f",
         lines, has_pulse_ ? "black-pulse" : "white-only",
         pulse_shape, white_shape, dead_cons_, next_start_,
         from_phasing ? "phasing" : "image lines", period_);
    return true;
}

bool StreamPreview::can_draw_row() const {
    if (opt_.max_lines > 0 && row_index_ >= opt_.max_lines) return false;
    // Everything this row can read: the sync search either way, the
    // template's reach past that, and the row's own pixels. A lock at the
    // far edge of the search window shifts the pixels with it, which is
    // why the span appears on BOTH sides rather than only on the left.
    const double lo = next_start_ - search_span() - template_margin();
    const double hi =
        next_start_ + search_span() + period_ + template_margin();
    if (lo < static_cast<double>(buf_start_)) return false;
    return total_in_ >= static_cast<long long>(std::ceil(hi)) + 2;
}

void StreamPreview::draw_row(std::vector<PreviewRow>& out, bool final_row) {
    const double p = period_;
    const double pulse = kFaxPulseFrac * p;
    const double base = static_cast<double>(buf_start_);

    double start = next_start_;
    double score = 0.0;
    bool locked = false;
    const bool reacq = reacq_due();

    // Per-line dead-sector relock — the weatherfax_pi/KiwiSDR approach the
    // batch path already uses, and the one piece of it that works forward
    // [docs/05 §6]. Only a black pulse gives per-line phase; a white-only
    // dead sector carries none [core/fax.hpp `fax_best_sync`], so those
    // stations coast on the working clock and report zero locks, which is
    // the truth rather than a failure.
    //
    // `final_row` is the last row of the transmission, drawn by `flush`:
    // its pixels are all present but the template's margin past them is
    // not, so it is drawn on the clock rather than searched for. One row
    // at the very bottom of the page.
    if (opt_.autolock && has_pulse_ && !final_row) {
        const double cand =
            search_row_sync(buf_, next_start_, base, search_span(), pulse,
                            reacq, &score);
        if (score >= kFaxPulseLock) {
            start = cand;
            locked = true;
            if (reacq) reacquired_rows_++;
        }
    }

    render_row_pixels(buf_, base, start, p, width_, &row_, &img_);

    PreviewRow r;
    r.index = row_index_;
    r.start_sample = start;
    r.period = p;
    r.locked = locked;
    r.sync_score = locked ? score : 0.0;
    r.phase_mark = pending_phase_mark_;
    r.sync_mark = pending_sync_mark_;
    pending_phase_mark_ = false;
    pending_sync_mark_ = false;
    out.push_back(r);

    if (locked) {
        locked_rows_++;
        miss_ = 0;
        // The forward rate estimate: a short EMA over the last N locked
        // lines [docs/05 §6]. Measured across the gap between this lock
        // and the previous one, so coasted rows in between cost accuracy
        // but not correctness. NOT a fit over a long baseline — that is
        // the estimator §6 forbids, because it would move rows already
        // drawn.
        if (have_prev_lock_) {
            const int span = row_index_ - prev_lock_row_;
            const double meas = (start - prev_lock_pos_) / span;
            if (std::fabs(meas / period_ - 1.0) < opt_.ema_max_jump) {
                const double alpha = 2.0 / (opt_.ema_lines + 1.0);
                period_ = (1.0 - alpha) * period_ + alpha * meas;
            }
        }
        prev_lock_pos_ = start;
        prev_lock_row_ = row_index_;
        have_prev_lock_ = true;
        next_start_ = start + period_;
    } else {
        miss_++;
        next_start_ += period_;
    }
    row_index_++;
}

void StreamPreview::release_behind() {
    // Page cap reached [docs/05 §4]: no further row will read anything, so
    // hold nothing. Without this the buffer would grow for as long as the
    // caller kept pushing at a renderer that has stopped drawing.
    if (opt_.max_lines > 0 && row_index_ >= opt_.max_lines) {
        buf_.clear();
        buf_start_ = total_in_;
        return;
    }
    const long long keep = static_cast<long long>(
        std::floor(next_start_ - kKeepLines * period_));
    if (keep <= buf_start_ + kTrimChunk) return;
    buf_.erase(buf_.begin(),
               buf_.begin() + static_cast<std::ptrdiff_t>(keep - buf_start_));
    buf_start_ = keep;
}

std::vector<PreviewRow> StreamPreview::push(const float* video,
                                            std::size_t n) {
    std::vector<PreviewRow> out;
    buf_.insert(buf_.end(), video, video + n);
    total_in_ += static_cast<long long>(n);

    if (!acquired_ && !try_acquire(opt_.acq_lines)) return out;

    // No progress fraction is reported here, and that is deliberate: a
    // live transmission has no known length, and docs/04 Finding 3 says
    // the operator is shown the state of the protocol, never a percentage.
    // The rows themselves are the progress.
    while (can_draw_row()) {
        throw_if_cancelled(opt_.hooks, "preview");
        draw_row(out, false);
        // Released per ROW, never per push, and the difference is not
        // housekeeping. `fax_best_sync` walks its search window by
        // accumulation (`p += step`), so where the window sits in the
        // buffer decides the last bits of every probe position: at
        // absolute magnitude the double grid is coarser than near zero,
        // and the two sequences drift apart by ~1e-11 over a line. If the
        // trim schedule depended on when `push` happened to be called,
        // that residue would make the picture depend on the audio
        // callback's block size — measured, before this moved inside the
        // loop: identical at 1, 7, 333, 1000 and 2000 samples, different
        // at 12345 and 65536, on five fixtures. Trimming once per row
        // makes `buf_start_` a function of the rows drawn and nothing
        // else, which is what makes the picture one too.
        release_behind();
    }
    release_behind();
    return out;
}

std::vector<PreviewRow> StreamPreview::flush() {
    std::vector<PreviewRow> out;

    // A transmission too short for a full acquisition window still gets an
    // anchor, taken over whatever whole lines arrived. Nothing about this
    // is a second chance at a decision already made: it runs only if the
    // renderer never acquired, so no row has been drawn.
    if (!acquired_) {
        const int have = static_cast<int>(total_in_ / period_) - 1;
        for (int n = std::min(opt_.acq_lines, have); n >= kMinAcqLines; n--)
            if (try_acquire(n)) break;
        if (!acquired_) {
            dlog(opt_.hooks, LogTopic::kInfo,
                 "preview: flush with nothing to draw (%.1f lines seen, "
                 "%d needed)",
                 total_in_ / period_, kMinAcqLines);
            return out;
        }
    }

    while (can_draw_row()) draw_row(out, false);

    // One last row: every pixel of it is present, only the sync template's
    // margin past it is missing. Drawn on the clock rather than searched
    // for, which is what `decode_fax` does at the end of a file too — the
    // alternative is a picture one row shorter than the signal.
    if ((opt_.max_lines == 0 || row_index_ < opt_.max_lines) &&
        next_start_ >= static_cast<double>(buf_start_) &&
        total_in_ >= static_cast<long long>(
                         std::ceil(next_start_ + period_)) + 2)
        draw_row(out, true);

    return out;
}

}  // namespace nova
