// spectrum.hpp — the M4.5 tuning strip's numbers [ROADMAP M4.5; decided
// 2026-08-13, Sara, session 17; built session 36].
//
// Why this is a live/ module and not GUI code. The waterfall is the one
// part of the window that serves TUNING rather than decoding, but it is
// still a thing that can be wrong about a signal — a column that names the
// wrong frequency is a tuning aid that tunes you to the wrong place. So it
// lives where every other "can be wrong about a signal" piece lives: no
// FLTK, no RtAudio, no real clock, drivable by a test with a generated
// tone instead of a sound card [docs/05 §1]. The FLTK widget draws what
// this produces and decides nothing.
//
// What it is fed. RAW CAPTURE AUDIO, before the resampler and before the
// demodulator — thread 2's `block` in live/engine.cpp, the same samples
// the peak meter reads. That is the whole point: the operator is tuning a
// receiver, so the strip must show the audio the receiver is actually
// delivering, not the internal 4-times-oversampled video Nova derives
// from it. A spectrum of the video would show the tuning error already
// removed.
//
// The band and its markers [Sara, session 36]: 800-3000 Hz, with the two
// WMO tones at 1500 Hz black and 2300 Hz white [WMO §5.2.1, docs/01].
// Tight enough that a mistune reads as a visible offset from the marker
// lines, wide enough that a signal several hundred Hz off is still on the
// display with a direction to correct in — 1200-2600 would have put a
// badly-tuned station off the edge with no clue which way to turn.
//
// The mapping is exposed (`column_hz` / `hz_column`) for the same reason
// live/ruler.hpp exposes its own: the marker lines the GUI draws and the
// columns this fills in must agree about where 1500 Hz is, and two
// implementations of that arithmetic are two chances to disagree. The
// ruler earned that lesson in session 20; this is the same shape.
#pragma once

#include <cstddef>
#include <vector>

namespace nova {

struct SpectrumOptions {
    // The band, in Hz. Sara's call, session 36.
    double f_lo = 800.0;
    double f_hi = 3000.0;

    // 4096 at 48 kHz is an 85 ms window and 11.7 Hz bins. Resolution is
    // what a tuning aid is for — 100 Hz of mistune has to be obvious —
    // and 85 ms is well inside one 120 lpm picture row, so the strip
    // still moves at capture speed. Must be a power of two.
    int fft_size = 4096;

    // Display columns across the band. Fixed rather than taken from the
    // widget's pixel width on purpose: the analyser must not reallocate
    // because somebody dragged the window, and the waterfall history
    // must survive a resize. The widget maps these columns to pixels.
    int columns = 256;

    // Waterfall rows kept. At one column per hop_sec this is the visible
    // history: 96 rows x 50 ms = 4.8 s, which is long enough to see a
    // fade arrive and short enough to still be the present.
    int history = 96;

    // One column per hop. 50 ms matches the GUI's drain tick [docs/05
    // §2.3], so the strip advances one row per repaint and never queues
    // work thread 4 cannot collect.
    double hop_sec = 0.05;

    // Display range. 0 dB is a full-scale sine at a bin centre — see
    // `push` on the normalisation — so the top of the scale is genuinely
    // "as loud as this sound card can report" and the floor is the level
    // below which the diagnosis is "no signal" rather than a picture.
    double floor_db = -80.0;
    double top_db = 0.0;
};

// The band mapping, as free functions over the options alone.
//
// Free rather than members because the mapping is pure geometry — it does
// not depend on the sample rate, on any audio having arrived, or on an
// analyser existing at all. The GUI needs to know which column 1500 Hz is
// in in order to draw a marker line, and it needs to know it before a sound
// card has been opened; making it construct a whole analyser (and its FFT
// tables) to ask a question about arithmetic would be the wrong shape, and
// re-deriving the arithmetic in the widget is the mistake live/ruler.hpp
// exists to prevent.
//
// `column_hz` is the frequency at the CENTRE of the column. `hz_column`
// returns -1 outside the band rather than clamping: a marker for a tone
// that is not on the display must be absent, not parked against the edge
// where it would read as a tone at the edge.
double spectrum_column_hz(const SpectrumOptions& opt, int col);
int spectrum_hz_column(const SpectrumOptions& opt, double hz);

// Rolling spectrum over raw capture audio, plus the waterfall history the
// widget scrolls. Single-threaded and allocation-free after construction
// once `push` is running: it is a thread-2 object, like the streaming
// front end, and must never be called from the RtAudio callback.
class SpectrumAnalyzer {
public:
    SpectrumAnalyzer(int sample_rate, const SpectrumOptions& opt =
                                          SpectrumOptions());

    // Feed raw capture samples. Returns the number of new columns
    // produced — zero is the ordinary case, because one 1024-frame
    // RtAudio block is less than one hop.
    int push(const float* x, std::size_t n);

    // The newest column, `columns()` wide, each value 0..1 after the
    // dB scale of `floor_db`..`top_db`. Empty until the first full
    // window has been seen: an empty strip means "nothing measured yet",
    // which is not the same as silence and must not be drawn as it.
    const std::vector<float>& latest() const { return latest_; }
    bool has_data() const { return rows_ > 0; }

    // The waterfall, newest row first. `row(0)` is `latest()`. Returns
    // nullptr past `rows_filled()`, so a widget drawing a fresh strip
    // leaves the unfilled rows blank instead of drawing a ring's stale
    // contents as if they were history.
    const float* row(int r) const;
    int rows_filled() const { return rows_; }

    int columns() const { return columns_; }
    int history() const { return history_; }
    int sample_rate() const { return fs_; }

    // The mapping, both ways — the free functions above applied to this
    // analyser's own options, so a caller holding one does not have to
    // carry the options separately. These are inverse over the band and
    // the suite pins that, but the load-bearing check is the other one:
    // a real sine generated at f must peak in `hz_column(f)`, which ties
    // the arithmetic to the DSP rather than to itself.
    double column_hz(int col) const { return spectrum_column_hz(opt_, col); }
    int hz_column(double hz) const { return spectrum_hz_column(opt_, hz); }

    // The options this analyser was built with, so a caller that has one
    // can ask the free functions the same questions.
    const SpectrumOptions& options() const { return opt_; }

    // There is deliberately no reset(). One was written, and it had no
    // caller: a device change destroys the engine and builds a new one
    // [gui/nova-gui.cpp cb_device], so the analyser's life is the audio
    // stream's life and a stale device's history cannot outlive it. A
    // principled method defended only by a test that calls it is the
    // survivor shape session 24 recorded — being principled is not
    // evidence — so it was deleted rather than kept for symmetry.

private:
    void transform_window();

    int fs_;
    SpectrumOptions opt_;
    int columns_;
    int history_;
    int fft_size_;
    std::size_t hop_;

    std::vector<float> fifo_;   // fft_size_ samples, most recent last
    std::size_t fill_ = 0;      // samples in fifo_ since the last hop
    std::size_t have_ = 0;      // total samples ever admitted, capped

    std::vector<double> window_;  // Hann
    double window_sum_ = 0.0;
    std::vector<double> re_, im_;
    std::vector<int> bitrev_;

    std::vector<float> latest_;
    std::vector<float> ring_;  // history_ rows of columns_, newest at head_
    int head_ = 0;
    int rows_ = 0;
};

}  // namespace nova
