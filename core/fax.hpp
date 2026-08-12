// fax.hpp — WEFAX line sync and image assembly.
//
// Approach [docs/01 §5]:
//   - line period measured from the signal itself (autocorrelation),
//     giving the sound-card clock error for free;
//   - coarse phase from a fold-average of the first lines;
//   - per-line relock on the dead-sector edge [WMO §5.1.3.3] with a
//     fractional accumulator (weatherfax_pi/KiwiSDR approach), so a
//     wrong or drifting clock does not slant the picture.
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
};

DecodeResult decode_fax(const std::vector<float>& video, int fs,
                        const DecodeOptions& opt);

}  // namespace nova
