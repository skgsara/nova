// test_overrides.cpp — §9 screamers 5 and 6 [docs/05]: the two operator
// corrections reach the batch decode, and they do NOT behave the same way
// when they get there [docs/05 §7.1, decided session 17].
//
// Why this test is the one that matters for those two fields. Both of them
// can be implemented as a plain override in about a line each, both will
// then look right on the recording the operator is staring at, and only
// one of the two is supposed to work that way. The failure has no symptom
// an operator could report: the picture they corrected by hand comes out
// corrected, which is exactly what they asked for — while every healthy
// recording is quietly drawn on an eyeballed clock instead of a fitted
// one, and nothing anywhere says so. That is a bug that ships.
//
// The two claims, in the words of the decision:
//
//   - **PHASE is a seed the search refines.** Auto-phasing fails by
//     picking the WRONG CANDIDATE for the dead sector, so the operator's
//     click is a statement about WHICH feature — but it was made through a
//     preview drawn on a possibly-wrong period, so it is approximate in
//     position. The decoder therefore starts its anchor search at the hint
//     and settles precisely nearby: the operator's judgement about which,
//     the decoder's precision about where.
//   - **SYNC is a fallback the measurement outranks.** A ppm eyeballed off
//     thirty seconds of preview is worse than one fitted over the whole
//     transmission (sessions 5, 8 and 9 are entirely about long baselines
//     beating short ones), so the fit wins wherever it has a baseline, and
//     the operator's value is used only where it does not — a white-only
//     station, a forced start, too few locked lines.
//
// What each is measured against, and why that form:
//
//   - a hint deliberately placed OFF the true anchor must produce a
//     **byte-identical image** to the decode that was given no hint at
//     all. Byte-identical is the whole point: "close enough" would pass on
//     an implementation that obeys the hint to within a pixel, and a pixel
//     is what the refinement is for;
//   - a hint pointed at a DECOY — a black-then-white feature planted in
//     synthetic picture content, which is exactly the wrong candidate the
//     field exists to disambiguate — must move the picture ONTO the decoy,
//     measured as a cyclic rotation of the whole page by the decoy's own
//     offset. Nothing in the library contains a decoy of known position,
//     so this one is generated;
//   - a deliberately wrong SYNC must leave a fitted recording
//     byte-identical, and BE the clock on one with no fit;
//   - and a SYNC of exactly **0 ppm** must be used, not ignored. That is
//     the NaN sentinel earning its keep: zero cannot mean "auto" here
//     because a perfect clock is 0 ppm, and an implementation that reached
//     for the usual `if (ppm != 0)` idiom passes every other check in this
//     file.
//
// Guarded by nothing: no window, no audio device, no network.
#include "../core/demod.hpp"
#include "../core/fax.hpp"
#include "../core/gen.hpp"
#include "../core/image.hpp"
#include "../core/wav.hpp"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}

void checkf(bool ok, const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    check(ok, buf);
}

std::vector<float> load(const std::string& path) {
    nova::Wav w = nova::read_wav(path);
    return nova::fm_demod(w.samples, w.sample_rate, 1900.0, 400.0);
}

bool same_pixels(const nova::Image& a, const nova::Image& b) {
    return a.width == b.width && a.height == b.height && a.px == b.px;
}

// Per column, the fraction of rows that are dark there. The same statistic
// `stage_dead_sector` anchors on, computed on the finished page instead of
// on the video — which makes it the natural thing to compare two pages by:
// it is what an operator sees as "the vertical features", and it does not
// care that two decodes of the same signal disagree about a grey level
// here and there.
std::vector<double> column_darkness(const nova::Image& img) {
    std::vector<double> f(img.width, 0.0);
    for (int y = 0; y < img.height; y++)
        for (int x = 0; x < img.width; x++)
            if (img.px[static_cast<size_t>(y) * img.width + x] < 64)
                f[x] += 1.0;
    for (double& v : f) v /= std::max(1, img.height);
    return f;
}

// The cyclic shift, in columns, that best takes `a`'s column profile onto
// `b`'s. Cyclic because the anchor IS the line start: moving it rotates
// the picture rather than sliding it off the paper.
int best_rotation(const nova::Image& a, const nova::Image& b) {
    const std::vector<double> fa = column_darkness(a);
    const std::vector<double> fb = column_darkness(b);
    const int w = a.width;
    int best_at = 0;
    double best = -std::numeric_limits<double>::infinity();
    for (int s = 0; s < w; s++) {
        double sum = 0.0;
        for (int x = 0; x < w; x++) {
            const double d = fa[(x + s) % w] - fb[x];
            sum -= d * d;
        }
        if (sum > best) {
            best = sum;
            best_at = s;
        }
    }
    return best_at;
}

// --- 5. PHASE: a seed the search refines ----------------------------------

// A hint placed near but not on the true anchor must land the picture on
// the TRUE anchor. Run on a station that sends a black pulse (where the
// per-line template is the strong witness) and on one that does not (where
// the profile is all there is), because the two take different paths
// through `stage_dead_sector` and only one of them was ever going to be
// remembered.
void test_hint_refines(const std::string& path, const char* what,
                       double true_frac) {
    std::printf("PHASE hint refines to the true anchor — %s\n", what);
    const std::vector<float> video = load(path);
    nova::DecodeOptions plain;
    const nova::DecodeResult base = nova::decode_fax(video, 8000, plain);
    check(!base.anchor_from_hint,
          "a decode given no hint reports none (the default is not a value)");

    // ±1% of a line either side: 40 samples on a 4000-sample line, which is
    // 18 px of 1810 — a plausible click error, and well inside the ±3%
    // window the refinement is allowed.
    bool all_identical = true;
    int landed = 0;
    for (const double off : {-0.010, -0.005, 0.005, 0.010}) {
        nova::DecodeOptions opt;
        opt.phase_anchor_hint = true_frac + off;
        const nova::DecodeResult r = nova::decode_fax(video, 8000, opt);
        if (!r.anchor_from_hint) all_identical = false;
        if (!same_pixels(base.img, r.img)) all_identical = false;
        else landed++;
    }
    checkf(all_identical,
           "%d/4 hints ±1%% of a line off the anchor draw a BYTE-IDENTICAL "
           "page to the un-hinted decode",
           landed);
}

// The half that says the operator is deciding WHICH feature, not where.
// Synthetic, because it needs a wrong candidate whose position is known to
// the sample: a black run followed immediately by white, planted in the
// picture content on every line, which is precisely the shape
// `fax_pulse_score` hunts for [WMO §5.1.3.3] and precisely the thing that
// makes auto-phasing pick wrong on real charts.
void test_hint_moves_to_the_decoy() {
    std::printf("PHASE hint at a decoy moves the picture ONTO the decoy\n");
    // Content: white paper with one black bar a fifth of the way across,
    // present on two rows in every three. That is the wrong candidate as
    // `stage_dead_sector` itself describes it — "a chart border is dark on
    // many lines, never on all of them" — and the two-in-three is what
    // makes it lose the global scan to the real dead sector while staying a
    // genuine black-then-white feature the template can lock onto. Paper is
    // white rather than grey so that the real dead sector's own white half
    // scores full marks and wins on merit rather than on a tie.
    const int cw = 1810, ch = 200;
    nova::Image content;
    content.width = cw;
    content.height = ch;
    content.px.assign(static_cast<size_t>(cw) * ch, 255);
    const int decoy_x = cw / 5, decoy_w = cw / 40;
    for (int y = 0; y < ch; y++) {
        if (y % 3 == 0) continue;
        for (int x = decoy_x; x < decoy_x + decoy_w; x++)
            content.px[static_cast<size_t>(y) * cw + x] = 0;
    }

    nova::GenOptions g;
    g.noise = 0.0;
    // The generator's default pulse is 1.5% of the line and the template
    // reads 2.25% [core/fax.hpp kFaxPulseFrac]; matching them keeps this
    // test about which feature was chosen, not about how well the real one
    // happens to fit the template.
    g.pulse_frac = 0.0225;
    const std::vector<float> sig = nova::gen_fax_signal(content, ch, g);
    const std::vector<float> video =
        nova::fm_demod(sig, g.fs, 1900.0, g.deviation);

    nova::DecodeOptions plain;
    const nova::DecodeResult base = nova::decode_fax(video, g.fs, plain);
    check(base.dead_sector == nova::DeadSector::kBlackPulse &&
              !base.anchor_from_hint,
          "un-hinted, the decoder anchors itself on the real dead sector");

    // Where the decoy ended up on the un-hinted page, measured rather than
    // predicted: the leading edge of the only dark thing that is not the
    // dead sector at the left margin. That column is what the hint should
    // rotate to zero.
    const std::vector<double> dark = column_darkness(base.img);
    int decoy_col = -1;
    for (int x = 100; x < base.img.width; x++)
        if (dark[x] > 0.4) {
            decoy_col = x;
            break;
        }
    checkf(decoy_col > 0, "the decoy is on the un-hinted page, at column %d",
           decoy_col);

    // Where the decoy sits in the drawn line: the picture sector begins one
    // dead sector in, so a content column maps to `dead + (1-dead)*x/cw` of
    // the line. Same arithmetic the generator laid it down with.
    const double decoy_frac =
        g.dead_frac + (1.0 - g.dead_frac) * decoy_x / static_cast<double>(cw);
    nova::DecodeOptions opt;
    opt.phase_anchor_hint = decoy_frac;
    const nova::DecodeResult hinted = nova::decode_fax(video, g.fs, opt);
    check(hinted.anchor_from_hint, "the hint is reported as what anchored it");

    const int got = best_rotation(base.img, hinted.img);
    // Wrapped difference: a rotation of w-1 and one of -1 are the same
    // rotation, and the decoy can land either side of the boundary.
    int err = got - decoy_col;
    while (err > base.img.width / 2) err -= base.img.width;
    while (err < -base.img.width / 2) err += base.img.width;
    checkf(std::abs(err) <= 8,
           "the page is rotated onto the decoy: %d px, the decoy's own "
           "column is %d (%+d)",
           got, decoy_col, err);
    check(!same_pixels(base.img, hinted.img),
          "...which is to say the hint changed the picture at all");
    // And the operator's click did NOT re-decide what kind of station this
    // is. A hint that could flip `per_line_sync` would silently switch the
    // per-line tracker off — a consequence nobody clicked for.
    check(hinted.dead_sector == nova::DeadSector::kBlackPulse &&
              hinted.per_line_sync,
          "a hint cannot turn a pulse station into a white-only one");
}

// The hint has to outrank the phasing anchor, or it works everywhere
// except on the recordings that need it. A white-only station WITH a
// phasing interval is the case: `stage_phasing` overwrites the image
// anchor there, and if it overwrites the operator's too, the field is
// silently dead on exactly the transmissions where auto-phasing is the
// thing being corrected.
void test_hint_outranks_phasing(const std::string& path) {
    std::printf("PHASE hint outranks the automatic phasing anchor\n");
    const std::vector<float> video = load(path);
    nova::DecodeOptions plain;
    const nova::DecodeResult base = nova::decode_fax(video, 8000, plain);
    check(base.anchor_from_phasing && !base.per_line_sync,
          "the fixture is the case: white-only, and phased from its phasing "
          "interval");

    nova::DecodeOptions opt;
    opt.phase_anchor_hint = 0.20;  // nowhere near the phasing anchor
    const nova::DecodeResult r = nova::decode_fax(video, 8000, opt);
    check(r.anchor_from_hint && !r.anchor_from_phasing,
          "the hint is used, and the phasing anchor stands down");
    check(!same_pixels(base.img, r.img),
          "the picture really moved (the hint is not merely reported)");
    check(r.phasing_found && r.phasing_anchor_delta != 0.0,
          "...and the phasing anchor is still measured and reported, so the "
          "two answers can still be compared");

    // The refinement converges: several clicks spread across ±1.7% of a
    // line settle on ONE position, and that position is within a few tens
    // of samples of the independent phasing witness. This is the strongest
    // statement available about a white-only station, where the design note
    // warned there is no per-line phase to refine against at all.
    std::vector<int> shifts;
    for (const double f : {0.710, 0.720, 0.728, 0.735, 0.745}) {
        nova::DecodeOptions o;
        o.phase_anchor_hint = f;
        const nova::DecodeResult h = nova::decode_fax(video, 8000, o);
        shifts.push_back(best_rotation(base.img, h.img));
    }
    int lo = shifts[0], hi = shifts[0];
    for (const int s : shifts) {
        lo = std::min(lo, s);
        hi = std::max(hi, s);
    }
    checkf(hi - lo <= 2,
           "5 clicks spanning 3.5%% of a line all land on the same anchor "
           "(spread %d px of %d)",
           hi - lo, base.img.width);
    // `base` is the phasing-anchored page, so the rotation between them IS
    // the disagreement between the operator's refined answer and the
    // independent one. 13 px of 1810 when this was written.
    int d = shifts[0];
    if (d > base.img.width / 2) d -= base.img.width;
    checkf(std::abs(d) <= 25,
           "and it lands %d px from the phasing anchor, which never saw the "
           "hint",
           d);
}

// --- 6. SYNC: a fallback the measurement outranks -------------------------

void test_sync_outranked(const std::string& path, const char* what) {
    std::printf("SYNC is outranked where the fit has a baseline — %s\n", what);
    const std::vector<float> video = load(path);
    nova::DecodeOptions plain;
    const nova::DecodeResult base = nova::decode_fax(video, 8000, plain);
    check(base.per_line_sync && base.locked_lines > 0 &&
              !base.clock_from_fallback,
          "the fixture is the case: locked lines, so a baseline to fit over");

    bool all_ignored = true;
    for (const double ppm : {-2000.0, -100.0, 0.0, 100.0, 2000.0}) {
        nova::DecodeOptions opt;
        opt.clock_ppm_fallback = ppm;
        const nova::DecodeResult r = nova::decode_fax(video, 8000, opt);
        if (r.clock_from_fallback) all_ignored = false;
        if (r.clock_ppm != base.clock_ppm) all_ignored = false;
        if (!same_pixels(base.img, r.img)) all_ignored = false;
    }
    checkf(all_ignored,
           "5 deliberately wrong ppm values (±2000 included) change NOTHING: "
           "same clock %+.1f ppm, byte-identical page",
           base.clock_ppm);
}

void test_sync_used(const std::string& path, const char* what) {
    std::printf("SYNC is the clock where the fit has no baseline — %s\n", what);
    const std::vector<float> video = load(path);
    nova::DecodeOptions plain;
    const nova::DecodeResult base = nova::decode_fax(video, 8000, plain);
    check(!base.per_line_sync && base.locked_lines == 0 &&
              !base.clock_from_fallback,
          "the fixture is the case: no per-line sync, so nothing to fit");

    bool all_used = true;
    for (const double ppm : {-2000.0, -100.0, 100.0, 2000.0}) {
        nova::DecodeOptions opt;
        opt.clock_ppm_fallback = ppm;
        const nova::DecodeResult r = nova::decode_fax(video, 8000, opt);
        if (!r.clock_from_fallback) all_used = false;
        if (std::fabs(r.clock_ppm - ppm) > 1e-6) all_used = false;
        // The value is a rate, so it must reach the drawn geometry, not
        // just the report: the line period is nominal scaled by it.
        const double nominal_s = 60.0 / base.lpm;
        if (std::fabs(r.line_period_s - nominal_s * (1.0 + ppm * 1e-6)) >
            1e-9)
            all_used = false;
        if (same_pixels(base.img, r.img)) all_used = false;
    }
    check(all_used,
          "4 ppm values are each the reported clock, the drawn line period "
          "AND a different page");

    // The sentinel claim. 0 ppm is a legal clock — a station whose rate is
    // exactly right — so it cannot double as "no value given". An
    // implementation using zero as the absent-marker passes everything
    // above and fails here, which is the whole reason the default is NaN.
    nova::DecodeOptions zero;
    zero.clock_ppm_fallback = 0.0;
    const nova::DecodeResult z = nova::decode_fax(video, 8000, zero);
    checkf(z.clock_from_fallback && z.clock_ppm == 0.0 &&
               !same_pixels(base.img, z.img),
           "a SYNC of exactly 0 ppm is USED, not read as 'no value': it "
           "replaces the %+.1f ppm the fold measured, and redraws the page",
           base.clock_ppm);
}

}  // namespace

int main(int argc, char** argv) {
    // usage: nova-test-overrides MODE pulse.wav white.wav phasing.wav
    if (argc < 5) {
        std::fprintf(stderr,
                     "usage: %s phase|sync PULSE.wav WHITE.wav PHASING.wav\n",
                     argv[0]);
        return 2;
    }
    const std::string mode = argv[1];
    const std::string pulse = argv[2], white = argv[3], phasing = argv[4];

    if (mode == "phase") {
        // The true anchors, as fractions of the line, measured from the
        // un-hinted decode of each fixture when this test was written:
        // 92/3999 on the pulse station, 3645/3999 on the white-only one.
        test_hint_refines(pulse, "a station that sends a black pulse", 0.0230);
        test_hint_refines(white, "a white-only station, no per-line sync",
                          0.9115);
        test_hint_moves_to_the_decoy();
        test_hint_outranks_phasing(phasing);
    } else if (mode == "sync") {
        test_sync_outranked(pulse, "a pulse station, 117 of 120 lines locked");
        test_sync_used(white, "a white-only station");
    } else {
        std::fprintf(stderr, "unknown mode %s\n", mode.c_str());
        return 2;
    }

    std::printf("%s (%d failure(s))\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
