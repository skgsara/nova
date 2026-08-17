// test_malformed.cpp — the screamers for untrusted input, added session 32
// after audit Pass D found three ways to turn a tiny file into gigabytes of
// resident memory and unbounded CPU.
//
// Nova decodes audio that arrives from somewhere else: a recording someone
// shared, a capture from a receiver nobody controls. A WAV header is a set
// of CLAIMS about a file, and every one of them was believed. Two measured
// results from the audit, on the code as it stood:
//   - a 144-byte file declaring a 4 GB data chunk reached ~12.9 GB of
//     footprint and never finished [D-PERF-001];
//   - a WELL-FORMED file — every field internally consistent — declaring a
//     1 Hz sample rate drove resample() to an 8000x output and hung
//     [D-PERF-002]. That one needs no malformed header at all: 1 is a
//     legal value in a legal field, and nothing bounded what it implied.
//
// Claims defended here:
//   - a declared data-chunk size larger than the file is clamped to the
//     bytes that actually exist, not allocated;
//   - a sample rate that cannot represent WEFAX is refused, and the rates
//     that can are still accepted (the guard must not eat real input);
//   - resample_ratio refuses a ratio far outside real use, whatever the
//     caller, so the bound does not depend on read_wav being the only door;
//   - truncation anywhere in the header or the data is refused or produces
//     a short result — never a hang.
//
// Instrument design, because this is where a test like this goes wrong.
// Every check is built so that DELETING the guard makes it FAIL FAST rather
// than hang or exhaust the machine — a screamer that takes the machine down
// with it cannot be run, and one that burns the ctest timeout reports as an
// infrastructure problem instead of a defect [the session-23 rule]. So the
// lying chunk declares 50 MB rather than 4 GB (50 MB against 100 real bytes
// is still a 500000x amplification, and it allocates fast enough to fail an
// assertion instead of thrashing), and the ratio check uses a 10-sample
// input so an unguarded 8000x expands to 80000 floats — wrong, cheap, and
// caught by the missing throw.
//
// This test generates its own inputs and reads no fixture. That is
// deliberate: it is the one suite that must run everywhere, including from
// a checkout that has no recordings in it.
#include "../core/resample.hpp"
#include "../core/wav.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}

void put_u32(std::vector<unsigned char>& v, uint32_t x) {
    v.push_back(x & 0xFF);
    v.push_back((x >> 8) & 0xFF);
    v.push_back((x >> 16) & 0xFF);
    v.push_back((x >> 24) & 0xFF);
}
void put_u16(std::vector<unsigned char>& v, uint16_t x) {
    v.push_back(x & 0xFF);
    v.push_back((x >> 8) & 0xFF);
}
void put_tag(std::vector<unsigned char>& v, const char* t) {
    for (int i = 0; i < 4; i++) v.push_back(static_cast<unsigned char>(t[i]));
}

// Build a WAV whose declared data size and actual payload can DISAGREE,
// which is the whole point: declared_size is what the header claims,
// payload_bytes is what is really there.
std::string write_wav_raw(const std::string& name, uint32_t rate,
                          uint16_t channels, uint16_t bits,
                          uint32_t declared_size, size_t payload_bytes,
                          bool truncate_header = false) {
    std::vector<unsigned char> b;
    put_tag(b, "RIFF");
    put_u32(b, 0);  // riff size: not trusted by the reader, left slack
    put_tag(b, "WAVE");
    put_tag(b, "fmt ");
    put_u32(b, 16);
    put_u16(b, 1);
    put_u16(b, channels);
    put_u32(b, rate);
    put_u32(b, rate * channels * bits / 8);
    put_u16(b, static_cast<uint16_t>(channels * bits / 8));
    put_u16(b, bits);
    if (!truncate_header) {
        put_tag(b, "data");
        put_u32(b, declared_size);
        for (size_t i = 0; i < payload_bytes; i++) b.push_back(0);
    }
    std::ofstream f(name, std::ios::binary);
    f.write(reinterpret_cast<const char*>(b.data()),
            static_cast<std::streamsize>(b.size()));
    f.close();
    return name;
}

bool throws_on_read(const std::string& path) {
    try {
        nova::read_wav(path);
        return false;
    } catch (const std::exception&) {
        return true;
    }
}

// Read a file that this test expects to be ACCEPTED, and report a refusal
// as a failed check rather than letting it escape.
//
// This exists because of a mutation. Tightening kMinRateHz to 16000 made
// read_wav throw here, the exception left main(), and the process aborted
// with SIGABRT before printing which check had failed — so a mutation that
// the suite genuinely catches looked like a hang, and its attribution was
// lost. A screamer that dies without naming the rule it was defending is
// only marginally better than one that survives [the session-31 rule].
bool read_expecting_success(const std::string& path, nova::Wav& out) {
    try {
        out = nova::read_wav(path);
        return true;
    } catch (const std::exception& e) {
        std::printf("  FAIL unexpected refusal of a good file: %s\n", e.what());
        failures++;
        return false;
    }
}

// A data chunk that claims far more than the file holds. Without the clamp
// this resizes to the DECLARED size and zero-fills it, so the sample count
// is the instrument: 50 MB of 16-bit mono is 26214400 samples, and what is
// really there is 50.
void test_lying_data_chunk() {
    std::printf("- data chunk larger than the file\n");
    const uint32_t declared = 50u * 1024u * 1024u;
    const size_t real_bytes = 100;
    const std::string p =
        write_wav_raw("malformed_liar.wav", 8000, 1, 16, declared, real_bytes);

    nova::Wav w;
    if (read_expecting_success(p, w)) {
        check(w.samples.size() <= real_bytes / 2,
              "declared size is clamped to the bytes that exist");
        check(w.samples.size() != declared / 2,
              "the declared size was not allocated");
    }
    std::remove(p.c_str());
}

// The rates that cannot carry WEFAX must be refused, and the ones that can
// must NOT be — a guard that also rejects real recordings is a worse defect
// than the one it fixes, so both directions are checked.
void test_rate_bounds() {
    std::printf("- implausible sample rates\n");
    struct Case { uint32_t rate; bool want_throw; const char* what; };
    const Case cases[] = {
        {0, true, "0 Hz is refused"},
        {1, true, "1 Hz is refused (the well-formed hang)"},
        {4000, true, "4000 Hz is refused: below twice the 2300 Hz white"},
        {8000, false, "8000 Hz is accepted"},
        {11025, false, "11025 Hz is accepted"},
        {48000, false, "48000 Hz is accepted"},
        {192000, false, "192000 Hz is accepted"},
        {4294967295u, true, "4294967295 Hz is refused"},
    };
    for (const Case& c : cases) {
        const std::string p = write_wav_raw("malformed_rate.wav", c.rate, 1, 16,
                                            200, 200);
        check(throws_on_read(p) == c.want_throw, c.what);
        std::remove(p.c_str());
    }
}

// The ratio guard is checked directly rather than through a file, because
// read_wav is not the only caller and a bound that lives only at the file
// boundary is one new entry point away from being absent.
void test_resample_ratio_bounds() {
    std::printf("- resample ratio bounds\n");
    const std::vector<float> tiny(10, 0.5f);

    bool threw = false;
    try {
        nova::resample_ratio(tiny, 8000.0);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "a ratio of 8000 is refused, not attempted");

    threw = false;
    try {
        nova::resample_ratio(tiny, 1.0 / 100000.0);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "a ratio of 1e-5 is refused");

    // Real use must survive. 6.0 is 8 kHz -> 48 kHz, which the live engine
    // does; 1/6 is the way back; 1.0001 is a +100 ppm clock correction.
    bool ok = true;
    try {
        nova::resample_ratio(tiny, 6.0);
        nova::resample_ratio(tiny, 1.0 / 6.0);
        nova::resample_ratio(tiny, 1.0001);
    } catch (const std::exception&) {
        ok = false;
    }
    check(ok, "6.0, 1/6 and 1.0001 still resample (the guard eats nothing real)");
}

void test_truncation() {
    std::printf("- truncation\n");
    const std::string h =
        write_wav_raw("malformed_trunc_hdr.wav", 8000, 1, 16, 0, 0, true);
    check(throws_on_read(h), "a file that ends before its data chunk is refused");
    std::remove(h.c_str());

    // Declares 50 MB, carries 40 bytes, and must read as 20 samples.
    //
    // The real attack declared 4 GB, and this test deliberately does NOT:
    // every case here has to stay safe to run against a build whose guard
    // has been deliberately removed, which is the only way this file is
    // ever shown to fail. At 4 GB the mutation run would allocate 4 GB and
    // zero-fill it on the machine doing the testing; at 50 MB it allocates,
    // gets the wrong answer, and reports. The defect is the same one — an
    // unclamped 32-bit field — and 50 MB against 40 bytes still exercises
    // it by a factor of 1.3 million.
    const std::string d = write_wav_raw("malformed_trunc_data.wav", 8000, 1, 16,
                                        50u * 1024u * 1024u, 40);
    nova::Wav w;
    if (read_expecting_success(d, w))
        check(w.samples.size() == 20,
              "a 50 MB claim over 40 bytes reads 20 samples");
    std::remove(d.c_str());
}

void test_zero_channels() {
    std::printf("- channel count\n");
    const std::string p =
        write_wav_raw("malformed_chan.wav", 8000, 0, 16, 200, 200);
    check(throws_on_read(p), "0 channels is refused");
    std::remove(p.c_str());
}

}  // namespace

int main() {
    std::printf("=== malformed input [audit Pass D] ===\n");
    test_lying_data_chunk();
    test_rate_bounds();
    test_resample_ratio_bounds();
    test_truncation();
    test_zero_channels();
    std::printf("%s\n", failures ? "FAILED" : "OK");
    return failures ? 1 : 0;
}
