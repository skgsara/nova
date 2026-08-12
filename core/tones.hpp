// tones.hpp — control-tone detection (start / IOC-select and stop).
//
// The control signals are alternating black/white at a precise rate in the
// VIDEO domain, not audio tones: 300 Hz = IOC 576, 675 Hz = IOC 288
// [WMO §5.2.2, ISO §3.5], 450 Hz stop [WMO §5.2.5], all to ±1%
// [WMO §5.2.6]. So detection runs on demodulated video, as JWX's Goertzel
// pair and fldigi's transition counter both do.
//
// What Nova adds over the prior art [docs/00, session 6]: the accept test
// is spectral PURITY — the fraction of a window's AC power that sits in
// the tone's own bin — not a transition rate. A transition counter cannot
// tell a clean 300 Hz square wave from text-heavy picture content that
// merely averages 300 transitions per second, and text-heavy content is
// the roadmap's named false-start trap (M3). Purity separates them because
// a rectangular control tone is one line in the spectrum and a page of
// weather text is broadband.
#pragma once
#include <vector>

namespace nova {

enum class ToneKind {
    kStartIOC576,  // 300 Hz [WMO §5.2.2]
    kStartIOC288,  // 675 Hz [WMO §5.2.2]
    kStop,         // 450 Hz [WMO §5.2.5]
};

struct ToneEvent {
    ToneKind kind;
    double t_start = 0.0;  // seconds into the video
    double t_end = 0.0;
    double freq_hz = 0.0;  // measured (median over the run), not nominal
    double purity = 0.0;   // median over the run, 0..1
};

struct ToneOptions {
    double win_sec = 0.25;   // Hann window; bin ~8 Hz at this length
    double hop_sec = 0.125;
    // Accept threshold on purity. Measured, not assumed: see the library
    // survey in docs/00 (session 6) for the separation this sits in.
    double purity = 0.35;
    // Search band about each nominal frequency, as a fraction. Wider than
    // the ±1% of WMO §5.2.6 on purpose, so an out-of-tolerance transmitter
    // is MEASURED as out of tolerance rather than silently missed.
    double tol = 0.015;
    // Spec durations are 5-10 s (start) and 5 s (stop); the accept minimum
    // sits well below both, because a recording routinely opens part-way
    // through the tone (GYA 2300Z gives ~2 s of its start tone before the
    // file begins). Duration is not what separates tone from content here
    // — purity is — so this only has to exclude a momentary burst.
    double min_start_sec = 2.0;
    double min_stop_sec = 2.0;
    // Longest dropout bridged inside one run. HF signals fade mid-tone;
    // measured on the library, real stop tones lose 0.5-1.5 s at a time.
    double max_gap_sec = 2.0;
    // Fraction of a run's span that must be above threshold, so the gap
    // rule cannot staple two unrelated bursts into one event.
    double min_hot_frac = 0.5;
    // Largest 10-90% spread of measured frequency within one run, as a
    // fraction of nominal, before the run is rejected as incoherent.
    // (weatherfax_pi/KiwiSDR apply the same shape of test to the phasing
    // wedge; here it is what stops a lucky noise burst from qualifying.)
    double max_spread = 0.01;
};

// All control tones found in `video`, in time order.
std::vector<ToneEvent> detect_tones(const std::vector<float>& video, int fs,
                                    const ToneOptions& opt = ToneOptions());

// Fraction of the AC power of video[s .. s+n) that lies in the bin at `f`.
// 1.0 = a pure sinusoid at f; 8/pi^2 = 0.811 = an ideal square wave at f
// (the rest is its odd harmonics); broadband content gives ~0.
// Exposed for the survey tool and the tests.
double tone_purity(const std::vector<float>& v, size_t s, size_t n, int fs,
                   double f);

// Best purity over the search band about `nominal` (fraction `tol`), with
// the frequency it peaked at. This — not a single probe at the nominal
// frequency — is what the accept decision reads, so it is also what the
// survey tool must plot.
double tone_purity_band(const std::vector<float>& v, size_t s, size_t n,
                        int fs, double nominal, double tol,
                        double* freq_out = nullptr);

const char* tone_name(ToneKind k);

}  // namespace nova
