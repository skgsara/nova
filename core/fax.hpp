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
    int ioc = 576;               // 576 or 288 (picture width)
    double start_sec = 0.0;      // skip this much of the input
    bool autolock = true;        // per-line dead-sector relock
    double search_frac = 0.03;   // sync search window, fraction of line
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

struct DecodeResult {
    Image img;
    int lpm = 0;
    double line_period_s = 0.0;  // measured, fractional
    double clock_ppm = 0.0;      // measured vs nominal
    int lines = 0;
    int locked_lines = 0;
    int clamped_corrections = 0;
    double max_step_px = 0.0;    // largest single-line correction
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
    // Where the phasing anchor sits relative to the anchor the image lines
    // gave, in samples, wrapped to ±half a line. Reported ALWAYS, even when
    // the phasing anchor is not the one used, because it is the only
    // independent check on a phase that otherwise nothing corroborates:
    // two detectors sharing no code either agree or they do not.
    double phasing_anchor_delta = 0.0;
    // True when the drawn picture is phased from the phasing interval
    // rather than from the image lines.
    bool anchor_from_phasing = false;

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
