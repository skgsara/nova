// test_png.cpp — §9 screamer 4 [docs/05]: the hand-rolled PNG writer's
// output decodes back to the source pixels under an INDEPENDENT decoder,
// and the file is a valid PNG.
//
// The independent decoder is python3's stdlib: zlib for the inflate (our
// deflate is stored blocks — zlib is the reference implementation reading
// them), binascii for the CRCs, struct for the chunk walk. Nothing in it
// shares a line with live/png.cpp, which is the whole point: a writer
// checked by its own reader is one implementation agreeing with itself.
// python3 is probed first; if it is absent the test exits 77, which the
// ctest SKIP_RETURN_CODE turns into "skipped", not "failed" — the same
// posture as the GUI tests, one dependency lighter.
//
// Claims defended:
//   - byte-exact pixel round-trip, at a real chart width (1810), at a
//     size that spans many stored deflate blocks (1810x2400 = 4.3 MB raw,
//     67 blocks), and at 3x2 (a single short block);
//   - validity as a container: signature, chunk CRCs, IHDR fields, filter
//     byte 0 on every row, IEND last with nothing after it — all asserted
//     by the independent side, not by us;
//   - the tEXt chunks round-trip [docs/05 §8.3 item 7: PNG metadata is the
//     home for Nova's decode QA];
//   - the writer is deterministic: two writes of one image are identical
//     files, because a saved chart that changes bytes between identical
//     runs is a diff nobody can read.
#include "../core/gen.hpp"
#include "../core/image.hpp"
#include "../live/png.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}

// The independent checker, in python3 stdlib. Reads the PNG strictly:
// every chunk's CRC, IHDR against the expected geometry, every row's
// filter byte, IEND last, then the pixels against the raw reference and
// the tEXt chunks against the manifest.
const char* kChecker = R"PY(
import sys, struct, zlib, binascii

png_path, raw_path = sys.argv[1], sys.argv[2]
w, h = int(sys.argv[3]), int(sys.argv[4])
manifest = sys.argv[5]

data = open(png_path, 'rb').read()
assert data[:8] == b'\x89PNG\r\n\x1a\n', "bad signature"
pos = 8
idat = b''
texts = {}
order = []
while pos < len(data):
    ln, = struct.unpack('>I', data[pos:pos+4])
    typ = data[pos+4:pos+8]
    payload = data[pos+8:pos+8+ln]
    crc, = struct.unpack('>I', data[pos+8+ln:pos+12+ln])
    assert crc == binascii.crc32(typ + payload) & 0xffffffff, \
        "CRC mismatch in %s" % typ
    order.append(typ)
    if typ == b'IHDR':
        fields = struct.unpack('>IIBBBBB', payload)
        assert fields == (w, h, 8, 0, 0, 0, 0), "IHDR fields %r" % (fields,)
    elif typ == b'tEXt':
        k, v = payload.split(b'\x00', 1)
        texts[k.decode('latin-1')] = v.decode('latin-1')
    elif typ == b'IDAT':
        idat += payload
    pos += 12 + ln
assert order[0] == b'IHDR', "IHDR not first"
assert order[-1] == b'IEND', "IEND not last / trailing bytes"

raw = zlib.decompress(idat)
assert len(raw) == (w + 1) * h, "decompressed length %d" % len(raw)
px = bytearray()
for y in range(h):
    row = raw[y*(w+1):(y+1)*(w+1)]
    assert row[0] == 0, "row %d filter byte %d" % (y, row[0])
    px += row[1:]
expected = open(raw_path, 'rb').read()
assert bytes(px) == expected, "pixels differ"

for line in open(manifest):
    line = line.rstrip('\n')
    if not line:
        continue
    k, v = line.split('\t', 1)
    assert texts.get(k) == v, "tEXt %r: %r != %r" % (k, texts.get(k), v)
print("independent decode OK")
)PY";

bool write_file(const std::string& path, const std::string& content) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fwrite(content.data(), 1, content.size(), f);
    std::fclose(f);
    return true;
}

bool write_raw(const std::string& path, const nova::Image& img) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::fwrite(img.px.data(), 1, img.px.size(), f);
    std::fclose(f);
    return true;
}

// One round trip under the independent decoder. Returns false when
// python3 itself is absent (the caller then skips), true checked
// otherwise.
void roundtrip(const char* tag, const nova::Image& img,
               const std::vector<nova::PngText>& text) {
    const std::string base = std::string("png_roundtrip_") + tag;
    const std::string png = base + ".png", raw = base + ".raw",
                      manifest = base + ".txt", script = base + ".py";
    nova::write_png(png, img, text);
    write_raw(raw, img);
    std::string man;
    for (const nova::PngText& t : text) man += t.key + "\t" + t.value + "\n";
    write_file(manifest, man);
    write_file(script, kChecker);

    const std::string cmd = "python3 \"" + script + "\" \"" + png +
                            "\" \"" + raw + "\" " + std::to_string(img.width) +
                            " " + std::to_string(img.height) + " \"" +
                            manifest + "\"";
    const int rc = std::system(cmd.c_str());
    std::printf("    %s: %dx%d\n", tag, img.width, img.height);
    check(rc == 0, "independent decoder verifies the file");
}

}  // namespace

int main() {
    // No python3 -> skip (77), not fail: the writer still ships, the
    // independent check is what is missing.
    if (std::system("python3 -c \"import zlib, binascii, struct\" > "
#ifndef _WIN32
                    "/dev/null"
#else
                    "NUL"
#endif
                    " 2>&1") != 0) {
        std::printf("SKIP: python3 not found\n");
        return 77;
    }

    roundtrip("chart", nova::gen_test_pattern(1810, 300),
              {{"Software", "Nova"},
               {"Title", "png_roundtrip chart"},
               {"Phase", "OK"}});
    roundtrip("tall", nova::gen_test_pattern(1810, 2400), {});
    roundtrip("tiny", nova::gen_test_pattern(3, 2), {});

    // Determinism: identical input, identical file.
    const nova::Image img = nova::gen_test_pattern(1810, 64);
    nova::write_png("png_roundtrip_det_a.png", img);
    nova::write_png("png_roundtrip_det_b.png", img);
    auto slurp = [](const char* p) {
        std::FILE* f = std::fopen(p, "rb");
        std::vector<uint8_t> v;
        if (f) {
            uint8_t buf[65536];
            std::size_t n;
            while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
                v.insert(v.end(), buf, buf + n);
            std::fclose(f);
        }
        return v;
    };
    check(slurp("png_roundtrip_det_a.png") == slurp("png_roundtrip_det_b.png"),
          "two writes of one image are byte-identical");

    std::printf("%s (%d failure(s))\n", failures ? "FAILED" : "OK",
                failures);
    return failures ? 1 : 0;
}
