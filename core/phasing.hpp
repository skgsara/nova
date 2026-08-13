// phasing.hpp — phasing-signal detection and line-start recovery.
//
// The phasing signal is ~30 s of alternating black/white at exactly the
// line rate, either symmetric (50/50) or asymmetric (5% white / 95% black)
// [WMO §5.2.3.1-.2] — a detector must accept both. Its value to Nova is
// §5.2.3.4: the LEADING EDGE OF WHITE is aligned with entry into the dead
// sector, so the phasing stage hands over the line-start phase directly,
// measured on a signal that contains no picture content to be fooled by.
//
// This is the one place a white-only station can get per-line phase at
// all. VMW, NMC and GYA send a plain white dead sector, which carries no
// per-line sync (docs/01 §5, measured in session 4); their anchor, if it
// exists anywhere, is here.
//
// Method follows weatherfax_pi / KiwiSDR [docs/00]: fit the wedge over a
// fraction of the line, take the median position over the phasing lines,
// and reject the result if the 10-90% spread is too wide.
#pragma once
#include <vector>

namespace nova {

struct PhasingOptions {
    // Per-line contrast (white run mean minus rest mean, 0..1) for a line
    // to SEED a run. Library-measured separation (session 6): true phasing
    // scores 0.88-0.97, while the dark satellite/photo imagery that fits a
    // "5% white, 95% black" template by accident scores 0.48-0.62. This
    // sits in that gap.
    //
    // Session 10: that separation holds for a strong signal and fails for a
    // faded one. GYA 2300Z's real phasing lines score 0.34-0.88 — BELOW the
    // false-content band — because fading cuts the contrast the score
    // measures while leaving the edge exactly where it was. So this is a
    // seeding threshold and no longer a membership test: a run grows on
    // POSITION agreement (`max_gap`), which fading does not touch.
    double min_score = 0.75;
    // How many lines a run may go without a line that scores above
    // `min_score` before it is over. Phasing is a continuous ~30 s of the
    // transmission [WMO §5.2.3], so a break in it is HF fading, not a
    // boundary — and a decoder that ends the run at the first faded line
    // reports GYA 2300Z's 20 s interval as ten fragments of one to six
    // lines, which is what happened until session 10.
    //
    // Counted from the last STRONG line, not from the last member. Weak
    // lines that agree in position still join the run, but they do not
    // renew its lease: on a white-only station the image dead sector is
    // white at the phasing position [WMO §5.2.3.4], so every later picture
    // line agrees and a run that reset on agreement never ends — measured
    // on the generated white-only signal, 30 phasing lines grew into a
    // 230-line run that ran off the end of the picture.
    //
    // fldigi's `decode_phasing` is the prior art for the shape of this:
    // it abandons the phasing state after 5 consecutive lines fail its own
    // test [docs/00, session 10]. The number here is measured rather than
    // borrowed — inside GYA 2300Z's real interval the widest run of
    // consecutive non-strong lines is 6, and the generated picture's
    // phasing-like rows are 49 apart.
    int max_gap = 8;
    // Phasing is ~30 s = 60 lines at 120 lpm, 30 at 60 lpm. Accepting far
    // fewer than that lets a run of dark picture lines qualify.
    int min_lines = 12;
    // ...and phasing is ~30 s [WMO §5.2.3], so it is not 8 minutes long
    // either. Before this cap, himawari and FAXSignal reported 439 s and
    // 481 s of "phasing" — the duration alone falsifies that, independent
    // of any score. Generous (2x spec) because a station may run long.
    double max_sec = 60.0;
    // The transmission this decoder is going to draw, in seconds, when the
    // caller knows it (from the control tones). Inside a known window the
    // interval wanted is the LAST qualifying one, because that is the one
    // the picture begins after; with no window it is the FIRST, because a
    // later run may belong to a transmission this decode is not drawing.
    //
    // Neither rule is right on its own and the library says so both ways.
    // `jmh sample` holds two whole transmissions, of 59 and 60 phasing
    // lines; taking the longest, or the last with no window, picks the
    // SECOND one and the head crop collapses from 62 lines to 3. FAXSignal
    // holds two openings before ONE picture — start tone 0-7 s, phasing
    // 7-22 s, a second 300 Hz burst 22-30.5 s, phasing again 32-64.5 s —
    // and taking the first draws 68 lines of the second opening into the
    // chart. The window separates them: `jmh sample`'s second transmission
    // is past its first stop tone, FAXSignal's second opening is not.
    double t_lo = 0.0, t_hi = 0.0;  // t_hi <= t_lo: no window known
    // Reject if the 10-90% spread of per-line positions exceeds this
    // fraction of the line. KiwiSDR uses 1/6, but it only ever runs this
    // inside a phasing stage its tone state machine already identified;
    // Nova scans a whole recording blind, which is the harder problem, and
    // 1/6 (667 samples) let everything through. Measured: true phasing
    // spreads 14-73 samples, false runs 288-635.
    double max_spread_frac = 1.0 / 24.0;
};

struct PhasingResult {
    bool found = false;
    double t_start = 0.0;
    double t_end = 0.0;
    int lines = 0;          // phasing lines that agreed
    // Sample offset, within the line, of the leading edge of white — i.e.
    // of dead-sector entry [WMO §5.2.3.4]. This is the line-start anchor.
    // Measured on a grid of whole samples counted from sample 0, which is
    // the caller's grid only when the period is a whole number; prefer
    // `anchor` for anything that has to line up with a decoder.
    double line_start = 0.0;
    // The same anchor as an ABSOLUTE sample position, at the middle of the
    // phasing run. `line_start` cannot be handed to a decoder directly: it
    // is a residue modulo the truncated integer period, so a caller with a
    // fractional period (every real recording — the clock is never exactly
    // nominal) walks off it by (period - trunc(period)) per line, tens of
    // samples across a 60-line interval. This is measured against the
    // fractional `period` passed in and is what decode_fax consumes.
    double anchor = 0.0;
    double spread = 0.0;    // 10-90% of per-line positions, in samples
    // The same spread with the best straight line removed: what is left
    // after any constant clock error is accounted for. `spread` is
    // dominated by that clock (0.66 samples/line at -90 ppm), so it says
    // little about whether the timebase is LINEAR; this does.
    //
    // Session 10: this number alone says only that the edge does not lie on
    // a straight line. It does not say WHY, and there are three reasons, of
    // which only one is a stepping timebase. The two fields below are what
    // separate them, and nothing should convict on `nonlinearity` alone.
    double nonlinearity = 0.0;
    // Median line-to-line change in that residual, over adjacent lines: the
    // interval's own measurement noise. An inserted sample moves the edge
    // and it STAYS moved, so a stepping recording's residual is a staircase
    // whose neighbouring lines agree (JSC2/3/4: 1.1-1.8 samples against a
    // nonlinearity of 17.8-22.0). A faded one redraws the edge every line,
    // so its residual is as rough as it is wide (GYA 2300Z: 14.2 against
    // 14.6). Noise is not a moved edge, and this is what tells them apart.
    double roughness = 0.0;
    // Number of PERSISTENT moves in the smoothed residual — the same thing
    // the image-domain half of the timebase test counts, in the domain that
    // works on a station with no sync pulse. One skip is not a rate
    // (session 9): JMH KiwiSDR Himawari's phasing interval straddles a
    // single ~95-sample jump with 60 lines of textbook-linear edge either
    // side of it, and a spread-only test convicts it on evidence that a
    // 1922-line tracked residual calls linear.
    int steps = 0;
    bool asymmetric = true; // 5/95 [WMO §5.2.3.2] vs symmetric 50/50
    double score = 0.0;     // median per-line contrast of the run
};

// `period` is the line length in samples (nominal is good enough: a
// clock error of even 500 ppm walks under 30 samples across a whole
// phasing interval).
PhasingResult detect_phasing(const std::vector<float>& video, int fs,
                             double period,
                             const PhasingOptions& opt = PhasingOptions());

}  // namespace nova
