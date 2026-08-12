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
    // to count as phasing. Library-measured separation (session 6): true
    // phasing scores 0.88-0.97, while the dark satellite/photo imagery
    // that fits a "5% white, 95% black" template by accident scores
    // 0.48-0.62. This sits in that gap.
    double min_score = 0.75;
    // Phasing is ~30 s = 60 lines at 120 lpm, 30 at 60 lpm. Accepting far
    // fewer than that lets a run of dark picture lines qualify.
    int min_lines = 12;
    // ...and phasing is ~30 s [WMO §5.2.3], so it is not 8 minutes long
    // either. Before this cap, himawari and FAXSignal reported 439 s and
    // 481 s of "phasing" — the duration alone falsifies that, independent
    // of any score. Generous (2x spec) because a station may run long.
    double max_sec = 60.0;
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
    double nonlinearity = 0.0;
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
