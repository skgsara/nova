// gen.hpp — test-harness WEFAX signal generator.
// Produces an on-spec F3C baseband signal [docs/01 §3, §4]:
//   start tone (300/675 Hz, 5 s) -> phasing (30 lines, 5% white wedge)
//   -> image lines (dead sector with sync pulse + picture sector)
//   -> stop (450 Hz, 5 s + 10 s black).
// Clock error is injected by resampling, noise as additive Gaussian.
// This is a TEST TOOL, not a product feature (ROADMAP M0).
#pragma once
#include "image.hpp"
#include <vector>

namespace nova {

struct GenOptions {
    int fs = 8000;
    int lpm = 120;          // 60/90/120
    int ioc = 576;          // 576/288 -> start tone 300/675 Hz
    double ppm = 0.0;       // clock error to inject
    double noise = 0.0;     // Gaussian RMS, relative to full scale
    double amplitude = 0.5;
    double deviation = 400.0;  // 400 (HF) or 150 (LF) [WMO §5.5.2]
    bool start_tone = true;
    bool phasing = true;
    bool stop_tone = true;
    // WMO §5.2.3.2 permits the phasing waveform to be symmetric (50/50) or
    // asymmetric (5% white / 95% black). A detector has to accept both, so
    // the generator has to be able to produce both.
    bool phasing_symmetric = false;
    // false = white-only dead sector (the pulse is optional, WMO §5.1.3.3):
    // no per-line sync exists, so the picture rides entirely on the
    // measured clock. This is how VMW, NMC and GYA transmit.
    bool dead_pulse = true;
};

// content: grayscale image whose width is the picture sector (any width;
// it is resampled into the line). Rows repeat if content is shorter than
// image_lines. image_lines = number of picture lines to emit.
std::vector<float> gen_fax_signal(const Image& content, int image_lines,
                                  const GenOptions& opt);

// Deterministic test pattern: light background, solid black vertical bar
// (straightness reference), horizontal bars every 50 lines (line-count
// reference), gradient strip.
Image gen_test_pattern(int width, int height);

}  // namespace nova
