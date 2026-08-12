// test_fixture.cpp — fixture screamer: real off-air JMH test chart.
//
// fixtures/test-chart-jmh-60s.wav: first 60 s of "test chart.m4a"
// (JMH Tokyo via 13988.5 kHz, AAC m4a -> 8 kHz mono WAV), containing the
// 300 Hz start tone, 30 s phasing, and the first image lines.
// Unique coverage (fixture doctrine): first REAL signal — real clock
// error (~-100 ppm), real noise/fading, and a long-path echo (~144 ms,
// discovered during M0 bring-up).
//
// Bounds are set between measured-known-good and clearly-broken values.
#include "../core/demod.hpp"
#include "../core/fax.hpp"
#include "../core/wav.hpp"
#include <cmath>
#include <cstdio>

int main(int argc, char** argv) {
    const char* path =
        argc > 1 ? argv[1] : "../fixtures/test-chart-jmh-60s.wav";
    int failures = 0;
    try {
        nova::Wav w = nova::read_wav(path);
        std::vector<float> video = nova::fm_demod(w.samples, w.sample_rate,
                                                  1900.0, 400.0);
        nova::DecodeOptions opt;
        nova::DecodeResult r = nova::decode_fax(video, w.sample_rate, opt);
        std::printf("fixture: lpm=%d clock=%+.1f ppm lines=%d locked=%d\n",
                    r.lpm, r.clock_ppm, r.lines, r.locked_lines);

        auto check = [&](bool ok, const char* what) {
            std::printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
            if (!ok) failures++;
        };
        check(r.lpm == 120, "JMH is 120 lpm");
        check(r.lines >= 110, "decodes >= 110 lines from 60 s");
        check(r.lines <= 125, "not inventing lines");
        check(r.clock_ppm > -160 && r.clock_ppm < -40,
              "clock in measured real-recording range (-99 +/- 60 ppm)");
        check(r.locked_lines > 0.6 * r.lines,
              "majority of lines lock on real signal");
        check(r.max_step_px < 100, "no wild line jumps");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "fixture test error: %s\n", e.what());
        return 1;
    }
    std::printf(failures ? "%d FAILURE(S)\n" : "fixture test passed\n",
                failures);
    return failures ? 1 : 0;
}
