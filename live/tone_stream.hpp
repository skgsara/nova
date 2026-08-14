// tone_stream.hpp — control-tone detection over a growing stream, so the
// live path reaches the same verdicts as `detect_tones` does, as they
// arrive [docs/05 §5, docs/04 answer 3].
//
// Why this exists. `detect_tones` scans a whole recording — ~9 s on the
// 61-minute JSC4, which AGENTS.md already registers as "unbudgeted for
// M4 live decode, where the scan wants to be incremental". The live
// session state machine cannot wait for the end of a transmission to be
// told that the transmission started.
//
// What is kept identical, and what cannot be. The existing detector is
// already frame-based — Hann window `win_sec`, hop `hop_sec`, a purity
// per frame [core/tones.hpp] — and only the *run assembly* is
// retrospective. So this class:
//
//   - computes each frame with the same `tone_purity_band` call over the
//     same absolute frame positions (0, hop, 2*hop, ...), which makes
//     the per-frame purity bit-identical rather than merely close: that
//     function reads nothing but the window handed to it;
//   - assembles runs incrementally, and **emits an event at the earliest
//     qualifying moment** rather than at the end of the run.
//
// The consequence, stated plainly because the screamer is written around
// it: `freq_hz` and `purity` are medians over the frames seen SO FAR,
// and `t_end` is where the run had reached when it qualified. For the
// same tone the batch detector reports medians over the whole run and a
// `t_end` at the run's end. Those three fields differ between the two
// paths by construction and comparing them would be a bug in the test,
// not in the code [docs/05 §5]. `kind` and `t_start` do NOT differ, and
// that is the claim `live_tones` pins.
//
// Why the two paths agree about which runs exist, which is the part
// worth being able to argue rather than only measure:
//
//   1. The partition of frames into runs depends on nothing but the
//      hot/cold pattern and `max_gap_sec`, and both paths see the same
//      frames in the same order. A run therefore begins and ends at the
//      same frame in both.
//   2. Batch evaluates a run once, over `[first .. last_hot]`. At the
//      moment the streaming detector receives that same `last_hot`
//      frame, its accumulated state IS that interval — same hot frames,
//      same freq/purity lists, same span. It applies the same three
//      tests to it. So every run the batch accepts is accepted here at
//      or before the moment of its last hot frame.
//
// So the streaming events are a superset of the batch events, and the
// only way they can differ is an EARLY emission: a run whose prefix
// passes the duration, spread and hot-fraction tests while the whole run
// would fail one of them. That is a real risk, not a hypothetical —
// `max_spread` is the test that rejects a run assembled out of noise,
// and a prefix of noise is exactly what has not wandered yet. Measured
// on the fixture library and on generated signals, it does not happen
// [tests/test_live_tones.cpp]; if it ever does, the honest fix is a
// confirmation delay, not a wider tolerance.
//
// Threading: a thread-2 object [docs/05 §2]. It allocates; never call it
// from the RtAudio callback.
#pragma once

#include "../core/hooks.hpp"
#include "../core/tones.hpp"

#include <cstddef>
#include <vector>

namespace nova {

class StreamToneDetector {
public:
    explicit StreamToneDetector(int fs, const ToneOptions& opt = ToneOptions(),
                                const DecodeHooks& hooks = DecodeHooks());

    // Feed demodulated VIDEO — the same domain `detect_tones` reads, not
    // audio [core/tones.hpp: the control signals are black/white
    // alternations in video, and session 3 got this wrong for a whole
    // session]. Returns the events that qualified during this call, in
    // the order they qualified, which is not necessarily the order of
    // their start times: two runs of different kinds can overlap, and
    // the one that started later can qualify first.
    std::vector<ToneEvent> push(const float* video, std::size_t n);
    std::vector<ToneEvent> push(const std::vector<float>& video) {
        return push(video.data(), video.size());
    }

    // Frames completed so far. The batch detector's frame count for the
    // same input is the same number.
    long long frames() const { return frame_index_; }
    // Video consumed, in seconds.
    double consumed_sec() const {
        return static_cast<double>(total_in_) / fs_;
    }

    int window_samples() const { return win_; }
    int hop_samples() const { return hop_; }

    // --- run-state queries, for the live session state machine -----------
    // [docs/05 §4]: the machine's START TONE and STOP TONE states leave
    // when the tone ENDS, which the one-event-per-run emission rule
    // deliberately never says. These report the run assembly's raw state
    // without touching it: whether a run on `k` is open right now, and
    // when the most recent run was last hot — which, once the run has
    // closed, is the tone's end. -1 when no run on `k` has ever opened.
    bool run_open(ToneKind k) const;
    double run_last_hot_sec(ToneKind k) const;

    // How far a consumer may read the stream without risking reading into
    // a stop tone that has not qualified yet — the question the live
    // preview's feed needs answered [docs/05 §4]. A stop run opens within
    // one hot frame of the tone's first classified frame, so a consumer
    // that stays `win + hop` behind the classification frontier never
    // crosses a tone start before the run for it exists; and once the run
    // IS open, the horizon drops to the run's first frame and holds there
    // until the run qualifies or dies. Both ends of this are absolute
    // stream positions, so a consumer fed to this horizon draws the same
    // rows whatever the block size.
    long long safe_horizon_samples() const;

private:
    // One candidate tone, and the run currently open on it.
    struct Cand {
        ToneKind kind;
        double nominal;
        double min_sec;
        // The open run, or `open == false`.
        bool open = false;
        bool emitted = false;   // one event per run, at the earliest moment
        bool seen = false;      // a run on this candidate has opened at least once
        long long first = 0;    // frame index the run started at
        long long last_hot = 0;
        long long cold = 0;     // consecutive cold frames since last_hot
        std::vector<double> fr;  // measured frequency, hot frames only
        std::vector<double> pu;  // measured purity, hot frames only
    };

    void feed_frame(std::vector<ToneEvent>& out);
    void update(Cand& c, double purity, double freq,
                std::vector<ToneEvent>& out);
    void close(Cand& c) {
        c.open = false;
        c.emitted = false;
        c.cold = 0;
        c.fr.clear();
        c.pu.clear();
    }

    int fs_;
    ToneOptions opt_;
    DecodeHooks hooks_;
    int win_ = 0;
    int hop_ = 0;
    long long max_gap_ = 0;  // in frames, as the batch path computes it

    Cand cands_[3];

    std::vector<float> buf_;    // video from buf_start_ to total_in_
    std::vector<float> window_;  // scratch, one frame wide
    long long buf_start_ = 0;
    long long frame_start_ = 0;  // absolute sample index of the next frame
    long long frame_index_ = 0;
    long long total_in_ = 0;
};

}  // namespace nova
