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
#include "hooks.hpp"
#include "image.hpp"
#include <limits>
#include <vector>

namespace nova {

// --- the line layout, and the sync template defined against it ------------
//
// These were private to `core/fax.cpp` until session 21. They are public
// now because the live preview renderer draws its own rows
// [live/preview.hpp, docs/05 §6] and must lock onto the SAME feature the
// batch path locks onto. The preview and the saved image are two pictures
// of one signal; two implementations of one template are two chances to
// disagree about which feature is the line start, and the operator would
// see that disagreement as the picture jumping when the batch decode
// replaces the preview. Same argument, and the same remedy, as
// `tone_median` moving into `core/tones.hpp` in session 20.
//
// Nothing here changed value or behaviour in the move: each constant keeps
// the measurement that set it, restated below.

// Dead sector, as a fraction of the line [WMO §5.1.3.3], and the optional
// black sync pulse inside it, which is at most half the dead sector.
constexpr double kFaxDeadFrac = 0.045;
constexpr double kFaxPulseFrac = 0.0225;

// Level slices for the across-line consistency profile. Deliberately well
// inside the demodulator's 0..1 black..white range so that fading and
// noise do not push a genuinely black pulse over the line.
constexpr double kFaxDarkLevel = 0.25;
constexpr double kFaxWhiteLevel = 0.93;

// A station "sends the pulse" when its black->white pulse shape holds on
// this fraction of lines. Measured across the 20-recording library
// (session 4): stations that send one score 0.48-0.94, stations that do
// not score 0.14-0.34. The cut sits in that gap; the two closest files to
// it are HDSDR (0.48) and JSC4 (0.50), both of which are pulse stations on
// other recordings of the same transmitter, so the cut errs the safe way.
constexpr double kFaxPulseConsistency = 0.40;

// Lock threshold: the pulse template swings ~0.9 on a clean black->white
// edge. This is "did the template really match", never "did the correction
// stay put".
constexpr double kFaxPulseLock = 0.6;

// Re-acquisition: after this many consecutive unlocked lines, sweep the
// whole line instead of the ±narrow window — but only on every Nth line,
// and at a coarse step, so a file that never locks costs a bounded extra.
//
// A tracker that only ever looks ±narrow around its own prediction can
// never come back from being wrong: a coarse anchor off by more than the
// window, or a stream time-skip, puts the sync outside every future window
// (measured: himawari.wav, anchor 128 samples late, 14 locks of 1988 — the
// signal itself is textbook). The sweep only counts if the template
// actually matches, so a white-only station cannot re-acquire onto picture
// content: it simply keeps coasting, which is the honest outcome.
//
// Public since session 21 for the same reason as the template itself: a
// dropout is exactly where the live preview and the batch decode would
// otherwise part company, and the preview coasting past one it could have
// recovered from is a visibly torn picture [live/preview.cpp].
constexpr int kFaxReacqMisses = 8;
constexpr int kFaxReacqEvery = 8;
constexpr double kFaxReacqStep = 4.0;

// Linear interpolation into the video at a fractional sample position.
// Clamps at both ends — so a caller that asks outside the data it holds
// gets the edge sample rather than an error, which is why the live
// renderer checks its buffer covers a row BEFORE drawing it
// [live/preview.cpp].
float fax_lerp_at(const std::vector<float>& v, double pos);

// Sync-template score for a station that sends the optional black pulse
// [WMO §5.1.3.3]: the dead sector opens with black (<= half the dead
// sector) followed by white. Content-independent — unlike edge strength
// this cannot lock onto picture content, because the pulse is black->white
// in every image line regardless of what precedes it.
// Returns roughly [-1, 1]; ~1.0 = clean sync at p.
double fax_pulse_score(const std::vector<float>& v, double p, double pulse);

// Best template position in [lo, hi], parabolic sub-sample refinement.
// `step` > 1 scans coarsely first and then refines at single-sample
// resolution around the winner — the template is smooth on the scale of
// its own width, so a coarse pass cannot step over the peak, and a
// whole-line re-acquisition sweep stays affordable.
//
// There is deliberately NO per-line template for a white-only dead sector.
// Two were built and measured against VMW 2215Z (session 4): "white across
// the dead sector, against the picture either side", and "rising edge into
// white". Both raise the lock count (0 -> 753 and -> 879 of 1162) and both
// make the picture WORSE — the first jitters inside the white run and tears
// the chart into strips, the second drifts and drags the fitted clock from
// -121 to -285 ppm, slanting the whole image. Neither is a lock; both are
// the picture's own white margin being matched. A white-only dead sector
// carries no per-line phase information, because WMO §5.1.3.3 puts nothing
// in it that the paper does not also contain. Such stations are decoded on
// the measured clock, and report zero locks, which is the truth.
double fax_best_sync(const std::vector<float>& v, double lo, double hi,
                     double pulse, double* score, double step = 1.0);

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
    // --- the operator's two corrections [docs/05 §7.1] --------------------
    // The only change M4 asks of core/, and the two do NOT behave the same
    // way. Both follow the existing auto-as-a-value idiom above (`lpm = 0`,
    // `ioc = 0` already mean "measure it"), so neither needs a mode toggle.
    //
    // PHASE — a SEED the search refines. Where the operator says the dead
    // sector is, as a fraction of the line width; negative (the default)
    // means no hint. Auto-phasing fails by picking the WRONG CANDIDATE for
    // the dead sector, and the click is what disambiguates which feature is
    // which — but it was made through a preview drawn on a possibly-wrong
    // period, so it is approximate in position. So the anchor search starts
    // here and settles on the best feature within `search_frac` of it: the
    // operator's judgement about WHICH, the decoder's precision about
    // WHERE. It does not decide which of the two dead-sector styles the
    // station sends — that is a property of the transmission, measured
    // across all the lines, and a click cannot turn a pulse station into a
    // white-only one [stage_dead_sector].
    double phase_anchor_hint = -1.0;
    // SYNC — a FALLBACK the measurement outranks. A line-rate trim in ppm,
    // used ONLY where the period fit has no baseline to measure over: a
    // white-only station, a forced start, too few locked lines to fit.
    // Where a baseline exists the fit wins, because a ppm eyeballed off
    // thirty seconds of preview is worse than one fitted over the whole
    // transmission — sessions 5, 8 and 9 are entirely about long baselines
    // beating short ones. The consequence, stated so it is not a surprise:
    // on a healthy recording the operator's value is measured away from,
    // and the saved image can differ from the preview they just corrected
    // by hand, in the direction of correct. `DecodeResult::
    // clock_from_fallback` reports which of the two happened.
    //
    // NaN = none, and it has to be: zero cannot mean auto here, because a
    // perfect clock IS 0 ppm. Any other sentinel would make some legal
    // measurement unrepresentable.
    double clock_ppm_fallback = std::numeric_limits<double>::quiet_NaN();
    // Log/progress/cancellation seams (core/hooks.hpp). All three null is
    // the batch default: silent, uninterruptible.
    DecodeHooks hooks;
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
    // ...except when it is not measured at all: true when the fit had no
    // baseline and `DecodeOptions::clock_ppm_fallback` was used instead, so
    // `clock_ppm` above is the OPERATOR's number. Reported because a
    // provenance field that says "operator" whenever the operator typed
    // something would be wrong on every healthy recording — the fit
    // outranks the typed value there, and the file must not claim
    // otherwise [live/engine.cpp `decode_qa`].
    bool clock_from_fallback = false;
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
    // True when it is phased from the operator's `phase_anchor_hint`
    // instead — refined locally, so this says which FEATURE was chosen by
    // hand, not that the position was. Exclusive with the flag above: the
    // hint is the operator overruling exactly the automatic choice that
    // one represents.
    bool anchor_from_hint = false;

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
// Failures are DecodeError (core/hooks.hpp), which is a std::runtime_error
// with a machine-readable kind.

}  // namespace nova
