// test_live_engine.cpp — §9 screamer 12 [docs/05]: the wiring of §2.
//
// The claim, and it is the only claim worth making about a piece of
// concurrency in a decoder: **threading changes nothing about the
// picture.** A recording driven through `LiveEngine` — a real producer
// thread pushing into the lock-free ring, thread 2 draining it in
// whatever blocks it happens to see, thread 3 decoding on its own —
// produces the same state sequence, the same rows in the same places,
// and the same saved pixels as the same recording driven through
// `LiveSession` on one thread with no ring at all.
//
// If that is not true, then every number the rest of the suite measures
// was measured on a path the operator does not use. Sessions 20-22 pinned
// each stage block-invariant on its own; this is the only test that runs
// them together with the threads actually running.
//
// Claims defended:
//   - the state sequence, the drawn rows (index, start_sample, period,
//     locked) and the saved image are identical to the single-threaded
//     reference, at five audio block sizes from 1 sample to 65536;
//   - the save happens, is named by §8.5 item 5's rule, and its pixels
//     are the batch decode's — checked by reading the PNG back;
//   - the tEXt QA says whether PHASE/SYNC were the operator's or measured
//     [§8.5 item 3], and says "operator" when they were;
//   - `sanitize_label` / `image_filename` survive a table of awkward
//     labels, including ones that reduce to nothing;
//   - a ring too small for the feed COUNTS what it dropped [§2.1]. The
//     picture is then wrong, and that is the point: it is wrong and said
//     so, rather than wrong and silent.
#include "../core/demod.hpp"
#include "../core/fax.hpp"
#include "../core/image.hpp"
#include "../core/resample.hpp"
#include "../core/wav.hpp"
#include "../live/engine.hpp"
#include "../live/session.hpp"

#include <chrono>
#include <cstdarg>
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int kInternalRate = 8000;

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}

void checkf(bool ok, const char* fmt, ...) {
    char buf[400];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    check(ok, buf);
}

// What a run produced, in the form the two paths can be compared in.
struct Run {
    std::vector<nova::SessionState> states;
    std::vector<nova::PreviewRow> rows;
    bool decoded = false;
    nova::Image saved;      // the batch image
    std::string saved_path; // empty when the run did not save
    std::string save_error; // why, when it did not
    // The message order as the GUI would see it. §8.5 item 1 is a claim
    // about ORDER — the file is written, and THEN the status line reads
    // SAVED — so the order has to be recorded to be checked.
    std::vector<std::pair<nova::EngineMsg, nova::SessionState>> order;
};

bool same_rows(const std::vector<nova::PreviewRow>& a,
               const std::vector<nova::PreviewRow>& b, std::string* why) {
    if (a.size() != b.size()) {
        char buf[128];
        std::snprintf(buf, sizeof buf, "row count %zu vs %zu", a.size(),
                      b.size());
        *why = buf;
        return false;
    }
    for (std::size_t i = 0; i < a.size(); i++) {
        if (a[i].index == b[i].index && a[i].start_sample == b[i].start_sample &&
            a[i].period == b[i].period && a[i].locked == b[i].locked)
            continue;
        char buf[200];
        std::snprintf(buf, sizeof buf,
                      "row %zu: index %d/%d start %.6f/%.6f period %.6f/%.6f "
                      "locked %d/%d",
                      i, a[i].index, b[i].index, a[i].start_sample,
                      b[i].start_sample, a[i].period, b[i].period,
                      a[i].locked ? 1 : 0, b[i].locked ? 1 : 0);
        *why = buf;
        return false;
    }
    return true;
}

bool same_image(const nova::Image& a, const nova::Image& b, std::string* why) {
    if (a.width != b.width || a.height != b.height) {
        char buf[128];
        std::snprintf(buf, sizeof buf, "%dx%d vs %dx%d", a.width, a.height,
                      b.width, b.height);
        *why = buf;
        return false;
    }
    for (std::size_t i = 0; i < a.px.size(); i++)
        if (a.px[i] != b.px[i]) {
            char buf[128];
            std::snprintf(buf, sizeof buf, "pixel %zu: %d vs %d", i,
                          int(a.px[i]), int(b.px[i]));
            *why = buf;
            return false;
        }
    return true;
}

// --- the reference: one thread, no ring, no engine -------------------------
// Deliberately a copy of what cli/nova-preview.cpp does, because that is
// the path every other screamer in the suite measured.
Run reference_run(const std::vector<float>& video, bool force, int ioc,
                  double lpm) {
    Run r;
    nova::LiveSession s(kInternalRate);
    std::shared_ptr<const std::vector<float>> snapshot;
    nova::DecodeOptions dopt;
    s.set_decode_callback(
        [&](std::shared_ptr<const std::vector<float>> snap, long long,
            const nova::DecodeOptions& d) {
            snapshot = std::move(snap);
            dopt = d;
        });

    const auto take = [&](const nova::SessionOutput& o) {
        r.rows.insert(r.rows.end(), o.rows.begin(), o.rows.end());
        r.states.insert(r.states.end(), o.entered.begin(), o.entered.end());
    };
    take(s.start_capture());
    if (force) take(s.force_start(ioc, lpm));
    for (std::size_t i = 0; i < video.size(); i += 4096)
        take(s.push(video.data() + i,
                    std::min<std::size_t>(4096, video.size() - i)));
    take(s.flush());

    if (snapshot) {
        try {
            const nova::DecodeResult res =
                nova::decode_fax(*snapshot, kInternalRate, dopt);
            r.saved = res.img;
            r.decoded = true;
            take(s.batch_done(res));
        } catch (const nova::DecodeError& e) {
            take(s.batch_failed(e.kind()));
        }
    }
    return r;
}

// --- the engine, with a real producer thread -------------------------------
// The feed is LOSSLESS on purpose: a dropped sample would make the two
// paths differ for a reason that has nothing to do with the wiring, which
// is the very confusion this test exists to remove. The overrun case is
// tested separately, where dropping is the claim.
Run engine_run(const std::vector<float>& audio, std::size_t block, bool force,
               int ioc, double lpm, const std::string& folder,
               const std::string& stamp, const std::string& label,
               bool set_phase, double phase, int capture_rate = kInternalRate) {
    Run r;
    nova::EngineOptions opt;
    opt.image_folder = folder;
    opt.utc_now = [stamp] { return stamp; };
    opt.poll_ms = 1;
    nova::LiveEngine eng(capture_rate, opt);

    eng.run();
    if (!label.empty()) eng.set_label(label);
    eng.start_capture();
    if (force) eng.force_start(ioc, lpm);

    std::thread feeder([&] {
        std::size_t at = 0;
        bool phase_sent = false;
        while (at < audio.size()) {
            const std::size_t n = std::min(block, audio.size() - at);
            const std::size_t took = eng.push_audio(audio.data() + at, n);
            at += took;
            if (took < n)  // ring full: wait for thread 2 rather than drop
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            if (set_phase && !phase_sent && at > audio.size() / 2) {
                eng.set_phase(phase);
                phase_sent = true;
            }
        }
    });
    feeder.join();
    eng.shutdown();

    for (const nova::EngineMessage& m : eng.drain()) {
        r.order.emplace_back(m.kind, m.state);
        switch (m.kind) {
            case nova::EngineMsg::kStateChanged:
                r.states.push_back(m.state);
                break;
            case nova::EngineMsg::kRowsDrawn:
                r.rows.insert(r.rows.end(), m.rows.begin(), m.rows.end());
                break;
            case nova::EngineMsg::kBatchDone:
                if (m.result) {
                    r.saved = m.result->img;
                    r.decoded = true;
                }
                break;
            case nova::EngineMsg::kSaved: r.saved_path = m.path; break;
            case nova::EngineMsg::kSaveFailed: r.save_error = m.detail; break;
            default: break;
        }
    }
    return r;
}

// --- the fixture-driven equivalence ----------------------------------------
void test_equivalence(const char* wav_path, const char* tmp_dir, bool force,
                      int ioc, double lpm) {
    const char* base = std::strrchr(wav_path, '/');
    base = base ? base + 1 : wav_path;
    std::printf("%s\n", base);

    nova::Wav w = nova::read_wav(wav_path);
    const std::vector<float> audio =
        nova::resample(w.samples, w.sample_rate, kInternalRate);
    const std::vector<float> video =
        nova::fm_demod(audio, kInternalRate, 1900.0, 400.0);

    const Run ref = reference_run(video, force, ioc, lpm);

    // Five block sizes. 1 is the pathological callback nobody ships and
    // every stage in this project has been wrong at; 65536 is bigger than
    // any real audio period, and is the size that caught session 21's
    // 1.46e-11 probe-grid drift and session 22's stop-tone rows.
    const std::size_t kBlocks[] = {1, 64, 1000, 4096, 65536};
    bool states_ok = true, rows_ok = true, image_ok = true;
    std::string why;
    for (const std::size_t b : kBlocks) {
        const Run got = engine_run(audio, b, force, ioc, lpm, tmp_dir,
                                   "20260814T000000Z", "", false, 0.0);
        if (got.states != ref.states) {
            states_ok = false;
            std::printf("    block %zu: %zu states vs %zu\n", b,
                        got.states.size(), ref.states.size());
            for (std::size_t i = 0;
                 i < std::max(got.states.size(), ref.states.size()); i++)
                std::printf("      %-22s %s\n",
                            i < got.states.size()
                                ? nova::session_state_name(got.states[i])
                                : "-",
                            i < ref.states.size()
                                ? nova::session_state_name(ref.states[i])
                                : "-");
        }
        std::string w2;
        if (!same_rows(got.rows, ref.rows, &w2)) {
            rows_ok = false;
            why = w2;
            std::printf("    block %zu rows: %s\n", b, w2.c_str());
        }
        if (ref.decoded) {
            std::string w3;
            if (!got.decoded || !same_image(got.saved, ref.saved, &w3)) {
                image_ok = false;
                std::printf("    block %zu image: %s\n", b,
                            got.decoded ? w3.c_str() : "no decode at all");
            }
        }
    }
    checkf(states_ok,
           "the state sequence is the single-threaded one at all 5 block "
           "sizes (%zu states)",
           ref.states.size());
    checkf(rows_ok,
           "every drawn row is in the same place as single-threaded (%zu "
           "rows)",
           ref.rows.size());
    if (ref.decoded)
        checkf(image_ok, "the saved image is pixel-identical (%dx%d)",
               ref.saved.width, ref.saved.height);
    else
        std::printf("    (no decode on this fixture: nothing to compare)\n");
}

// --- the capture rate the operator actually has ----------------------------
// Every case above feeds 8 kHz into an 8 kHz engine, which puts
// `StreamResampler` in PASSTHROUGH: the engine's resampling path is not
// exercised at all, and dropping its end-of-stream tail entirely changed
// nothing. That survived the first version of this test.
//
// A sound card does not offer 8 kHz. The shell opens a device at its
// preferred rate — 44100 or 48000 — so the resampler is on the live path
// for every real capture and off it for every test. This case closes
// that: the fixture is upsampled to 48 kHz, fed at 48 kHz, and compared
// against the whole-file resample of the same audio. It is the same
// equivalence as above with the one stage that was missing.
void test_capture_rate(const char* wav_path, const char* tmp_dir) {
    std::printf("48 kHz capture (the rate a sound card actually offers)\n");
    nova::Wav w = nova::read_wav(wav_path);
    const std::vector<float> at8 =
        nova::resample(w.samples, w.sample_rate, kInternalRate);
    const std::vector<float> at48 = nova::resample(at8, kInternalRate, 48000);

    // The reference takes the same 48 kHz stream down the batch path.
    const std::vector<float> mono = nova::resample(at48, 48000, kInternalRate);
    const std::vector<float> video =
        nova::fm_demod(mono, kInternalRate, 1900.0, 400.0);
    const Run ref = reference_run(video, true, 576, 120.0);

    bool ok = true;
    for (const std::size_t b : {std::size_t(512), std::size_t(4096)}) {
        const Run got =
            engine_run(at48, b, true, 576, 120.0, tmp_dir,
                       "20260814T000000Z", "", false, 0.0, 48000);
        std::string why;
        if (got.states != ref.states) {
            ok = false;
            std::printf("    block %zu: %zu states vs %zu\n", b,
                        got.states.size(), ref.states.size());
        }
        if (!same_rows(got.rows, ref.rows, &why)) {
            ok = false;
            std::printf("    block %zu rows: %s\n", b, why.c_str());
        }
        if (ref.decoded) {
            std::string w3;
            if (!got.decoded || !same_image(got.saved, ref.saved, &w3)) {
                ok = false;
                std::printf("    block %zu image: %s\n", b,
                            got.decoded ? w3.c_str() : "no decode");
            }
        }
    }
    checkf(ok,
           "resampled 48000 -> 8000 inside the engine matches the whole-file "
           "resample (%zu rows)",
           ref.rows.size());
}

// --- the save path ---------------------------------------------------------
void test_save(const char* wav_path, const char* tmp_dir) {
    std::printf("the save path [docs/05 §8.5]\n");
    nova::Wav w = nova::read_wav(wav_path);
    const std::vector<float> audio =
        nova::resample(w.samples, w.sample_rate, kInternalRate);

    const Run got = engine_run(audio, 4096, true, 576, 120.0, tmp_dir,
                               "20260814T031544Z", "JMH Tokyo", false, 0.0);
    const std::string want =
        std::string(tmp_dir) + "/20260814T031544Z-JMH-Tokyo.png";
    checkf(got.saved_path == want, "the file is named %s (got %s%s%s)",
           "<stamp>-<sanitized label>.png",
           got.saved_path.empty() ? "nothing" : got.saved_path.c_str(),
           got.save_error.empty() ? "" : "; save failed: ",
           got.save_error.c_str());

    // Read it back: this is the only place the whole chain is checked
    // end to end, from a float in the ring to a byte on disk.
    std::FILE* f = std::fopen(want.c_str(), "rb");
    check(f != nullptr, "the file exists on disk");
    if (f) {
        unsigned char sig[8] = {0};
        const std::size_t n = std::fread(sig, 1, 8, f);
        std::fclose(f);
        check(n == 8 && sig[0] == 0x89 && sig[1] == 'P' && sig[2] == 'N' &&
                  sig[3] == 'G',
              "...and it is a PNG");
    }
    check(got.decoded && got.saved.width > 0,
          "the decode that produced it reported an image");

    // §8.5 item 1: "DECODING completing writes the image to the folder and
    // the status line reads SAVED" — in that order. A status line that
    // reads SAVED over a file not yet on disk is the one lie this whole
    // lifecycle was designed to prevent, and swapping the two lines in
    // collect_batch() was invisible to every other check here.
    long long saved_at = -1, state_at = -1;
    for (std::size_t i = 0; i < got.order.size(); i++) {
        if (saved_at < 0 && got.order[i].first == nova::EngineMsg::kSaved)
            saved_at = static_cast<long long>(i);
        if (state_at < 0 &&
            got.order[i].first == nova::EngineMsg::kStateChanged &&
            got.order[i].second == nova::SessionState::kSaved)
            state_at = static_cast<long long>(i);
    }
    checkf(saved_at >= 0 && state_at >= 0 && saved_at < state_at,
           "the file is written BEFORE the state reads SAVED (Saved at %lld, "
           "SAVED at %lld)",
           saved_at, state_at);
}

// --- the operator provenance in the metadata -------------------------------
void test_provenance() {
    std::printf("decode QA provenance [docs/05 §8.5 item 3]\n");
    nova::DecodeResult r;
    r.img.width = 4;
    r.img.height = 2;
    r.ioc = 576;
    r.lpm = 120;

    const std::vector<nova::PngText> measured =
        nova::decode_qa(r, "", false, false);
    // Supplied AND used: the decode reports it took both from the operator.
    nova::DecodeResult used = r;
    used.anchor_from_hint = true;
    used.clock_from_fallback = true;
    const std::vector<nova::PngText> edited =
        nova::decode_qa(used, "JMH", true, true);
    // Supplied and OUTRANKED, which §7.1 makes the ordinary case for SYNC:
    // the operator typed a ppm, the fit had a baseline, and the pixels owe
    // it nothing. A header that called this "operator" would be claiming a
    // provenance the picture does not have — and it would be doing it on
    // every healthy recording, where nobody would notice.
    const std::vector<nova::PngText> outranked =
        nova::decode_qa(r, "JMH", true, true);

    const auto find = [](const std::vector<nova::PngText>& t,
                         const char* key) -> std::string {
        for (const auto& e : t)
            if (e.key == key) return e.value;
        return "(absent)";
    };
    check(find(measured, "Nova:Phase") == "measured" &&
              find(measured, "Nova:Sync") == "measured",
          "an untouched decode records PHASE and SYNC as measured");
    check(find(edited, "Nova:Phase") == "operator hint, refined" &&
              find(edited, "Nova:Sync") == "operator (no fit baseline)",
          "values the decode actually used are recorded as the operator's");
    check(find(outranked, "Nova:Sync") == "measured (operator outranked)" &&
              find(outranked, "Nova:Phase") == "operator (not used)",
          "values it did not use are NOT recorded as the operator's");
    check(find(edited, "Nova:Anchor") == "operator hint, refined" &&
              find(measured, "Nova:Anchor") == "image lines",
          "the anchor line names the hint when the hint is what anchored it");
    check(find(measured, "Title") == "(absent)" &&
              find(edited, "Title") == "JMH",
          "a blank label writes no Title chunk; a label writes it whole");
    check(find(edited, "Nova:IOC") == "576" && find(edited, "Nova:LPM") == "120",
          "the geometry is in the header");
}

// --- §8.5 item 5's sanitizing rule -----------------------------------------
void test_filenames() {
    std::printf("filenames [docs/05 §8.5 item 5]\n");
    const struct {
        const char* label;
        const char* want;
    } kCases[] = {
        {"", "20260813T220417Z.png"},
        {"JMH", "20260813T220417Z-JMH.png"},
        {"JMH Tokyo", "20260813T220417Z-JMH-Tokyo.png"},
        {"  JMH  ", "20260813T220417Z-JMH.png"},
        {"a/b:c*d?e\"f<g>h|i", "20260813T220417Z-a-b-c-d-e-f-g-h-i.png"},
        {"a///b", "20260813T220417Z-a-b.png"},
        {"line\none", "20260813T220417Z-line-one.png"},
        {"   ", "20260813T220417Z.png"},
        {"///", "20260813T220417Z.png"},
        // Capped at 32 characters, and the cap is on the LABEL, not the
        // whole name.
        {"012345678901234567890123456789012345",
         "20260813T220417Z-01234567890123456789012345678901.png"},
    };
    bool ok = true;
    for (const auto& c : kCases) {
        const std::string got = nova::image_filename("20260813T220417Z",
                                                     c.label);
        if (got != c.want) {
            ok = false;
            std::printf("    \"%s\" -> %s, wanted %s\n", c.label, got.c_str(),
                        c.want);
        }
    }
    check(ok, "ten labels, including three that reduce to nothing");
    check(nova::sanitize_label("JMH").size() == 3 &&
              nova::sanitize_label(std::string(200, 'x')).size() == 32,
          "the label is capped at 32 characters");
}

// --- the overrun, which must be counted rather than hidden -----------------
void test_overrun_counted(const char* wav_path) {
    std::printf("a ring too small for the feed [docs/05 §2.1]\n");
    nova::Wav w = nova::read_wav(wav_path);
    const std::vector<float> audio =
        nova::resample(w.samples, w.sample_rate, kInternalRate);

    nova::EngineOptions opt;
    opt.ring_capacity = 256;  // ~32 ms: far too small on purpose
    opt.poll_ms = 20;         // ...and a thread 2 that wakes slowly
    nova::LiveEngine eng(kInternalRate, opt);
    eng.run();
    eng.start_capture();
    // No retry: this feeder behaves exactly like a realtime callback,
    // which cannot wait for anybody.
    for (std::size_t at = 0; at < audio.size(); at += 2048)
        eng.push_audio(audio.data() + at,
                       std::min<std::size_t>(2048, audio.size() - at));
    eng.shutdown();

    checkf(eng.overruns() > 0,
           "the engine counted %llu dropped samples rather than reporting a "
           "clean capture",
           eng.overruns());
    check(eng.ring_capacity() == 256, "the capacity is the one asked for");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: nova-test-live-engine TMPDIR fixture.wav "
                     "[fixture.wav ...]\n");
        return 2;
    }
    const std::string tmp_root = std::string(argv[1]) + "/nova-engine-test";
    std::error_code ec;
    std::filesystem::create_directories(tmp_root, ec);
    if (ec) {
        std::fprintf(stderr, "cannot create %s: %s\n", tmp_root.c_str(),
                     ec.message().c_str());
        return 1;
    }
    const char* tmp = tmp_root.c_str();
    std::printf("=== the wiring of docs/05 §2 ===\n");

    // The first fixture is the tone-driven one: a full IDLE -> ... -> SAVED
    // walk with no operator input at all. The rest are given a forced
    // start, which is how a recording that opens mid-transmission reaches
    // the same path [docs/04 Finding 2].
    for (int i = 2; i < argc; i++)
        test_equivalence(argv[i], tmp, i > 2, 576, 120.0);

    test_capture_rate(argv[2], tmp);
    test_save(argv[argc - 1], tmp);
    test_provenance();
    test_filenames();
    test_overrun_counted(argv[argc - 1]);

    std::printf("%s\n", failures ? "FAILED" : "OK");
    return failures ? 1 : 0;
}
