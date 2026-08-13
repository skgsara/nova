// tone_stream.cpp — see tone_stream.hpp for the contract and for the
// argument that the two paths partition frames into the same runs.
#include "tone_stream.hpp"

#include <algorithm>

namespace nova {

StreamToneDetector::StreamToneDetector(int fs, const ToneOptions& opt,
                                       const DecodeHooks& hooks)
    : fs_(fs), opt_(opt), hooks_(hooks) {
    // The identical expressions detect_tones forms, casts included, so
    // the two paths cut the stream into frames at the same samples.
    win_ = static_cast<int>(static_cast<size_t>(opt_.win_sec * fs));
    hop_ = static_cast<int>(static_cast<size_t>(opt_.hop_sec * fs));
    // detect_tones forms this only after returning on `hop == 0`, so the
    // guard is free there and has to be explicit here: ToneOptions comes
    // from the caller, and a zero hop would divide by zero before push()
    // ever got the chance to refuse.
    max_gap_ = opt_.hop_sec > 0.0
                   ? static_cast<long long>(
                         static_cast<size_t>(opt_.max_gap_sec / opt_.hop_sec))
                   : 0;

    cands_[0].kind = ToneKind::kStartIOC576;
    cands_[0].nominal = 300.0;  // [WMO §5.2.2]
    cands_[0].min_sec = opt_.min_start_sec;
    cands_[1].kind = ToneKind::kStartIOC288;
    cands_[1].nominal = 675.0;  // [WMO §5.2.2]
    cands_[1].min_sec = opt_.min_start_sec;
    cands_[2].kind = ToneKind::kStop;
    cands_[2].nominal = 450.0;  // [WMO §5.2.5]
    cands_[2].min_sec = opt_.min_stop_sec;

    if (win_ >= 8 && hop_ > 0) window_.resize(static_cast<size_t>(win_));
}

std::vector<ToneEvent> StreamToneDetector::push(const float* video,
                                                std::size_t n) {
    std::vector<ToneEvent> out;
    // The same guard detect_tones applies before it starts: a window
    // shorter than 8 samples, or no hop, detects nothing at all.
    if (win_ < 8 || hop_ <= 0) return out;

    buf_.insert(buf_.end(), video, video + n);
    total_in_ += static_cast<long long>(n);

    while (frame_start_ + win_ <= total_in_) {
        throw_if_cancelled(hooks_, "tones");
        const std::size_t off = static_cast<std::size_t>(frame_start_ -
                                                         buf_start_);
        std::copy(buf_.begin() + static_cast<std::ptrdiff_t>(off),
                  buf_.begin() + static_cast<std::ptrdiff_t>(off) + win_,
                  window_.begin());
        feed_frame(out);

        frame_index_++;
        frame_start_ += hop_;

        // Nothing before the next frame can ever be read again.
        if (frame_start_ > buf_start_) {
            buf_.erase(buf_.begin(),
                       buf_.begin() + static_cast<std::ptrdiff_t>(
                                          frame_start_ - buf_start_));
            buf_start_ = frame_start_;
        }
    }
    return out;
}

void StreamToneDetector::feed_frame(std::vector<ToneEvent>& out) {
    for (Cand& c : cands_) {
        double freq = c.nominal;
        // The same call the batch path makes, over the same samples: the
        // window is all this function reads, so the purity it returns
        // here is bit-identical to the batch's for this frame.
        const double p = tone_purity_band(window_, 0,
                                          static_cast<std::size_t>(win_), fs_,
                                          c.nominal, opt_.tol, &freq);
        update(c, p, freq, out);
    }
}

void StreamToneDetector::update(Cand& c, double purity, double freq,
                                std::vector<ToneEvent>& out) {
    const bool hot = purity >= opt_.purity;

    if (!c.open) {
        if (!hot) return;
        c.open = true;
        c.emitted = false;
        c.first = frame_index_;
        c.last_hot = frame_index_;
        c.cold = 0;
        c.fr.clear();
        c.pu.clear();
    } else if (hot) {
        c.last_hot = frame_index_;
        c.cold = 0;
    } else {
        // A cold frame cannot change the verdict on the run so far: the
        // three tests are computed over [first .. last_hot], which this
        // frame is not in. It can only end the run [core/tones.cpp: the
        // gap rule exists because HF signals fade mid-tone].
        if (++c.cold > max_gap_) close(c);
        return;
    }

    c.fr.push_back(freq);
    c.pu.push_back(purity);
    if (c.emitted) return;  // one event per run

    const double t0 = static_cast<double>(c.first * hop_) / fs_;
    const double t1 = static_cast<double>(c.last_hot * hop_ + win_) / fs_;
    const double dur = t1 - t0;
    const double hot_frac = static_cast<double>(c.fr.size()) /
                            static_cast<double>(c.last_hot - c.first + 1);
    const double sp = tone_spread_10_90(c.fr) / c.nominal;

    if (dur < c.min_sec || sp > opt_.max_spread ||
        hot_frac < opt_.min_hot_frac)
        return;

    ToneEvent e;
    e.kind = c.kind;
    e.t_start = t0;
    // All three of these are "so far" values, and the run may well grow
    // after this. See the header: this is the announced difference from
    // the batch path, not a defect in it.
    e.t_end = t1;
    e.freq_hz = tone_median(c.fr);
    e.purity = tone_median(c.pu);
    c.emitted = true;

    dlog(hooks_, LogTopic::kInfo,
         "dbg: live tone %s qualified %.2f-%.2fs (%.2fs) f=%.1f "
         "purity=%.3f spread=%.4f hot=%.2f",
         tone_name(c.kind), t0, t1, dur, e.freq_hz, e.purity, sp, hot_frac);
    out.push_back(e);
}

}  // namespace nova
