// fax.hpp — WEFAX line sync and image assembly.
//
// Approach [docs/01 §5]:
//   - signal onset found by an odd-harmonic line-comb scan (recordings
//     may open with leader tones or SDR stall-fill, not signal);
//   - line period measured from the strongest comb window
//     (autocorrelation), giving the sound-card clock error for free;
//   - coarse phase from the one part of the line that looks the same on
//     EVERY line — the dead sector [WMO §5.1.3.3] — located by across-line
//     consistency, which also tells us which of the two dead-sector styles
//     the station sends (black pulse, or plain white);
//   - per-line relock on the dead sector with a fractional accumulator
//     (weatherfax_pi/KiwiSDR approach), so a wrong or drifting clock does
//     not slant the picture;
//   - locked_lines counts real sync-template matches only; if no comb
//     is found the decoder fails loudly instead of drawing noise.
#pragma once
#include "image.hpp"
#include <vector>

namespace nova {

struct DecodeOptions {
    int lpm = 0;                 // 60/90/120; 0 = measure from signal
    // 576/288 manual; 0 = select from the 300/675 Hz start tone, falling
    // back to 576 when the recording carries none [ISO §4.2.3, §4.2.5].
    int ioc = 0;
    double start_sec = 0.0;      // skip this much of the input
    bool autolock = true;        // per-line dead-sector relock
    // Per-line sync search window, fraction of a line. Must exceed the
    // phasing<->image regime offset (~half a dead sector, 0.0225 lines) or
    // the tracker falls off the grid at that boundary and coasts to EOF.
    // Until session 11 this also gated which residuals the assembly would
    // believe, which made it two settings in one; it is now only the window.
    double search_frac = 0.03;
    int max_lines = 0;           // 0 = all available
    // Take the line-start phase from the phasing interval when the station
    // sends one and the image gives no per-line sync [WMO §5.2.3.4]. Off is
    // the pre-session-7 behaviour, kept so the two can be compared on the
    // same recording rather than argued about.
    bool use_phasing = true;
    // Draw only the picture: start tone and phasing before it, stop tone
    // after it, are control signals, not image [WMO §5.2.3, §5.2.5]. Off
    // draws every line from onset to EOF, which is what Nova did before
    // session 7.
    bool segment = true;
};

// Which of the two dead-sector styles WMO §5.1.3.3 permits the station
// actually sends. Decided from measured across-line consistency, not
// configured: it changes which per-line sync template can work at all.
enum class DeadSector {
    kBlackPulse,  // black pulse <= half the dead sector, then white
    kWhiteOnly,   // plain white across the whole dead sector
};

// Whether the recording's time axis is the straight line the rest of the
// decoder assumes. Not a property of the transmission: it is the capture
// chain — SDR, link, sound card, file — that inserts or drops samples.
enum class Timebase {
    kUnknown,  // no phasing interval and no per-line sync: not measurable
    kLinear,   // one period and one clock error describe the whole file
    kSteps,    // the timebase jumps; clock_ppm is the clock PLUS that rate
};

// What the PHASING half of the timebase test was able to say. Reported so
// that a kUnknown verdict names the witness it is missing rather than
// implying none exists — and so that "the edge is not straight" is never
// silently promoted to "the timebase steps", which is only one of the
// reasons an edge bends (session 10).
enum class PhasingWitness {
    kNone,       // no phasing interval in the recording at all
    kTooShort,   // found, but under 8 lines: nothing to fit a line to
    kStraight,   // the edge lies on a straight line: linear
    kNoisy,      // it does not, but the interval's own noise explains that
    kOneSkip,    // ...nor does noise, but there is a single jump, not a rate
    kSteps,      // persistent moves, more than one: a stepping timebase
};

struct DecodeResult {
    Image img;
    int lpm = 0;
    // Selected from the start tone unless DecodeOptions::ioc overrode it.
    int ioc = 576;
    double line_period_s = 0.0;  // measured, fractional
    double clock_ppm = 0.0;      // measured vs nominal
    int lines = 0;
    int locked_lines = 0;
    int clamped_corrections = 0;
    double max_step_px = 0.0;    // largest single-line correction
    // --- how straight the drawn line starts actually are (session 11) -----
    // Every line is drawn at `a + b*l + corr(l)`; the line the SIGNAL sends
    // starts at `a + b*l + resid(l)`. What is left over, `resid - corr`, is
    // not an internal diagnostic — it is the crookedness of the dead-sector
    // edge in the finished picture, which is the first thing an operator
    // sees. RMS over the drawn locked lines, in pixels of the image width.
    // Zero on a white-only station: there is no per-line measurement to be
    // right or wrong about (`per_line_sync` says which case this is).
    double place_rms_px = 0.0;
    double place_max_px = 0.0;
    // Lines where the line start moved PERSISTENTLY — a sample-level skip in
    // the capture chain, agreed by the lines on both sides — and the
    // assembly followed it within one line instead of ramping across the
    // smoothing window. A seam of one line is the honest picture of a
    // recording with samples missing; a ramp is a tear several lines deep.
    // Counted separately from `max_step_px`, which keeps its old meaning of
    // how far a NON-seam correction wandered in one line.
    int seams = 0;
    double max_seam_px = 0.0;
    // Rows whose timebase moved INSIDE the line, so the row was drawn in two
    // pieces with the break placed where the picture agrees best with the
    // row above (session 11b). A per-line offset cannot place such a row at
    // all: it is stretched, not moved.
    int intra_line_breaks = 0;
    // Rows that a dropout left with no line-start evidence at all, placed
    // by matching the row above them (session 11b). Not the same as an
    // unlocked row: most of those are placed correctly by their neighbours.
    int picture_placed = 0;
    // Rows inside a dropout whose line start the sync template still finds
    // at the FAR side of the phase move — the tracker's narrow window never
    // looked there, but the run is bracketed by two known levels and a
    // ±20-sample probe at each decides it (session 12: far side 0.66-0.96,
    // near side <= 0.22 on the three library dropouts). Placed by the
    // signal, not the picture; the one row the drop landed inside scores
    // nothing at either level and is left to the split search instead.
    int relocked_lines = 0;
    DeadSector dead_sector = DeadSector::kBlackPulse;
    double dead_consistency = 0.0;  // fraction of lines agreeing at anchor
    // False when the recording carries no per-line sync feature at all —
    // the picture is then drawn on the measured clock alone. Not a failure:
    // a white-only dead sector that is not reliably white gives a per-line
    // template nothing but picture content to match.
    bool per_line_sync = true;

    // --- phasing [WMO §5.2.3] ---------------------------------------------
    bool phasing_found = false;
    double phasing_t_start = 0.0, phasing_t_end = 0.0;
    int phasing_lines = 0;
    // 10-90% spread of the per-line white-edge positions across the phasing
    // interval, in samples. The phasing signal is one edge repeated at
    // exactly the line rate [WMO §5.2.3], so on a linear timebase this is
    // the measurement noise and nothing else.
    double phasing_spread = 0.0;
    // ...with the best straight line removed, so a constant clock error is
    // not counted as non-linearity. See PhasingResult::nonlinearity.
    double phasing_nonlinearity = 0.0;
    // Why that residual is not straight, which the size of it cannot say:
    // its line-to-line roughness (noise) and how many PERSISTENT moves it
    // contains (steps). See PhasingResult for the measurements behind both.
    double phasing_roughness = 0.0;
    int phasing_steps = 0;
    // Median per-line contrast of the run. Real intervals score 0.76-0.99
    // across the library; a faded one sits at the bottom of that.
    double phasing_score = 0.0;
    PhasingWitness phasing_witness = PhasingWitness::kNone;
    // Where the phasing anchor sits relative to the anchor the image lines
    // gave, in samples, wrapped to ±half a line. Reported ALWAYS, even when
    // the phasing anchor is not the one used, because it is the only
    // independent check on a phase that otherwise nothing corroborates:
    // two detectors sharing no code either agree or they do not.
    double phasing_anchor_delta = 0.0;
    // True when the drawn picture is phased from the phasing interval
    // rather than from the image lines.
    bool anchor_from_phasing = false;

    // --- timebase linearity -----------------------------------------------
    // `clock_ppm` above is one number for the whole recording, which is only
    // meaningful if the timebase IS one straight line. On a stepping
    // recording it is the clock plus the mean insertion rate, and
    // `phasing_anchor_delta` is the porch plus whatever the timebase did
    // between the two measurement epochs — neither is comparable with the
    // same number from a clean file. Session 11: where per-line sync exists
    // the steps are CORRECTED as well as reported (see `place_rms_px`), so
    // this flag is about those two numbers and about the recording, not
    // about the picture. Where it does not exist, nothing corrects them.
    Timebase timebase = Timebase::kUnknown;
    // Lines the step rate was measured over. ZERO means the image-domain
    // half of the test had nothing to work with (a white-only station, or
    // too few drawn lines) — not that it looked and found nothing.
    int timebase_lines = 0;
    int timebase_step_lines = 0;    // drawn lines where the residual stepped
    // ...per 1000 drawn lines. A FLOOR on the true insertion rate: the
    // local median that makes a step visible is 17 lines wide, so steps
    // closer together than that merge (synthetic: 90.9 inserted, 36.9
    // reported). Good for convicting, not for counting.
    double timebase_step_rate = 0.0;

    // --- segmentation [WMO §5.2.3 transmission sequence] -------------------
    // The picture actually drawn, in seconds into the recording. When no
    // control signal bounds an end, that end is the recording's.
    bool segmented = false;
    double image_t_start = 0.0, image_t_end = 0.0;
    int lines_dropped_head = 0;  // start tone + phasing
    int lines_dropped_tail = 0;  // stop tone and whatever follows it
};

DecodeResult decode_fax(const std::vector<float>& video, int fs,
                        const DecodeOptions& opt);

}  // namespace nova
