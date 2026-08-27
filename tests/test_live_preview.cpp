// test_live_preview.cpp — §9 screamer 3 [docs/05]: the provisional
// renderer (live/preview.hpp) draws the picture forward-only, and the
// picture does not depend on how the audio callback chunked the stream.
//
// Why M4 owes this test. This is the first thing in Nova that an operator
// looks AT rather than reads about, and almost everything that can be
// wrong with it is invisible to the rest of the suite: the saved image is
// produced by the batch path, so a preview that draws the page rotated,
// slanted, torn, or dependent on the sound card's buffer size would pass
// all 25 other tests and still be the only thing the operator saw for the
// twenty minutes the chart was arriving.
//
// Claims defended, starting with the three §9 names:
//
//   - **expected dimensions** — the drawn width is the IOC's (1810 at
//     576, 905 at 288), and the row count is the one the working geometry
//     predicts from the video it was given, not whatever fell out;
//   - **the dead-sector edge lands where the batch path puts it**, within
//     a tolerance stated per ANCHOR CLASS — see the note on `AnchorClass`
//     below, which is where this test says something the design document
//     did not. The two implementations share the sync template
//     (`fax_best_sync`, promoted to core/fax.hpp this session) and share
//     nothing else — not the period estimator, not the anchor search, not
//     the assembly — so agreement about which column the dead sector is in
//     is two independent answers to the same question;
//   - **the image is bit-identical whatever the block size**, at every
//     size from one sample to 65536. This is the real claim. A preview
//     that depends on the callback's chunking is broken, and the failure
//     would be untraceable in the field because no two capture sessions
//     chunk alike.
//
// Four more the writing of it turned out to need:
//
//   - **the renderer cannot look back** — no row ever starts behind the
//     first sample the renderer still retains. Forward-only as a property
//     of the memory, not of the intentions;
//   - **the two paths agree what kind of dead sector the station sends**,
//     because if they do not they are anchoring on different features and
//     comparing their columns compares two answers to two questions;
//   - **the forward tracker keeps the sync the batch path keeps** — where
//     the batch locked nearly every line, so does the preview. This is
//     what pins the whole-line re-acquisition sweep after a run of misses,
//     without which a dropout tears the page from there to the end;
//   - **every row's pixels are the ones its own reported start and period
//     produce.** `PreviewRow` is not a diagnostic — §7 marks the override
//     row with it and the GUI will map screen rows to stream positions
//     through it — and this is also the only claim here that separates a
//     renderer which USES its per-line lock from one that computes the
//     lock and then draws at its prediction. Neither geometry statistic
//     could: see the note beside `edge_scatter`.
//
// And the two live overrides [docs/05 §7], which are §9 screamers 5 and 6
// one stage early — those two pin the BATCH re-decode's handling, and
// nothing else pins the live half:
//
//   - **PHASE applies forward from the next row only**: the rows above the
//     override are bit-identical to a render without it, exactly one row
//     carries the mark, and the rows below move by the fraction asked for;
//   - **SYNC is a seed where the signal can contradict it and the value
//     where it cannot**: a deliberately wrong ppm is measured away within
//     a few rows on a pulse station, and stands for the whole page on a
//     white-only one. This is the asymmetry decided in session 17
//     [docs/05 §7.1], which the live path had no test for.
//
// Verified by mutation, seven of them, all killed: trimming the retained
// buffer per push instead of per row; dropping the re-acquisition sweep;
// applying PHASE in the wrong direction; freezing the forward EMA so SYNC
// becomes a winner; ignoring SYNC where nothing contradicts it; drawing
// rows at the prediction instead of at their own lock; and ignoring the
// phasing anchor on a white-only station. Two of those seven survived an
// earlier version of this test — the last two — and both survivals were
// holes in the test rather than in the code. The phasing-anchor mutation
// survived because the test asked the RENDERER which anchor class it was
// in, so a change that stopped using the phasing anchor reclassified
// itself out of the check that would have caught it.
//
// Explicitly NOT claimed: that the preview equals the batch image. It
// does not, and §6 says why — bracketed dropout repair, intra-line break
// placement and change-point timebase fitting all need signal the preview
// has not received yet, so rows the batch path repairs are drawn wrong
// here once. `himawari-jmh-warp-120s` is the case to look at by eye: the
// batch draws its 1270-sample loss in one piece, the preview shows the
// tear. That difference is the announced swap, not a defect, and a test
// that demanded equality would be demanding the design be abandoned.
#include "../core/demod.hpp"
#include "../core/fax.hpp"
#include "../core/gen.hpp"
#include "../core/image.hpp"
#include "../core/phasing.hpp"
#include "../core/wav.hpp"
#include "../live/preview.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}

// Block sizes a real capture can produce, plus the pathological one-sample
// case that exercises every partial-row path there is. Same set as
// `live_tones`, deliberately: the two tests make the same claim about the
// same kind of state machine, and a difference in the sets would be a
// difference nobody chose.
const std::size_t kBlocks[] = {1, 7, 333, 1000, 2000, 12345, 65536};

struct Render {
    nova::Image img;
    std::vector<nova::PreviewRow> rows;
    double period = 0.0;
    int locked = 0;
    int reacquired = 0;
    bool pulse = false;
    bool from_phasing = false;
    long long min_lookback = 0;  // rows' reach behind `retained_from`
};

// One full render at one block size. `phase_at` / `sync_at`, when >= 0,
// apply the corresponding operator override once that many rows have been
// drawn — the live moment §7 describes, mid-transmission.
Render render(const std::vector<float>& video, std::size_t from,
              std::size_t to, const nova::PreviewOptions& opt,
              std::size_t block, int phase_at = -1, double phase_frac = 0.0,
              int sync_at = -1, double sync_ppm = 0.0) {
    nova::StreamPreview p(8000, opt);
    Render r;
    // How far behind the retained window's first sample the neediest row
    // started. Negative would mean the renderer read video it had already
    // released — `fax_lerp_at` clamps rather than failing, so that would be
    // a silently wrong row rather than a crash, which is exactly why it is
    // measured instead of assumed.
    //
    // Measured on the LAST row of each call only. `retained_from()` can be
    // read from outside just once per call, and the renderer releases
    // video per row, so for any earlier row of a multi-row call the number
    // observed here belongs to a later moment and would understate the
    // slack. The last row's is exact.
    bool any_row = false;
    auto note = [&](const std::vector<nova::PreviewRow>& rows,
                    long long retained) {
        r.rows.insert(r.rows.end(), rows.begin(), rows.end());
        if (rows.empty()) return;
        const long long slack =
            static_cast<long long>(rows.back().start_sample) - retained;
        if (!any_row || slack < r.min_lookback) r.min_lookback = slack;
        any_row = true;
    };

    bool phase_done = phase_at < 0, sync_done = sync_at < 0;
    for (std::size_t i = from; i < to; i += block) {
        const std::size_t n = std::min(block, to - i);
        note(p.push(video.data() + i, n), p.retained_from());
        if (!phase_done && static_cast<int>(r.rows.size()) >= phase_at) {
            p.set_phase_anchor(phase_frac);
            phase_done = true;
        }
        if (!sync_done && static_cast<int>(r.rows.size()) >= sync_at) {
            p.set_clock_ppm(sync_ppm);
            sync_done = true;
        }
    }
    note(p.flush(), p.retained_from());
    r.img = p.image();
    r.from_phasing = p.anchor_from_phasing();
    r.period = p.period_samples();
    r.locked = p.locked_rows();
    r.reacquired = p.reacquired_rows();
    r.pulse = p.dead_sector() == nova::DeadSector::kBlackPulse;
    return r;
}

// Where the dead sector actually landed in a DRAWN image, in columns.
//
// This is the same pair of shape scores the decoder uses to find it in the
// SIGNAL [core/fax.cpp `stage_dead_sector`, live/preview.cpp
// `try_acquire`], applied to pixels instead of samples: the dead sector is
// the one part of the line that looks the same on every row, a black pulse
// is black-then-white, and a white-only sector is a rising edge into
// consistent white. Scoring a shape rather than a level matters here for
// the reason session 4 measured on the signal — a satellite image carries
// black at both margins on 100% of rows, and "darkest column" lands
// anywhere inside it.
//
// `first`/`count` restrict it to a band of rows, which is what the PHASE
// test needs: the claim there is about the top of the page differing from
// the bottom.
int dead_sector_column(const nova::Image& img, bool pulse, int first = 0,
                       int count = 0) {
    const int w = img.width;
    const int lo = first;
    const int hi = count > 0 ? std::min(img.height, first + count) : img.height;
    const int n = hi - lo;
    if (n <= 0 || w <= 0) return -1;
    std::vector<double> dark(w, 0.0), white(w, 0.0);
    for (int y = lo; y < hi; y++) {
        const uint8_t* row = &img.px[static_cast<std::size_t>(y) * w];
        for (int x = 0; x < w; x++) {
            if (row[x] < nova::kFaxDarkLevel * 255.0) dark[x] += 1.0;
            if (row[x] > nova::kFaxWhiteLevel * 255.0) white[x] += 1.0;
        }
    }
    for (int x = 0; x < w; x++) {
        dark[x] /= n;
        white[x] /= n;
    }
    auto win_mean = [&](const std::vector<double>& f, int at, int win) {
        double s = 0.0;
        for (int j = 0; j < win; j++) s += f[((at + j) % w + w) % w];
        return s / win;
    };
    const int pulse_w = std::max(2, static_cast<int>(nova::kFaxPulseFrac * w));
    const int dead_w = std::max(2, static_cast<int>(nova::kFaxDeadFrac * w));
    double best = -1e300;
    int at = 0;
    for (int x = 0; x < w; x++) {
        const double s =
            pulse ? std::min(win_mean(dark, x, pulse_w),
                             win_mean(white, x + pulse_w, pulse_w))
                  : win_mean(white, x, dead_w) - win_mean(white, x - dead_w,
                                                          dead_w);
        if (s > best) {
            best = s;
            at = x;
        }
    }
    return at;
}

// How much the dead-sector edge WANDERS from row to row, in columns.
//
// This exists because a whole-page column comparison cannot see the thing
// an operator notices first. A renderer that finds the lock on every row
// and then draws the row at its own prediction instead has a page whose
// average anchor is still right — the prediction is re-seeded from the
// lock each time — and whose edge zig-zags. Sara's review of the library
// named exactly this failure on the batch side in session 11 ("the black
// strip, it's actually zig zagging"), and the batch path answers it with
// `DecodeResult::place_rms_px`. This is the same measurement taken from
// the drawn pixels instead of from the residuals.
//
// Per row, the black->white pulse shape [WMO §5.1.3.3] is scored across a
// window around the page's anchor and the best column kept; the result is
// the spread of those columns about their median.
// What is measured is ROUGHNESS — the median row-to-row change in that
// column — and not the spread about the median, and session 10 already
// settled why in the phasing domain [core/phasing.hpp
// `PhasingResult::roughness`]: "An inserted sample moves the edge and it
// STAYS moved... a faded one redraws the edge every line". A tear is one
// large step that every later row inherits, and the preview is SUPPOSED to
// show it — `himawari-jmh-warp-120s` loses 1270 samples mid-picture and
// the batch path's bracketed repair is unavailable forward [docs/05 §6].
// Spread cannot tell that from a zig-zag: measured, both warp fixtures
// score 7.0 px of spread against 0.1-1.8 px for the clean ones, purely
// from the step. Roughness scores the step once.
struct EdgeScatter {
    double roughness = 0.0;  // median |col[i] - col[i-1]|
    double spread = 0.0;     // rms about the median, for the record
    double max = 0.0;
    int rows = 0;
};

EdgeScatter edge_scatter(const nova::Image& img, int about) {
    const int w = img.width;
    const int pw = std::max(2, static_cast<int>(nova::kFaxPulseFrac * w));
    const int span = 40;
    EdgeScatter e;
    if (img.height < 8 || w <= 4 * pw) return e;
    std::vector<double> cols;
    cols.reserve(img.height);
    for (int y = 0; y < img.height; y++) {
        const uint8_t* row = &img.px[static_cast<std::size_t>(y) * w];
        auto band = [&](int at) {
            double s = 0.0;
            for (int j = 0; j < pw; j++) s += row[((at + j) % w + w) % w];
            return s / pw;
        };
        double best = -1e300;
        int at = about;
        for (int c = about - span; c <= about + span; c++) {
            const double s = band(c + pw) - band(c);
            if (s > best) {
                best = s;
                at = c;
            }
        }
        cols.push_back(at);
    }
    std::vector<double> sorted = cols;
    std::sort(sorted.begin(), sorted.end());
    const double med = sorted[sorted.size() / 2];
    double sq = 0.0;
    for (double c : cols) {
        const double d = c - med;
        sq += d * d;
        e.max = std::max(e.max, std::fabs(d));
    }
    e.rows = static_cast<int>(cols.size());
    e.spread = std::sqrt(sq / e.rows);

    std::vector<double> steps;
    steps.reserve(cols.size());
    for (std::size_t i = 1; i < cols.size(); i++)
        steps.push_back(std::fabs(cols[i] - cols[i - 1]));
    std::sort(steps.begin(), steps.end());
    e.roughness = steps.empty() ? 0.0 : steps[steps.size() / 2];
    return e;
}

// Signed distance from a to b on a ring of `w` columns: a page rotated by
// one pixel and a page rotated by w-1 pixels are the same mistake.
int ring_delta(int a, int b, int w) {
    int d = (a - b) % w;
    if (d > w / 2) d -= w;
    if (d < -w / 2) d += w;
    return d;
}

bool same_pixels(const nova::Image& a, const nova::Image& b) {
    return a.width == b.width && a.height == b.height && a.px == b.px;
}

bool same_rows(const std::vector<nova::PreviewRow>& a,
               const std::vector<nova::PreviewRow>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); i++) {
        if (a[i].start_sample != b[i].start_sample) return false;
        if (a[i].period != b[i].period) return false;
        if (a[i].locked != b[i].locked) return false;
        if (a[i].sync_score != b[i].sync_score) return false;
    }
    return true;
}

std::vector<float> load_video(const std::string& path) {
    nova::Wav w = nova::read_wav(path);
    if (w.sample_rate != 8000)
        throw std::runtime_error(path + ": fixture is not 8 kHz");
    return nova::fm_demod(w.samples, 8000);
}

// --- where the anchor comes from, which decides what may be claimed ------
//
// The preview and the batch path can only be expected to put the dead
// sector in the same column when the transmission contains something that
// says where it is. The standard offers exactly two such things, and a
// recording may contain neither:
//
//   A. a BLACK PULSE in the dead sector [WMO §5.1.3.3] — a per-line
//      anchor, and both paths lock the same template onto it;
//   B. no pulse, but a PHASING INTERVAL [WMO §5.2.3.4] — its leading edge
//      of white IS dead-sector entry, and both paths take the anchor from
//      there (the preview via `PreviewOptions::phase_anchor`, which the
//      live state machine fills in on its way out of the PHASING state);
//   C. neither. A white-only dead sector carries no per-line phase at all
//      — measured, session 4 — so the ONLY evidence is an across-line
//      consistency profile, and the batch path builds one over 120 lines
//      where a forward renderer has 16. Where those two profiles pick
//      different candidates the page is drawn rotated, and NOTHING in the
//      signal says which is right.
//
// A and B are pinned tight below. C is measured and reported, not pinned,
// because pinning it would mean inventing a tolerance wide enough to admit
// a third of a page — which would stop the check from failing at all. The
// answer to C is not a wider tolerance, it is the operator: `ISO §4.2.6`
// manual adjustment, `docs/05 §7`'s PHASE control, and this test
// demonstrates that one click lands the page (see `check_phase_remedy`).
enum class AnchorClass { kPulse, kPhasing, kNone };

// Class A and B tolerances, set from the measurement this test prints
// across the library rather than chosen: 0-1 px observed on all eleven
// pulse fixtures, 0-1 px on both phasing-anchored white-only ones. The
// headroom is for the residue that is genuinely expected — a forward EMA
// against a fitted period over the drawn rows — and for another
// compiler's last bit, not for a different feature.
constexpr int kEdgeTolPulsePx = 12;
constexpr int kEdgeTolPhasingPx = 24;

int fixtures_seen = 0;
int fixtures_with_pulse = 0;
int fixtures_unanchored = 0;
int style_disagreements = 0;
double worst_edge_pulse = 0.0;
double worst_edge_phasing = 0.0;
double worst_edge_unanchored = 0.0;

// The answer to an unanchored page, demonstrated rather than asserted
// about: the operator sees the dead sector at column `prev_col` and says
// so [docs/05 §7 — PHASE reports WHERE IT IS, never a delta], and the rows
// below the click land where the batch path puts them. This is the
// affordance `ISO §4.2.6` requires, doing the job the signal could not.
void check_phase_remedy(const std::vector<float>& video, std::size_t from,
                        std::size_t to, const nova::PreviewOptions& opt,
                        int prev_col, int ref_col) {
    const int at = 8;
    // What the operator clicks: the dead sector as they see it, minus
    // where the page should have put it. On a page the batch draws at
    // column ~0 this is just `prev_col`.
    const int w = opt.ioc == 288 ? 905 : 1810;
    const double frac =
        static_cast<double>(((prev_col - ref_col) % w + w) % w) / w;
    const Render fixed = render(video, from, to, opt, 2000, at, frac);
    const int n = fixed.img.height - (at + 4);
    if (n <= 8) return;
    const int col = dead_sector_column(fixed.img, fixed.pulse, at + 4, n);
    const int d = ring_delta(col, ref_col, w);
    std::printf("       one PHASE click of %.3f: col %d -> %d, %+d px from "
                "the batch page\n",
                frac, prev_col, col, d);
    check(std::abs(d) <= 24,
          "...and one PHASE click lands the page where the batch path "
          "puts it");
}

}  // namespace

// --------------------------------------------------------------------------
// One library fixture: dimensions, the dead-sector edge against the batch
// image, and bit-identity across every block size.
// --------------------------------------------------------------------------
static void test_fixture(const std::string& path) {
    const std::string name =
        path.substr(path.find_last_of('/') + 1);
    std::printf("\n== %s\n", name.c_str());

    std::vector<float> video;
    try {
        video = load_video(path);
    } catch (const std::exception& e) {
        std::printf("  FAIL cannot read: %s\n", e.what());
        failures++;
        return;
    }

    // The batch decode is the reference AND the source of the seed. It
    // stands in for the live state machine, which enters DRAWING at the end
    // of the phasing interval with an IOC from the start tone and a rate
    // from the phasing [docs/05 §4, §6 seed order item 2]. Using its period
    // rather than the nominal 120 lpm is the honest stand-in: a live
    // session that saw a phasing interval has a measured rate, and one that
    // did not is the forced-start case, measured separately below.
    nova::DecodeResult ref;
    try {
        ref = nova::decode_fax(video, 8000, nova::DecodeOptions());
    } catch (const nova::DecodeError& e) {
        // A fixture with no picture in it — stall fill. The preview is
        // never handed one of these by the state machine, which only
        // enters DRAWING on a tone or a forced start, so there is nothing
        // here to render and nothing to claim.
        std::printf("  (no picture: %s) skipped\n", e.what());
        return;
    }

    const std::size_t from =
        static_cast<std::size_t>(ref.image_t_start * 8000.0);
    const std::size_t to = std::min(
        video.size(), static_cast<std::size_t>(ref.image_t_end * 8000.0));
    if (to <= from + 8000) {
        std::printf("  (picture shorter than a second) skipped\n");
        return;
    }

    nova::PreviewOptions opt;
    opt.ioc = ref.ioc;
    opt.lpm = 60.0 / ref.line_period_s;
    opt.seed = nova::PreviewSeed::kSignal;
    // What the live state machine has in hand when it enters DRAWING: it
    // has just come through PHASING [docs/05 §4], so if the station sent a
    // phasing interval it knows the line-start phase. Searched over the
    // recording BEFORE the picture, which is the only part a live session
    // would have heard by then.
    nova::PhasingOptions pho;
    pho.t_lo = 0.0;
    pho.t_hi = ref.image_t_start;
    const nova::PhasingResult ph =
        nova::detect_phasing(video, 8000, 8000.0 * ref.line_period_s, pho);
    if (ph.found) {
        const double period = 8000.0 * ref.line_period_s;
        double f = std::fmod(ph.anchor - static_cast<double>(from), period);
        if (f < 0) f += period;
        opt.phase_anchor = f / period;
    }

    const Render base = render(video, from, to, opt, 2000);
    fixtures_seen++;
    if (base.pulse) fixtures_with_pulse++;

    // --- dimensions -------------------------------------------------------
    const int want_w = ref.ioc == 288 ? 905 : 1810;
    check(base.img.width == want_w, "drawn width is the IOC's");
    check(base.img.height == static_cast<int>(base.rows.size()),
          "image height equals the rows emitted");
    // The renderer must draw every whole line it was given, less the one it
    // may give up at the head so the template does not read behind the
    // stream. Anything fewer is video silently dropped.
    const int avail =
        static_cast<int>(static_cast<double>(to - from) / base.period);
    check(base.img.height >= avail - 2 && base.img.height <= avail,
          "row count is the one the geometry predicts");
    std::printf("     %dx%d rows (%d predicted), %d locked, %d re-acquired, "
                "period %.2f smp, %s\n",
                base.img.width, base.img.height, avail, base.locked,
                base.reacquired, base.period,
                base.pulse ? "black-pulse" : "white-only");

    // --- forward-only, as a property of the memory ------------------------
    check(base.min_lookback >= 0,
          "no row ever started behind the retained window");

    // --- the two paths must at least agree what kind of station this is ---
    // They decide it the same way, from across-line consistency against the
    // same `kFaxPulseConsistency` cut, but over different numbers of lines.
    // Where they disagree they are anchoring on different features, and
    // comparing their columns would be comparing two answers to two
    // questions.
    const bool ref_pulse = ref.dead_sector == nova::DeadSector::kBlackPulse;
    const bool style_agrees = base.pulse == ref_pulse;
    if (style_agrees) {
        check(true, "the two paths agree what kind of dead sector this "
                    "station sends");
    } else {
        style_disagreements++;
        std::printf("     dead-sector STYLE differs: preview says %s over "
                    "%d row(s), batch says %s over the whole recording\n",
                    base.pulse ? "black-pulse" : "white-only",
                    base.img.height, ref_pulse ? "black-pulse" : "white-only");
        // The only case where disagreeing is allowed, and it is a limit of
        // the material rather than of the code: a consistency profile over
        // a couple of dozen rows is a weak instrument, and one of these
        // fixtures is an 8-second tail of a transmission. Above that, a
        // disagreement means the two paths are looking at different
        // features and one of them is wrong.
        check(base.img.height < 32,
              "...and the picture is under 32 rows, which is the only case "
              "where a profile is too short to settle it");
    }

    // --- re-acquisition, which is the difference between a dropout costing
    // --- a few rows and costing the rest of the page ----------------------
    // Where the batch path locked nearly every line, the forward tracker
    // has no excuse for losing the sync permanently: it sees the same
    // template on the same signal, and the whole-line sweep after a run of
    // misses is available to it. Measured without that sweep, on
    // `himawari-kiwisdr-dropout-120s`: 140 rows locked of 238, against 232
    // of 240 for the batch — the tracker never came back after the dropout
    // and every row below it was drawn torn.
    if (ref.lines > 0 && ref.locked_lines >= 0.9 * ref.lines &&
        base.img.height > 0) {
        const double frac =
            static_cast<double>(base.locked) / base.img.height;
        std::printf("     locks: preview %.0f%%, batch %.0f%%\n", frac * 100.0,
                    100.0 * ref.locked_lines / ref.lines);
        check(frac >= 0.75,
              "the forward tracker keeps the sync the batch path keeps");

        // Reported, not pinned, and the reason is worth writing down: the
        // library cannot separate a renderer that USES its per-line locks
        // from one that merely computes them. Draw every row at the
        // prediction instead of at its own lock and the page barely moves
        // — the prediction is re-seeded from each lock, so all that is lost
        // is the within-row correction, measured at 0.1-3 px across these
        // fixtures. No threshold on either statistic separates that from a
        // clean render without also failing the two warp fixtures, whose
        // 7.02 px of spread is a real tear the preview is SUPPOSED to show.
        // What pins placement instead is the redraw identity below.
        //
        // The numbers are still worth printing. Roughness 0.00 px with
        // 7.02 px of spread is a precise statement about the warp
        // fixtures: the edge takes one step and stays there, which is a
        // recording that lost samples, not a renderer that jitters.
        const EdgeScatter e =
            edge_scatter(base.img, dead_sector_column(base.img, true));
        std::printf("     dead-sector edge: roughness %.2f px, spread %.2f "
                    "px, max %.0f px over %d rows (batch place_rms "
                    "%.2f px)\n",
                    e.roughness, e.spread, e.max, e.rows, ref.place_rms_px);
    }

    // --- the row is drawn where the row says it was drawn -----------------
    // `PreviewRow::start_sample` and `::period` are not diagnostics: §7
    // needs them to mark the row an override took effect on, and the GUI
    // needs them to relate a screen row to a position in the retained
    // stream. Re-deriving the pixels from them independently is also the
    // only thing here that can tell a renderer which USES its per-line lock
    // from one that finds the lock and then draws at its prediction — see
    // the note above.
    //
    // Indexed into a COPY of the pushed segment rather than into `video` at
    // `from + start_sample`: `start_sample` is relative to the stream the
    // renderer was given, and re-adding `from` would do the arithmetic at a
    // different magnitude, where the double grid is coarser. That shifts
    // the interpolation fraction in the last bits and would eventually
    // round one pixel differently — a test failing on floating-point
    // bookkeeping it introduced itself.
    const std::vector<float> seg(video.begin() + from, video.begin() + to);
    bool redraw_ok = true;
    for (int y = 0; y < base.img.height && redraw_ok; y += 16) {
        const nova::PreviewRow& r = base.rows[y];
        const uint8_t* drawn = &base.img.px[static_cast<std::size_t>(y) *
                                            base.img.width];
        for (int j = 0; j < base.img.width; j++) {
            const double pos =
                r.start_sample + r.period * j / base.img.width;
            const uint8_t want = static_cast<uint8_t>(
                std::lround(nova::fax_lerp_at(seg, pos) * 255.0f));
            if (want != drawn[j]) {
                std::printf("     row %d col %d: %d drawn, %d from its own "
                            "start %.3f/period %.3f\n",
                            y, j, drawn[j], want, r.start_sample, r.period);
                redraw_ok = false;
                break;
            }
        }
    }
    check(redraw_ok,
          "every row's pixels are the ones its reported start and period "
          "produce");

    // --- the dead-sector edge, against the batch image --------------------
    // Classified from what the CALLER supplied, never from what the
    // renderer reports it did. Asking the code which class it is in lets a
    // change that stops using the phasing anchor reclassify itself into the
    // class that is not checked — which is exactly what happened when this
    // was written the other way round, and the mutation survived.
    const AnchorClass cls = base.pulse          ? AnchorClass::kPulse
                            : opt.phase_anchor >= 0.0 ? AnchorClass::kPhasing
                                                      : AnchorClass::kNone;
    check(base.from_phasing == (cls == AnchorClass::kPhasing),
          "the renderer used the anchor the caller supplied, and said so");
    const int prev_col = dead_sector_column(base.img, base.pulse);
    const int ref_col = dead_sector_column(ref.img, base.pulse);
    const int d = ring_delta(prev_col, ref_col, base.img.width);
    if (!style_agrees) {
        std::printf("     dead sector: preview col %d, batch col %d "
                    "(not compared: different features)\n",
                    prev_col, ref_col);
    } else if (cls == AnchorClass::kNone) {
        // Class C. Reported, not pinned — see the note above the tolerances.
        fixtures_unanchored++;
        worst_edge_unanchored =
            std::max(worst_edge_unanchored, std::fabs(1.0 * d));
        std::printf("     dead sector: preview col %d, batch col %d, "
                    "delta %+d px — UNANCHORED (white-only, no phasing "
                    "interval): nothing in the signal says which is right\n",
                    prev_col, ref_col, d);
        check_phase_remedy(video, from, to, opt, prev_col, ref_col);
    } else {
        const int tol =
            cls == AnchorClass::kPulse ? kEdgeTolPulsePx : kEdgeTolPhasingPx;
        std::printf("     dead sector: preview col %d, batch col %d, "
                    "delta %+d px (tol %d, anchored on the %s)\n",
                    prev_col, ref_col, d, tol,
                    cls == AnchorClass::kPulse ? "sync pulse"
                                               : "phasing interval");
        check(std::abs(d) <= tol,
              "dead sector lands where the batch path puts it");
        double& worst = cls == AnchorClass::kPulse ? worst_edge_pulse
                                                   : worst_edge_phasing;
        worst = std::max(worst, std::fabs(1.0 * d));
    }

    // --- the real claim: bit-identical whatever the block size ------------
    bool identical = true;
    for (std::size_t b : kBlocks) {
        const Render r = render(video, from, to, opt, b);
        if (!same_pixels(base.img, r.img) || !same_rows(base.rows, r.rows)) {
            std::printf("     block %zu DIFFERS (%dx%d vs %dx%d)\n", b,
                        r.img.width, r.img.height, base.img.width,
                        base.img.height);
            identical = false;
        }
    }
    check(identical, "image and row placement identical at every block size");
}

// --------------------------------------------------------------------------
// PHASE: applies forward from the next row only [docs/05 §7].
// --------------------------------------------------------------------------
static void test_phase_forward_only(const std::string& path) {
    std::printf("\n== PHASE applies forward only (%s)\n",
                path.substr(path.find_last_of('/') + 1).c_str());
    const std::vector<float> video = load_video(path);
    const nova::DecodeResult ref =
        nova::decode_fax(video, 8000, nova::DecodeOptions());
    const std::size_t from =
        static_cast<std::size_t>(ref.image_t_start * 8000.0);
    const std::size_t to = std::min(
        video.size(), static_cast<std::size_t>(ref.image_t_end * 8000.0));

    nova::PreviewOptions opt;
    opt.ioc = ref.ioc;
    opt.lpm = 60.0 / ref.line_period_s;
    // The relock would pull the picture straight back to the dead sector
    // within a row, which is correct behaviour and would hide the thing
    // being tested. The operator's PHASE is for the case where the anchor
    // is wrong and the signal cannot say so — so the test asks the same
    // question that case does.
    opt.autolock = false;

    const int at = 20;
    const double frac = 0.25;
    const Render plain = render(video, from, to, opt, 2000);
    const Render moved =
        render(video, from, to, opt, 2000, at, frac);

    // Rows above the override: byte for byte the picture that was already
    // on the screen. This is the whole promise of "drawn rows never move".
    bool top_same = plain.img.height >= at && moved.img.height >= at;
    if (top_same)
        top_same = std::equal(
            plain.img.px.begin(),
            plain.img.px.begin() +
                static_cast<std::ptrdiff_t>(at) * plain.img.width,
            moved.img.px.begin());
    check(top_same, "rows above the override are unchanged, byte for byte");

    // Exactly one row carries the mark, and it is the first row after the
    // override — the affordance §7 asks for.
    int marks = 0, mark_row = -1;
    for (const nova::PreviewRow& r : moved.rows)
        if (r.phase_mark) {
            marks++;
            if (mark_row < 0) mark_row = r.index;
        }
    check(marks == 1 && mark_row == at,
          "exactly one row is marked, and it is the first after the override");

    // Rows below: the page has rotated by the fraction asked for.
    const int w = plain.img.width;
    const int lo = at + 4, n = std::min(plain.img.height, moved.img.height) - lo;
    const int col_plain = dead_sector_column(plain.img, plain.pulse, lo, n);
    const int col_moved = dead_sector_column(moved.img, moved.pulse, lo, n);
    const int want = -static_cast<int>(std::lround(frac * w));
    const int got = ring_delta(col_moved, col_plain, w);
    std::printf("     below the override: col %d -> %d, moved %+d px, "
                "asked %+d px\n",
                col_plain, col_moved, got, want);
    check(std::abs(ring_delta(got, want, w)) <= 8,
          "rows below the override moved by the fraction asked for");
}

// --------------------------------------------------------------------------
// SYNC: a seed where the signal can contradict it, the value where it
// cannot [docs/05 §7.1 — the asymmetry decided in session 17].
// --------------------------------------------------------------------------
static void test_sync_is_a_fallback(const std::string& pulse_path,
                                    const std::string& white_path) {
    std::printf("\n== SYNC is measured away on a pulse station, and stands "
                "on a white-only one\n");
    const double kWrongPpm = 2000.0;  // 8 samples a line at 8 kHz

    auto run = [&](const std::string& path, bool& is_pulse, double& before,
                   double& after) {
        const std::vector<float> video = load_video(path);
        const nova::DecodeResult ref =
            nova::decode_fax(video, 8000, nova::DecodeOptions());
        const std::size_t from =
            static_cast<std::size_t>(ref.image_t_start * 8000.0);
        const std::size_t to = std::min(
            video.size(), static_cast<std::size_t>(ref.image_t_end * 8000.0));
        nova::PreviewOptions opt;
        opt.ioc = ref.ioc;
        opt.lpm = 60.0 / ref.line_period_s;
        const Render plain = render(video, from, to, opt, 2000);
        const Render trimmed =
            render(video, from, to, opt, 2000, -1, 0.0, 20, kWrongPpm);
        is_pulse = plain.pulse;
        before = plain.period;
        after = trimmed.period;
    };

    bool pulse = false, white = false;
    double p_before = 0, p_after = 0, w_before = 0, w_after = 0;
    run(pulse_path, pulse, p_before, p_after);
    run(white_path, white, w_before, w_after);

    check(pulse, "the pulse fixture really is a pulse station");
    check(!white, "the white-only fixture really is white-only");

    // On a pulse station the per-line relock keeps measuring the real
    // period, and the EMA walks the operator's trim off within a few rows.
    const double p_left = (p_after / p_before - 1.0) * 1e6;
    std::printf("     pulse station: %.2f -> %.2f smp, %.0f ppm of %.0f "
                "still standing\n",
                p_before, p_after, p_left, kWrongPpm);
    check(std::fabs(p_left) < 0.2 * kWrongPpm,
          "a wrong SYNC is measured away where the signal can contradict it");

    // On a white-only station nothing ever contradicts it, so it is the
    // value used — which is the whole point of a fallback, and the half of
    // the decision most likely to be quietly implemented as a plain
    // override in either direction.
    const double w_left = (w_after / w_before - 1.0) * 1e6;
    std::printf("     white-only:    %.2f -> %.2f smp, %.0f ppm of %.0f "
                "still standing\n",
                w_before, w_after, w_left, kWrongPpm);
    check(std::fabs(w_left - kWrongPpm) < 1.0,
          "a wrong SYNC stands where the signal cannot contradict it");
}

// --------------------------------------------------------------------------
// IOC 288: no library recording carries one [AGENTS.md registered gaps],
// and the drawn width is the one thing that must be different about it.
// --------------------------------------------------------------------------
static void test_ioc288_width() {
    std::printf("\n== IOC 288 draws 905 columns (generated: no fixture has "
                "one)\n");
    nova::GenOptions g;
    g.ioc = 288;
    g.start_tone = false;
    g.phasing = false;
    g.stop_tone = false;
    const nova::Image content = nova::gen_test_pattern(600, 120);
    const std::vector<float> audio = nova::gen_fax_signal(content, 120, g);
    const std::vector<float> video = nova::fm_demod(audio, 8000);

    nova::PreviewOptions opt;
    opt.ioc = 288;
    opt.lpm = 120.0;
    opt.seed = nova::PreviewSeed::kOperator;
    const Render r = render(video, 0, video.size(), opt, 2000);
    std::printf("     %dx%d rows, %d locked, %s\n", r.img.width, r.img.height,
                r.locked, r.pulse ? "black-pulse" : "white-only");
    check(r.img.width == 905, "IOC 288 draws 905 columns");
    check(r.img.height > 100, "and still draws the page");

    bool identical = true;
    for (std::size_t b : kBlocks) {
        const Render o = render(video, 0, video.size(), opt, b);
        if (!same_pixels(r.img, o.img)) identical = false;
    }
    check(identical, "identical at every block size at IOC 288 too");
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s FIXTURE.wav...\n", argv[0]);
        return 2;
    }
    std::string pulse_path, white_path;
    for (int i = 1; i < argc; i++) {
        const std::string p = argv[i];
        test_fixture(p);
        if (p.find("test-chart-jmh-kiwisdr-image") != std::string::npos)
            pulse_path = p;
        if (p.find("vmw-white-sector") != std::string::npos) white_path = p;
    }

    if (!pulse_path.empty()) test_phase_forward_only(pulse_path);
    if (!pulse_path.empty() && !white_path.empty())
        test_sync_is_a_fallback(pulse_path, white_path);
    test_ioc288_width();

    std::printf(
        "\n%d fixture(s) rendered: %d anchored on a sync pulse, %d on a "
        "phasing interval, %d on neither. %d style disagreement(s).\n"
        "Worst dead-sector disagreement with the batch path: %.0f px on a "
        "pulse (tol %d), %.0f px on a phasing anchor (tol %d), %.0f px "
        "unanchored (not pinned — the operator's PHASE is the answer, and "
        "one click lands it).\n",
        fixtures_seen, fixtures_with_pulse,
        fixtures_seen - fixtures_with_pulse - fixtures_unanchored,
        fixtures_unanchored, style_disagreements, worst_edge_pulse,
        kEdgeTolPulsePx, worst_edge_phasing, kEdgeTolPhasingPx,
        worst_edge_unanchored);
    std::printf("%s (%d failure(s))\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
