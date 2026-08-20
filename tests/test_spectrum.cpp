// test_spectrum.cpp — the screamer for the M4.5 tuning strip
// [ROADMAP M4.5, live/spectrum.hpp].
//
// What makes this worth writing rather than eyeballing. A waterfall is the
// most convincing wrong answer in the project: it always LOOKS like a
// spectrum. Colourful noise in the right shape reads as a working
// instrument, and an operator who nulls a mistune against a marker line
// that names the wrong frequency has been actively misled — worse off than
// with no strip at all. So the claims here are about Hz, not about pixels.
//
// Claims defended:
//   - the column/frequency mapping is invertible over the band and refuses
//     frequencies outside it;
//   - a REAL generated sine peaks where `hz_column` says it should, at nine
//     frequencies across the band, with the peak naming the true frequency
//     to within 1.5 FFT bins. This is the load-bearing one: it ties the
//     arithmetic to the DSP instead of to itself, so a band edge or a
//     mapping written the wrong way round cannot pass by agreeing with
//     itself [the trap session 30 recorded: two sides equal by
//     construction cannot fail];
//   - the answer does not move when the SOUND CARD changes — 44100 and
//     48000 put the same tone in the same column, which is the property
//     that stops the strip being a per-device fiction;
//   - the two WMO tones together read as two peaks with a valley between,
//     because "am I tuned" is answered by seeing 1500 and 2300 at once;
//   - the dB scale is a dB scale: halving the amplitude drops the column
//     by 6.02 dB of the display range, checked between two amplitudes that
//     both sit off the clamp;
//   - an out-of-band signal does NOT produce an in-band peak;
//   - the waterfall scrolls, keeps its history in order, wraps without
//     tearing, and reports nothing measured as nothing measured rather
//     than as silence.
//
// One claim is deliberately absent: there is no reset() to check. It was
// written, found to have no caller, and deleted [live/spectrum.hpp].
#include "../live/spectrum.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}

void checkf(bool ok, const char* fmt, double a, double b) {
    char buf[256];
    std::snprintf(buf, sizeof buf, fmt, a, b);
    check(ok, buf);
}

constexpr double kPi = 3.14159265358979323846;

// A sine of `sec` seconds at `hz`, pushed in 1024-frame blocks because that
// is what RtAudio hands the callback and a hop boundary landing inside a
// block is the ordinary case, not an edge case.
void feed_sine(nova::SpectrumAnalyzer& sa, double hz, double amp, double sec,
               int fs, double* phase) {
    const long n = std::lround(sec * fs);
    std::vector<float> block(1024);
    long done = 0;
    while (done < n) {
        const long m = std::min<long>(1024, n - done);
        for (long i = 0; i < m; i++) {
            block[static_cast<std::size_t>(i)] =
                static_cast<float>(amp * std::sin(*phase));
            *phase += 2.0 * kPi * hz / fs;
            if (*phase > 2.0 * kPi) *phase -= 2.0 * kPi;
        }
        sa.push(block.data(), static_cast<std::size_t>(m));
        done += m;
    }
}

int argmax(const float* v, int n) {
    int best = 0;
    for (int i = 1; i < n; i++)
        if (v[i] > v[best]) best = i;
    return best;
}

// --- 1. the mapping, both ways ---------------------------------------------
void test_mapping() {
    std::printf("-- mapping --\n");
    nova::SpectrumAnalyzer sa(48000);
    const int C = sa.columns();

    bool inverse = true, monotonic = true;
    double prev = -1.0;
    for (int c = 0; c < C; c++) {
        if (sa.hz_column(sa.column_hz(c)) != c) inverse = false;
        if (sa.column_hz(c) <= prev) monotonic = false;
        prev = sa.column_hz(c);
    }
    check(inverse, "hz_column(column_hz(c)) == c for every column");
    check(monotonic, "column_hz is strictly increasing");

    check(sa.hz_column(1500.0) >= 0 && sa.hz_column(2300.0) >= 0,
          "both WMO tones are inside the band [WMO §5.2.1]");
    check(sa.hz_column(1500.0) < sa.hz_column(2300.0),
          "black (1500) sits left of white (2300)");
    check(sa.hz_column(799.0) == -1 && sa.hz_column(3000.0) == -1 &&
              sa.hz_column(0.0) == -1 && sa.hz_column(20000.0) == -1,
          "frequencies outside the band are refused, not clamped");
    // The band edges are the operator's "which way do I turn" margin, so
    // they are asserted rather than left to the defaults being read.
    check(std::abs(sa.column_hz(0) - 800.0) < 10.0 &&
              std::abs(sa.column_hz(C - 1) - 3000.0) < 10.0,
          "the band really runs 800 Hz to 3000 Hz [Sara, session 36]");
}

// --- 2. a real tone peaks where the mapping says ---------------------------
// Nine frequencies, not one. A check that only ever runs at one point in a
// range is not checking the range [session 29's third survivor shape].
void test_peak_position() {
    std::printf("-- a generated tone peaks where hz_column says --\n");
    const int fs = 48000;
    const double bin_hz = static_cast<double>(fs) / 4096.0;
    // 0.75 of a bin, not the 1.5 the first draft used. The measured worst
    // is half a bin (the interpolated curve's breakpoints are the bin
    // centres, so that is the floor of the method), and a limit set at
    // 1.5 bins would let a whole-bin indexing error through — which is the
    // ordinary way this code goes wrong. A deterministic test may hold a
    // tight tolerance; there is no noise here to leave room for.
    const double tol_hz = 0.75 * bin_hz;

    const double freqs[] = {850.0,  1000.0, 1200.0, 1500.0, 1800.0,
                            2100.0, 2300.0, 2650.0, 2950.0};
    bool all_col = true, all_hz = true;
    double worst = 0.0;
    for (double f : freqs) {
        nova::SpectrumAnalyzer sa(fs);
        double ph = 0.0;
        feed_sine(sa, f, 0.5, 0.5, fs, &ph);
        if (!sa.has_data()) {
            all_col = all_hz = false;
            break;
        }
        const int peak = argmax(sa.latest().data(), sa.columns());
        const int want = sa.hz_column(f);
        if (std::abs(peak - want) > 1) {
            all_col = false;
            std::printf("       %.0f Hz -> column %d, expected %d\n", f, peak,
                        want);
        }
        const double err = std::abs(sa.column_hz(peak) - f);
        worst = std::max(worst, err);
        if (err > tol_hz) {
            all_hz = false;
            std::printf("       %.0f Hz -> named %.1f Hz (err %.1f)\n", f,
                        sa.column_hz(peak), err);
        }
    }
    check(all_col, "nine tones across the band each peak in their own column");
    checkf(all_hz, "the peak names the true frequency to within %.1f Hz "
                   "(worst seen %.1f)",
           tol_hz, worst);
}

// --- 3. the answer does not depend on the sound card -----------------------
void test_sample_rate_independence() {
    std::printf("-- 44100 and 48000 agree --\n");
    const double freqs[] = {1500.0, 1900.0, 2300.0};
    bool same = true;
    for (double f : freqs) {
        nova::SpectrumAnalyzer a(48000), b(44100);
        double p1 = 0.0, p2 = 0.0;
        feed_sine(a, f, 0.5, 0.5, 48000, &p1);
        feed_sine(b, f, 0.5, 0.5, 44100, &p2);
        const int ca = argmax(a.latest().data(), a.columns());
        const int cb = argmax(b.latest().data(), b.columns());
        if (std::abs(ca - cb) > 1) {
            same = false;
            std::printf("       %.0f Hz -> column %d at 48k, %d at 44.1k\n", f,
                        ca, cb);
        }
    }
    check(same, "the same tone lands in the same column at either rate");
}

// --- 4. both WMO tones at once ---------------------------------------------
void test_two_tones() {
    std::printf("-- black and white together --\n");
    const int fs = 48000;
    nova::SpectrumAnalyzer sa(fs);
    // Both tones at once is what an FSK fax signal actually looks like over
    // an 85 ms window of picture content: it is hopping between them.
    const long n = fs / 2;
    std::vector<float> block(1024);
    double p1 = 0.0, p2 = 0.0;
    long done = 0;
    while (done < n) {
        const long m = std::min<long>(1024, n - done);
        for (long i = 0; i < m; i++) {
            block[static_cast<std::size_t>(i)] = static_cast<float>(
                0.4 * std::sin(p1) + 0.4 * std::sin(p2));
            p1 += 2.0 * kPi * 1500.0 / fs;
            p2 += 2.0 * kPi * 2300.0 / fs;
        }
        sa.push(block.data(), static_cast<std::size_t>(m));
        done += m;
    }

    const int c1500 = sa.hz_column(1500.0), c2300 = sa.hz_column(2300.0);
    const std::vector<float>& v = sa.latest();
    const int mid = (c1500 + c2300) / 2;

    // Each tone is the local maximum of its own neighbourhood...
    const int lo_peak = argmax(v.data() + c1500 - 6, 13) + c1500 - 6;
    const int hi_peak = argmax(v.data() + c2300 - 6, 13) + c2300 - 6;
    check(std::abs(lo_peak - c1500) <= 1 && std::abs(hi_peak - c2300) <= 1,
          "two tones give two peaks, each in its own column");
    // ...and there is a real valley between them, which is what makes the
    // pair legible as two lines rather than one smear.
    check(v[static_cast<std::size_t>(mid)] <
                  v[static_cast<std::size_t>(c1500)] - 0.15 &&
              v[static_cast<std::size_t>(mid)] <
                  v[static_cast<std::size_t>(c2300)] - 0.15,
          "the gap between them is visibly lower than both");
}

// --- 5. the dB scale is a dB scale -----------------------------------------
void test_db_scale() {
    std::printf("-- the scale --\n");
    const int fs = 48000;
    const double f = 1900.0;  // the fax band centre, off a bin centre
    double vals[3];
    const double amps[3] = {0.5, 0.25, 0.125};
    for (int i = 0; i < 3; i++) {
        nova::SpectrumAnalyzer sa(fs);
        double ph = 0.0;
        feed_sine(sa, f, amps[i], 0.5, fs, &ph);
        vals[i] = sa.latest()[static_cast<std::size_t>(argmax(
            sa.latest().data(), sa.columns()))];
    }
    // Halving amplitude is -6.02 dB, which over an 80 dB display range is
    // 0.0753 of full height. Checked as a DIFFERENCE between two levels
    // that both sit off the clamp, so the clamp cannot supply the answer.
    const double step1 = vals[0] - vals[1], step2 = vals[1] - vals[2];
    const double want = 6.0206 / 80.0;
    checkf(std::abs(step1 - want) < 0.008,
           "halving the amplitude drops the column by %.4f (measured %.4f)",
           want, step1);
    checkf(std::abs(step2 - want) < 0.008,
           "and again at the next halving: %.4f (measured %.4f)", want, step2);
    check(vals[0] < 0.999 && vals[2] > 0.001,
          "all three levels sit off both ends of the scale");

    // Full scale reads full scale. Hann plus interpolation loses at most
    // ~1.4 dB to scalloping, which is 0.018 of the range.
    nova::SpectrumAnalyzer sa(fs);
    double ph = 0.0;
    feed_sine(sa, f, 1.0, 0.5, fs, &ph);
    const double top =
        sa.latest()[static_cast<std::size_t>(argmax(sa.latest().data(),
                                                    sa.columns()))];
    checkf(top > 0.97, "a full-scale sine reads %.3f, above %.2f", top, 0.97);
}

// --- 6. silence, and the difference between silence and nothing ------------
void test_silence_and_nothing() {
    std::printf("-- silence is not the same as no capture --\n");
    const int fs = 48000;
    nova::SpectrumAnalyzer sa(fs);
    check(!sa.has_data(), "before any audio, has_data() is false");
    check(sa.latest().empty(), "...and there is no column to draw");
    check(sa.row(0) == nullptr, "...and no waterfall row either");

    // Less than one window: still nothing measured.
    std::vector<float> quiet(2048, 0.0f);
    sa.push(quiet.data(), quiet.size());
    check(!sa.has_data(), "half a window is still nothing measured");

    std::vector<float> more(48000, 0.0f);
    sa.push(more.data(), more.size());
    check(sa.has_data(), "a second of silence IS a measurement");
    double peak = 0.0;
    for (float x : sa.latest()) peak = std::max(peak, static_cast<double>(x));
    checkf(peak < 1e-6, "...and it reads %.6f, the floor, not %.1f", peak, 0.0);
}

// --- 7. out of band stays out of band --------------------------------------
void test_out_of_band() {
    std::printf("-- out-of-band signals --\n");
    const int fs = 48000;
    const double freqs[] = {200.0, 400.0, 3600.0, 5000.0};
    bool clean = true;
    double worst = 0.0;
    for (double f : freqs) {
        nova::SpectrumAnalyzer sa(fs);
        double ph = 0.0;
        feed_sine(sa, f, 1.0, 0.5, fs, &ph);
        double peak = 0.0;
        for (float x : sa.latest())
            peak = std::max(peak, static_cast<double>(x));
        worst = std::max(worst, peak);
        if (peak > 0.15) {
            clean = false;
            std::printf("       %.0f Hz leaks to %.3f in band\n", f, peak);
        }
    }
    checkf(clean, "a full-scale tone outside the band leaves the band below "
                  "%.2f (worst %.3f)",
           0.15, worst);
}

// --- 8. the waterfall ------------------------------------------------------
void test_waterfall() {
    std::printf("-- the waterfall --\n");
    const int fs = 48000;
    nova::SpectrumAnalyzer sa(fs);
    double ph = 0.0;
    feed_sine(sa, 1500.0, 0.5, 1.0, fs, &ph);
    const int after_first = sa.rows_filled();
    ph = 0.0;
    feed_sine(sa, 2300.0, 0.5, 0.5, fs, &ph);

    check(sa.rows_filled() > after_first, "rows accumulate as audio arrives");
    check(sa.row(0) != nullptr &&
              std::equal(sa.latest().begin(), sa.latest().end(), sa.row(0)),
          "row(0) IS the newest column");

    const int c1500 = sa.hz_column(1500.0), c2300 = sa.hz_column(2300.0);
    check(std::abs(argmax(sa.row(0), sa.columns()) - c2300) <= 1,
          "the newest row holds the tone that just arrived");
    // Far enough back to be clear of the 85 ms window straddling the switch.
    const int old_row = after_first - 2;
    check(old_row > 0 && sa.row(old_row) != nullptr &&
              std::abs(argmax(sa.row(old_row), sa.columns()) - c1500) <= 1,
          "an older row still holds the tone that was playing then");

    check(sa.row(sa.rows_filled()) == nullptr,
          "past the filled rows there is no row, so a fresh strip draws "
          "blank rather than a ring's leftovers");

    // Wrap. Push well past the history depth and the invariants hold.
    ph = 0.0;
    feed_sine(sa, 1900.0, 0.5, 10.0, fs, &ph);
    check(sa.rows_filled() == sa.history(),
          "the history saturates at its depth and does not grow");
    check(std::equal(sa.latest().begin(), sa.latest().end(), sa.row(0)),
          "row(0) is still the newest column after the ring has wrapped");
    const int c1900 = sa.hz_column(1900.0);
    bool all_1900 = true;
    for (int r = 0; r < sa.rows_filled(); r++)
        if (std::abs(argmax(sa.row(r), sa.columns()) - c1900) > 1)
            all_1900 = false;
    check(all_1900,
          "after ten seconds of one tone, every row in the ring holds it — "
          "no row is torn or stale");
}

// --- 9. the strip advances at the rate it claims --------------------------
void test_rate() {
    std::printf("-- one column per hop --\n");
    const int fs = 48000;
    nova::SpectrumAnalyzer sa(fs);
    double ph = 0.0;
    std::vector<float> block(1024);
    long done = 0;
    int produced = 0;
    const long n = fs * 10L;
    while (done < n) {
        const long m = std::min<long>(1024, n - done);
        for (long i = 0; i < m; i++) {
            block[static_cast<std::size_t>(i)] =
                static_cast<float>(0.5 * std::sin(ph));
            ph += 2.0 * kPi * 1900.0 / fs;
        }
        produced += sa.push(block.data(), static_cast<std::size_t>(m));
        done += m;
    }
    // 50 ms hops: 200 columns in ten seconds, less the one window of
    // start-up. The claim is that the strip keeps up with the capture and
    // does not silently decimate.
    checkf(produced >= 197 && produced <= 200,
           "ten seconds gives %.0f columns (expected ~%.0f)",
           static_cast<double>(produced), 200.0);
}

}  // namespace

int main() {
    std::printf("=== the tuning strip [ROADMAP M4.5] ===\n");
    test_mapping();
    test_peak_position();
    test_sample_rate_independence();
    test_two_tones();
    test_db_scale();
    test_silence_and_nothing();
    test_out_of_band();
    test_waterfall();
    test_rate();
    std::printf("%s\n", failures ? "FAILED" : "OK");
    return failures ? 1 : 0;
}
