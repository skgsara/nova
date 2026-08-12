// gen.cpp
#include "gen.hpp"
#include "resample.hpp"
#include <cmath>
#include <random>
#include <stdexcept>

namespace nova {
namespace {
constexpr double kPi = 3.14159265358979323846;
}

Image gen_test_pattern(int width, int height) {
    Image img;
    img.width = width;
    img.height = height;
    img.px.resize(static_cast<size_t>(width) * height, 200);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint8_t v = 200;
            // vertical straightness reference bar
            if (x >= width / 6 && x < width / 6 + width / 36) v = 0;
            // horizontal line-count reference bars
            if (y % 50 < 2) v = 0;
            // gradient strip on the right
            if (x >= 2 * width / 3)
                v = static_cast<uint8_t>(255.0 * (x - 2 * width / 3) /
                                         (width / 3));
            img.px[static_cast<size_t>(y) * width + x] = v;
        }
    }
    return img;
}

std::vector<float> gen_fax_signal(const Image& content, int image_lines,
                                  const GenOptions& opt) {
    if (content.width <= 0 || content.height <= 0)
        throw std::invalid_argument("gen_fax_signal: empty content");
    const double fs = opt.fs;
    const double period = fs * 60.0 / opt.lpm;  // samples per line
    const int plen = static_cast<int>(period);
    const double dead = 0.045 * period;         // [WMO §5.1.3.3]

    // Build the video waveform v(t) in [0,1] (0=black, 1=white).
    std::vector<float> vid;
    auto push_samples = [&](int n, float v) {
        vid.insert(vid.end(), static_cast<size_t>(n), v);
    };
    // Alternating black/white tone at freq f0 for `seconds`.
    // The half-period must stay fractional. Truncating it to whole samples
    // (as this did until session 6) quantizes the tone to fs/(2k): at
    // fs=8000 that turned 300 Hz into 307.7, 450 into 500 and 675 into 800
    // — the last two far outside the ±1% of WMO §5.2.6, so the generator
    // was not producing control signals at all. Measured with nova-tones
    // before the fix: 306.0 / 499.0 / 800.5 Hz.
    auto push_tone = [&](double f0, double seconds) {
        const long total = static_cast<long>(seconds * fs);
        const double half = fs / (2.0 * f0);
        for (long i = 0; i < total; i++)
            vid.push_back((static_cast<long>(i / half) & 1) ? 1.0f : 0.0f);
    };

    if (opt.start_tone)
        push_tone(opt.ioc == 288 ? 675.0 : 300.0, 5.0);  // [WMO §5.2.2]

    const int phasing_lines = opt.phasing ? opt.phasing_lines : 0;
    const int total_lines = phasing_lines + image_lines;
    for (int l = 0; l < total_lines; l++) {
        std::vector<float> line(plen);
        if (l < phasing_lines) {
            // Phasing: leading edge of white at dead-sector entry
            // [WMO §5.2.3.4]. The white run is either the 5% asymmetric
            // wedge or a symmetric half-line [WMO §5.2.3.2].
            const double wlen = opt.phasing_symmetric ? 0.5 * plen : dead;
            for (int i = 0; i < plen; i++)
                line[i] = (i < wlen) ? 1.0f : 0.0f;
        } else {
            // image line, measured JMH layout (session 3): sync pulse
            // (black) 1.5%, white gap to 3.6%, picture to 98.4%, then a
            // black porch to end of line. The pulse is OPTIONAL in
            // WMO §5.1.3.3 — with dead_pulse false the dead sector is
            // plain white, as VMW/NMC/GYA send it, and the decoder then
            // has no per-line sync at all and must draw on the measured
            // clock alone.
            for (int i = 0; i < plen; i++)
                line[i] = (opt.dead_pulse && i < 0.015 * plen) ? 0.0f : 1.0f;
            const int pic0 = static_cast<int>(0.036 * plen);
            const int pic1 = static_cast<int>(0.984 * plen);
            const int row = (l - phasing_lines) % content.height;
            for (int i = pic0; i < pic1; i++) {
                const int x = static_cast<int>(
                    static_cast<double>(i - pic0) / (pic1 - pic0) *
                    content.width);
                line[i] = content.px[static_cast<size_t>(row) *
                                         content.width +
                                     std::min(x, content.width - 1)] /
                          255.0f;
            }
            // Black porch closing the line. The dead sector straddles the
            // line boundary [WMO §5.1.3.3], so porch and pulse are the two
            // halves of one feature: a station that sends no pulse sends no
            // porch either, and its dead sector is white end to end (VMW,
            // NMC, GYA — session 4 measured white consistency 0.70-0.99).
            // Emitting the porch anyway leaves a black->white edge at every
            // line boundary, which is a sync pulse in all but name: it gave
            // 629 locks on a signal generated with dead_pulse false.
            for (int i = pic1; i < plen; i++)
                line[i] = opt.dead_pulse ? 0.0f : 1.0f;
        }
        vid.insert(vid.end(), line.begin(), line.end());
    }

    if (opt.stop_tone) {
        push_tone(450.0, 5.0);   // [WMO §5.2.5]
        push_samples(static_cast<int>(10.0 * fs), 0.0f);
    }

    // FM modulate: f = 1900 + dev * (2v - 1)  [WMO §5.3.1.2]
    std::vector<float> out(vid.size());
    double ph = 0.0;
    for (size_t i = 0; i < vid.size(); i++) {
        const double f = 1900.0 + opt.deviation * (2.0 * vid[i] - 1.0);
        ph += 2.0 * kPi * f / fs;
        if (ph > 1e9) ph = std::fmod(ph, 2.0 * kPi);
        out[i] = static_cast<float>(opt.amplitude * std::sin(ph));
    }

    if (opt.noise > 0.0) {
        std::mt19937 rng(42);
        std::normal_distribution<double> g(0.0, opt.noise);
        for (auto& s : out) s += static_cast<float>(g(rng));
    }

    if (opt.ppm != 0.0) {
        const double r = 1.0 + opt.ppm * 1e-6;
        return resample_ratio(out, r);
    }
    return out;
}

}  // namespace nova
