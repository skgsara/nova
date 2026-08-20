// nova-gui — the M4 shell: the window of docs/05 §8 built from real FLTK
// widgets, carrying the §8.3 and §8.4 surfaces, with the live decode
// behind them since session 23.
//
// Why this binary exists. The §8 layout was drawn in HTML at FLTK's
// documented metrics, which makes it a prediction. This is the measurement.
// Wherever the toolkit disagrees with the mockup, `docs/05` §8 is the thing
// that gets corrected — not this file.
//
// **What is in this file, and what is deliberately not.** Everything that
// can be wrong about a SIGNAL lives in `nova-live` — the ring, the
// streaming front end, the session machine, the preview, the batch
// handoff and the save path are all `live/engine.hpp`, drivable by a test
// with a fixture instead of a sound card [docs/05 §1]. What is here is
// widgets, the RtAudio stream that feeds `LiveEngine::push_audio`, and the
// 50 ms timer that drains its queue. No DSP, no state machine, no file
// naming: if a picture comes out wrong, this file is not where it went
// wrong, and that is the point of the split.
//
// A control with nothing behind it is created deactivated, so the window
// never claims to do what it cannot [docs/05 §3: an image with no raw
// behind it shows PHASE/SYNC visibly disabled rather than silently
// inert]. On a machine with no input device there is no engine, and the
// transport stays grey exactly as it did before the wiring landed — a
// Start that greys itself is honest where a Start that does nothing is
// not. Since session 27 the same rule decides the correction surface with
// something actually behind it: `correction_for` is a pure function of
// four booleans, and with nothing typed and nothing applied both Apply and
// Auto are grey, because the picture already IS the measured render and a
// button that rewrites the same file is a button that does nothing. When a
// correction is impossible the shell says WHY [§3], which is the half of
// that rule a grey control cannot carry on its own.
//
// No column arithmetic is written inside a widget. The ruler's mapping —
// zoom, scroll, tick step, and the left-edge retention of §8.4 item 2 —
// lives in `live/ruler.hpp` as pure functions, so the ruler's draw code and
// the (future) click handler call the same numbers the `ruler_mapping`
// screamer tests without a window.
//
// Flags make the shell inspectable without a window, which is also how it
// gets checked on a machine with no audio device:
//   --devices     list the input devices RtAudio reports, then exit
//   --metrics     print every region's real geometry and the shell state
//   --size WxH    build at a window size
//   --resize WxH  put the window through FLTK's own resize path
//   --state NAME  put the shell in a live state as nova-live will drive it,
//                 so the §8.3/§8.4 transport rules are inspectable before
//                 there is a capture behind them
//   --zoom V      Fit | 25 | 50 | 100 | 200
//   --ioc N       Auto | 576 | 288        (Force Start needs both explicit)
//   --rate N      Auto | 60 | 90 | 120
//   --follow BxR  drive the newest-row follow with B batches of R rows and
//                 report where the picture ACTUALLY sits — the child's own
//                 position, not Fl_Scroll's cached copy of it [session 27]
//   --correction  the whole truth table of `correction_for`, so §8.5 item
//                 4's edit boundary is checkable with nothing decoded
//   --sync-step   where a SYNC nudge starts from, which is the one thing
//                 in the steppers that can be quietly wrong [sync_step]
//   --click X,Y   click the image at X,Y through the real handler;
//                 repeatable, because the gesture under test is a
//                 SEQUENCE (one click sets PHASE, a second one far
//                 enough down measures the slant). --click-rows N
//                 gives the pane a picture to be clicked on first
//   --feed W,PCT  an OFFLINE CAPTURE [session 31]: push the next PCT% of
//                 WAV through the real engine, with no sound card and no
//                 window. The cursor per file wraps, so `--feed w,100
//                 --feed w,50` is the whole recording and then the first
//                 half of a second pass — which is how one fixture makes
//                 TWO transmissions. Needs --image-folder [see feed_wav]
//   --image-folder DIR   where a capture saves, overriding the operator's
//                 remembered folder. --feed REFUSES without it, because a
//                 test run must never write into a real image folder
//   --stop-capture   press Stop, which is what ends a transmission early
//   --type phase|sync V  type into a correction box through the real edit
//                 callback, so the edit BEGINS the way §8.5 item 4 says
//   --apply / --auto     press Apply / Auto
//   --recv-click  click the receiving indicator [§8.2], the one and only
//                 thing that brings a buffered picture forward
//   --mark NAME   print the indicator's state HERE, mid-sequence. §8.2's
//                 rules are about transitions — a buffer that survives an
//                 Apply, a count that names the buffered picture and not
//                 the pane's — and a check spanning two processes cannot
//                 observe a transition [session 28]
#include "../core/version_flag.hpp"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Float_Input.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Int_Input.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Native_File_Chooser.H>
#include <FL/Fl_Progress.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/Fl_Scroll.H>
#include <FL/fl_draw.H>

#include <RtAudio.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "../core/wav.hpp"
#include "engine.hpp"
#include "ruler.hpp"
#include "session.hpp"

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace {

// ---------------------------------------------------------------------------
// Metrics. Every number the layout rests on, named, so that a disagreement
// with the mockup is a one-line correction rather than an archaeology
// exercise. Row heights come from docs/05 §8; the window size does not —
// §8 never fixed one, so this is a choice made here and recorded.
constexpr int kWinW = 980;
constexpr int kWinH = 700;
// The control row is the widest fixed thing in the window, and the Zoom
// control [§8.3 item 2] made it wider: captions and controls out to the Zoom
// menu now reach 678 px, then Start and Force Start against the right edge
// (168 px), plus a gap. §8.3 predicted "roughly 880" for this; 678 + 168 + 4
// is 850, and 880 keeps a 26 px gap between the two halves.
constexpr int kMinW = 880;
constexpr int kMinH = 420;
constexpr int kMenuH = 25;        // §8.3: the File / Settings / Help bar
constexpr int kControlRowH = 25;  // §8: "25 px control rows"
constexpr int kRulerH = 18;
constexpr int kMeterH = 18;  // §8
// M4.5's tuning strip [ROADMAP M4.5; Sara, session 36]. It extends the
// full-width meter strip rather than taking sidebar space — a waterfall
// wants width for a frequency axis, and 200 px is the wrong shape for one
// [docs/05 §8.3 item 5]. The three sub-heights add up to kStripH and are
// named separately because the waterfall's height is also the number of
// history rows the analyser is asked to keep: every retained row is a drawn
// row, so nothing is decimated on its way to the screen.
constexpr int kStripH = 72;
constexpr int kStripTraceH = 20;    // the instantaneous spectrum, on top
constexpr int kStripAxisH = 12;     // the frequency labels, underneath
constexpr int kStripWaterfallH = kStripH - kStripTraceH - kStripAxisH;  // 40
// The two WMO tones the strip exists to let an operator line up on
// [WMO §5.2.1, docs/01]: black is 1500 Hz, white is 2300 Hz.
constexpr double kBlackHz = 1500.0;
constexpr double kWhiteHz = 2300.0;
constexpr int kStatusH = 22;
constexpr int kPanelW = 200;
constexpr int kPad = 4;
constexpr int kPanelRowH = 20;
constexpr int kFontSize = 12;  // §8: "12 px Helvetica"
constexpr int kFrame = 2;      // §8: "two-pixel FL_UP_BOX / FL_DOWN_BOX"
// The GUI queue's drain interval [docs/05 §2.3]: 50 ms, one repaint per
// tick at most. 20 Hz is well past what a 120 lpm picture — two rows a
// second — can justify.
constexpr double kTickSec = 0.05;

// Image width is round(IOC * pi) [docs/05 §8.3]: 1810 columns at IOC 576,
// 905 at IOC 288, both measured from the real decoder.
constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------------------
// The live session state [docs/05 §4]. Until session 23 this file carried
// its own copy of the enum — a "display twin" of `SessionState`, written
// when there was no session to display. There is one now, and the twin is
// gone: two enums that must stay in the same order is a bug waiting for
// somebody to insert a state in one of them.
using LiveState = nova::SessionState;

// The state name for the status line. It is the state's own name and never
// a percentage [docs/04 Finding 3], and the button never carries it
// [§8.4 item 4]. `nova::session_state_name` is the one definition.
const char* state_text(LiveState s) { return nova::session_state_name(s); }

// The short token --metrics prints, so a test script matches on a word
// rather than on a UTF-8 em dash. This one stays here: it exists for the
// test harness, not for the operator, and nothing in nova-live needs it.
const char* state_token(LiveState s) {
    switch (s) {
        case LiveState::kIdle: return "IDLE";
        case LiveState::kReady: return "READY";
        case LiveState::kStartTone: return "START-TONE";
        case LiveState::kPhasing: return "PHASING";
        case LiveState::kDrawingPreview: return "DRAWING";
        case LiveState::kStopTone: return "STOP-TONE";
        case LiveState::kDecoding: return "DECODING";
        case LiveState::kSaved: return "SAVED";
    }
    return "IDLE";
}

bool parse_state(const char* s, LiveState* out) {
    const struct {
        const char* name;
        LiveState state;
    } kNames[] = {
        {"idle", LiveState::kIdle},
        {"ready", LiveState::kReady},
        {"start-tone", LiveState::kStartTone},
        {"phasing", LiveState::kPhasing},
        {"drawing", LiveState::kDrawingPreview},
        {"stop-tone", LiveState::kStopTone},
        {"decoding", LiveState::kDecoding},
        {"saved", LiveState::kSaved},
    };
    for (const auto& n : kNames)
        if (!std::strcmp(s, n.name)) {
            *out = n.state;
            return true;
        }
    return false;
}

// The transport, as one rule rather than as scattered widget calls
// [docs/05 §8.3 item 4, §8.4 items 3-4]. Everything the two buttons do is
// decided here, so the screamer and the window cannot disagree about it.
//
//   - one button, relabelled by state: it reads "Stop" from READY through
//     STOP TONE, and "Start" everywhere else. It never reads a state name;
//     states live in the status line;
//   - during DECODING it is insensitive and still reads "Start" — the first
//     shell is serialized, one thing at a time, active again at SAVED;
//   - Force Start is insensitive unless BOTH IOC and rate are explicit
//     numbers, and insensitive during reception. Deactivate, never prompt.
//
// `capture` is whether there is anything behind the buttons at all. It is
// false on a plain run today, which is why they are grey.
struct Transport {
    const char* label;
    bool start_active;
    bool force_active;
};

Transport transport_for(LiveState s, bool ioc_explicit, bool rate_explicit,
                        bool capture) {
    const bool receiving =
        s == LiveState::kReady || s == LiveState::kStartTone ||
        s == LiveState::kPhasing || s == LiveState::kDrawingPreview ||
        s == LiveState::kStopTone;
    Transport t;
    t.label = receiving ? "Stop" : "Start";
    // Stop is always available while receiving: it is the first of the
    // three ways a transmission ends [docs/04 Finding 6], and it holds the
    // image rather than discarding it. DECODING is the one state where the
    // button is dead.
    t.start_active = capture && s != LiveState::kDecoding;
    t.force_active = capture && !receiving && s != LiveState::kDecoding &&
                     ioc_explicit && rate_explicit;
    return t;
}

// ---------------------------------------------------------------------------
// What the correction surface offers [docs/05 §7, §7.1, §3, §8.5 items
// 2-4]. A pure function for the same reason `transport_for` is one: the
// rule can then be checked without a window, a sound card, or a decoded
// image behind it, and the shell has exactly one place that decides it.
//
// The same two boxes serve two surfaces, and never both at once, because a
// picture is either being drawn or has been decoded:
//   - LIVE [§7]: forward-only corrections to a preview. Apply is always
//     available, because the correction takes effect from the next row
//     whatever the boxes hold.
//   - POST-DECODE [§7.1, §8.5]: a re-render of a picture already on disk,
//     from the raw stream retained behind it [§3]. Apply is available when
//     an EDIT IS IN PROGRESS — §8.5 item 4's "an edit begins at the first
//     dirty control" — because re-rendering the values already rendered
//     would rewrite the same file for nothing, and Auto when there is a
//     typed value to discard or an applied one to undo.
//
// The rule both halves obey, and the reason this is not just taste: **an
// active button that does nothing is the one failure this shell must not
// have** (session 26, finding 2, found on the air).
struct CorrectionUi {
    bool inputs_active;
    bool apply_active;
    bool auto_active;
};

CorrectionUi correction_for(bool live_surface, bool can_rerender,
                            bool edit_dirty, bool applied) {
    CorrectionUi c;
    c.inputs_active = live_surface || can_rerender;
    c.apply_active = live_surface || (can_rerender && edit_dirty);
    c.auto_active = can_rerender && (edit_dirty || applied);
    return c;
}

// --- the SYNC steppers [Sara, session 28] ----------------------------------
// SYNC is a ppm trim and the operator nulls a slant with it BY EYE, so the
// judgement they actually have is "a bit more", not "-93". Four buttons
// step the box.
//
// The two sizes are measured, not chosen. At IOC 576 a line is 1810 px
// wide, so a clock error of 1 ppm walks the line start by 1810e-6 px per
// line: over a full ~1200-line chart that is ~2.2 px of skew at the bottom
// of the page — visible, and about the smallest step worth having. Real
// errors run 30-180 ppm (session 5; the four white-only fixtures read -70
// to -118), so a 1 ppm button ALONE would be a hundred clicks to cross the
// range it exists to cross. Fine 1, coarse 10.
//
// PHASE deliberately gets no steppers, and this is the asymmetry rather
// than an omission: PHASE is a SEED refined to the best feature within
// `search_frac` of it, ±3% of a line = ±54 columns at IOC 576
// [core/fax.cpp, stage_dead_sector]. Any nudge smaller than that window is
// refined straight back onto the same feature and the picture does not
// move — a control that visibly does nothing, which is session 26's
// finding 2 wearing a different hat. PHASE's instrument is the click.
struct SyncStep {
    double value;      // the ppm the box will hold after the nudge
    bool from_shown;   // ...and whether it started from the shown clock
};

// The one decision here with a trap in it is WHERE A NUDGE STARTS when the
// box is blank. Blank means "as measured", and the measured clock is not
// 0 ppm — on the white-only fixtures it is -70 to -118. Starting from zero
// would make the operator's FIRST click a jump of the entire clock error,
// away from correct, on exactly the stations this control exists for. So a
// nudge from blank starts at the clock the picture on the pane was DRAWN
// on, which is the number the Quality field is already showing them.
SyncStep sync_step(const char* typed, double shown_ppm, bool shown_valid,
                   double delta) {
    SyncStep s;
    s.from_shown = false;
    double base = 0.0;
    char* end = nullptr;
    const double v = typed ? std::strtod(typed, &end) : 0.0;
    if (typed && end != typed) {
        base = v;
    } else if (shown_valid) {
        base = shown_ppm;
        s.from_shown = true;
    }
    s.value = base + delta;
    return s;
}

// --- arming a gesture [Sara, session 29; hamfax's shape] -------------------
// The image is one surface and there are two things an operator can mean by
// clicking it, so the click has to be declared. `Arm` is that declaration:
// nothing acts on the picture unless one of these is set.
//
// **This reverses session 28's decision, and the reversal is evidenced.**
// Session 28 built the two gestures undeclared — a click set PHASE, a
// second click far enough away measured a slant — and rejected a mode
// toggle on the grounds that this document's idiom is "auto is a value in
// the same list, never a separate mode", and rejected a shift-click on the
// grounds that it is INVISIBLE and this surface is supposed to explain
// itself. Sara asked, session 29, what hamfax does. It arms: Image ->
// "Adjust IOC (change width)" prompts "select first point of vertical
// line", disables the rest of the controls, takes two clicks, and ends;
// "set beginning of line" prompts, takes one click, and ends. Twenty years
// of a shipping program doing exactly this gesture.
//
// Three things that settles, none of which the session-28 reasoning had:
//   - The lifecycle objection was wrong. "A one-shot arming button breaks
//     the two-click gesture, because you would re-arm between clicks" —
//     no: you arm the GESTURE, not the click. One press covers both, and
//     the arming ends when the measurement completes.
//   - It is not a mode you can be stuck in, which is what the idiom was
//     protecting against. It is per-gesture and self-clearing.
//   - It is VISIBLE, which is the criterion session 28 itself stated when
//     it rejected shift-click. A button satisfies that test better than
//     the undeclared gesture that got built.
//
// And it buys the thing that could not be bought otherwise: an accidental
// click on the picture cannot move the operator's data, because an
// unarmed click does not act at all. That was Sara's objection to the
// session-28 surface and it has no answer inside an undeclared gesture.
//
// What is deliberately NOT taken from hamfax: the modal prompt dialog and
// disabling every other control. Nova already has the surface for saying
// what is going on — the reason line under Apply/Auto — and the cursor
// already carries the affordance. So the armed state shows in three
// places that cannot disagree, because all three read this one value: the
// button is pushed, the reason line says what to click, and the crosshair
// appears. The crosshair is the biggest gain: until session 29 it was on
// whenever a correction was possible, which made it ambient decoration;
// now it means "this click will act".
enum class Arm { kNone, kPhase, kSync };

const char* arm_token(Arm a) {
    switch (a) {
        case Arm::kPhase: return "phase";
        case Arm::kSync: return "sync";
        case Arm::kNone: break;
    }
    return "none";
}

// --- what a click on the image NAMES [docs/05 §8.3 item 1] ----------------
// PHASE is typed as an image column because that is what the ruler reads,
// and the surveyed affordance is clicking the dead sector rather than
// reading a number off the ruler and typing it [docs/04 answer 8]. This is
// that click, and it is deliberately the SAME arithmetic the ruler draws
// from — `nova::column_at` on the shell's own `view_state()` — because
// ruler.hpp's correctness claim is precisely that the column under a screen
// x is the column the ruler names there. Two different mappings would let
// the operator click a tick and get a different number than the tick says;
// one mapping cannot. If `Fl_Scroll::xposition` ever lies the way session
// 27 found `yposition` lie, the ruler and the click are wrong TOGETHER and
// the ruler is the visible witness — which is why `scroll_x_actual` is
// reported beside it rather than being used as a second source of truth.
//
// Returns the column, or -1 for "this click names nothing". With the image
// narrower than the pane, screen x beyond the image's right edge maps
// legitimately past the last column [ruler.hpp, `column_at`]: there is no
// picture there, so there is no dead sector there, and inventing a column
// the operator did not point at is worse than doing nothing.
int clicked_column(const nova::RulerView& v, double x) {
    if (v.image_cols <= 0) return -1;
    const double col = nova::column_at(v, x);
    if (col < 0.0) return -1;
    const int c = static_cast<int>(col);
    return c < v.image_cols ? c : -1;
}

// --- two-click SYNC [Sara, session 28] ------------------------------------
// The other half of the click, and the reason it is worth building: SYNC
// is a SLANT, and a slant is the same feature at two different ROWS. One
// click cannot see it; two can. `ppm = (dcol/drow) / width * 1e6`.
//
// What that buys is not convenience. §7.1 currently apologises for this
// field — "a ppm eyeballed off thirty seconds of preview is worse than one
// fitted over the whole transmission" — and it is right, because an
// eyeballed number has no baseline behind it. Two clicks a thousand rows
// apart DO have one. This is sessions 5, 8 and 9's finding (precision is
// baseline, not averaging) reaching the operator's hand instead of only
// the decoder's fit.
//
// The measurement is a RESIDUAL: it is read off the picture as drawn, so
// it is the error remaining on top of whatever clock that render used, and
// the value for the box is `shown_ppm + residual`. Same reasoning as the
// steppers' blank-box rule, and the same trap if it is skipped — a picture
// drawn at -118 ppm with 4 ppm of visible slant left needs -114, not 4.

// One screen pixel of click error is `1/scale` columns, so the shortest
// baseline worth measuring over is the one that puts the resulting ppm
// error an order of magnitude below the errors this control exists to
// remove (30-180 ppm; session 5).
//
// **Session 29 demoted this number from a GATE to a REFERENCE**, and the
// reason is the arming buttons [see Arm]. Until session 29 the gesture was
// undeclared, so this one number had to do two jobs at once: it was the
// precision limit AND it was what told "measure the slant" apart from "the
// operator is re-picking PHASE". Arming answers the second question
// outright — the operator has said which gesture this is — so the number
// goes back to being only what it honestly is, a statement about how good
// the answer will be. A short baseline now MEASURES, and says what it is
// worth [see slant_error_ppm]; Sara, session 29: her judgement, with the
// honesty attached rather than a refusal.
constexpr double kSlantPrecisionPpm = 5.0;

int min_baseline_rows(double scale, int image_cols) {
    if (scale <= 0.0 || image_cols <= 0) return 0;
    const double n = 1e6 / (kSlantPrecisionPpm * scale * image_cols);
    return static_cast<int>(std::ceil(n));
}

// What the measurement the operator just made is actually worth, in the
// same unit as the measurement. One screen pixel of aim is `1/scale`
// columns, spread over `drow` rows of baseline. This is `min_baseline_rows`
// solved for the error instead of for the rows, so the two cannot disagree
// about what the precision limit means — at exactly the baseline it
// returns kSlantPrecisionPpm.
//
// It exists because hamfax, which is where this gesture comes from, has no
// baseline notion at all: two clicks on the same row divide by zero there.
// Nova takes the gesture and keeps its own arithmetic honest.
double slant_error_ppm(double scale, int drow, int image_cols) {
    if (scale <= 0.0 || drow == 0 || image_cols <= 0) return 0.0;
    return 1e6 / (scale * std::abs(drow) * image_cols);
}

// Signed both ways on purpose: clicking bottom-then-top flips dcol and
// drow together, so the answer is the same measurement and the operator
// does not have to know which order the gesture wanted.
double slant_ppm(int dcol, int drow, int image_cols) {
    if (drow == 0 || image_cols <= 0) return 0.0;
    return (static_cast<double>(dcol) / drow) / image_cols * 1e6;
}

// Whole ppm is the normal case — the buttons step by 1 and 10 — and a
// trailing ".0" on every value is noise. A typed fraction survives.
std::string sync_text(double ppm) {
    char buf[32];
    if (std::fabs(ppm - static_cast<double>(std::lround(ppm))) < 1e-9)
        std::snprintf(buf, sizeof buf, "%ld", std::lround(ppm));
    else
        std::snprintf(buf, sizeof buf, "%.1f", ppm);
    return buf;
}

// ---------------------------------------------------------------------------
// The preference file [docs/05 §8.4 item 1]: plain text, next to the
// executable — visible, inspectable, movable with the program, and no
// hidden platform store. If the directory is not writable (a system-wide
// install) Nova runs without persistence for the session and never fails:
// settings are a convenience, never a precondition.
std::string executable_dir(const char* argv0) {
    std::string path;
#if defined(__APPLE__)
    uint32_t n = 0;
    _NSGetExecutablePath(nullptr, &n);
    std::string buf(n, '\0');
    if (_NSGetExecutablePath(&buf[0], &n) == 0) path = buf.c_str();
#elif defined(__linux__)
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n > 0) {
        buf[n] = '\0';
        path = buf;
    }
#endif
    if (path.empty() && argv0) path = argv0;
    const size_t slash = path.find_last_of('/');
    if (slash == std::string::npos) return ".";
    return path.substr(0, slash);
}

class Prefs {
public:
    void open(const std::string& dir) {
        path_ = dir + "/nova.conf";
        const bool existed = std::ifstream(path_).good();
        load();
        // Writability is discovered by trying, not by inspecting mode bits:
        // a read-only directory, a read-only file and a full disk are the
        // same answer here, and each of them is "no persistence", not an
        // error the operator has to dismiss. Appending nothing cannot
        // damage an existing file — but it would CREATE one that was not
        // there, and merely inspecting the shell must not leave a file
        // behind, so a probe that created it removes it again.
        {
            std::ofstream probe(path_, std::ios::app);
            writable_ = probe.good();
        }
        if (!existed) std::remove(path_.c_str());
    }

    const std::string& path() const { return path_; }
    bool writable() const { return writable_; }

    std::string get(const std::string& key) const {
        for (const auto& kv : entries_)
            if (kv.first == key) return kv.second;
        return std::string();
    }

    void set(const std::string& key, const std::string& value) {
        for (auto& kv : entries_)
            if (kv.first == key) {
                kv.second = value;
                save();
                return;
            }
        entries_.emplace_back(key, value);
        save();
    }

private:
    void load() {
        std::ifstream in(path_);
        if (!in) return;
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            entries_.emplace_back(line.substr(0, eq), line.substr(eq + 1));
        }
    }

    void save() {
        if (!writable_) return;
        std::ofstream out(path_, std::ios::trunc);
        if (!out) {
            writable_ = false;
            return;
        }
        out << "# nova preferences [docs/05 8.4 item 1]. Plain text, next to\n"
               "# the program, safe to edit or delete.\n";
        for (const auto& kv : entries_) out << kv.first << "=" << kv.second
                                            << "\n";
        if (!out.good()) writable_ = false;
    }

    std::string path_;
    std::vector<std::pair<std::string, std::string>> entries_;
    bool writable_ = false;
};

// ---------------------------------------------------------------------------
// About [docs/05 §8.3 item 8], and it is not decoration: Nova is GPLv3+, so
// this is where the licence and no-warranty notice live, with the pointer to
// NOTICE the provenance rule requires reachable from the program itself.
// The text is copied verbatim from docs/05 §8.3 — it is not this file's to
// reword.
const char* kAboutText =
    "Nova \xe2\x80\x94 an HF weather facsimile (WEFAX) decoder\n"
    "\n"
    "Copyright \xc2\xa9 2026 Nova contributors\n"
    "\n"
    "Nova is free software: you can redistribute it and/or modify it under "
    "the terms of the GNU General Public License as published by the Free "
    "Software Foundation, either version 3 of the License, or (at your "
    "option) any later version.\n"
    "\n"
    "Nova is distributed in the hope that it will be useful, but WITHOUT ANY "
    "WARRANTY; without even the implied warranty of MERCHANTABILITY or "
    "FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License "
    "for more details \xe2\x80\x94 the full text is in the LICENSE file "
    "distributed with this program.\n"
    "\n"
    "Built from public standards: WMO-No. 386 Vol. I Part III \xc2\xa7"
    "5 (the signal) and ISO 9876:2015 \xc2\xa7"
    "4.2 (receiver behaviour as a design target \xe2\x80\x94 no "
    "certified-compliance claim).\n"
    "\n"
    "DSP reuse attributions and linked-library licences (FLTK, RtAudio): "
    "see the NOTICE file.";

// ---------------------------------------------------------------------------
// Fl_Menu_ parses '&', '/', '\' and '_' out of the labels it is given, and
// device names are not ours to choose — a USB interface called "Scarlett
// 2i2 In 1/2" would otherwise become a submenu.
std::string escape_menu_label(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (const char c : s) {
        if (c == '&' || c == '/' || c == '\\' || c == '_') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

struct InputDevice {
    unsigned int id = 0;
    std::string name;
    unsigned int channels = 0;
    unsigned int preferred_rate = 0;
    bool is_default = false;
};

// Enumeration is the whole of the shell's RtAudio use: no stream is
// opened, so nothing here can touch the microphone. Capture rate is not
// filtered on [docs/05 §12 item 1, decided: accept whatever the device
// offers and resample to 8 kHz], so a device is listed if it has an input
// channel and for no other reason.
std::vector<InputDevice> list_input_devices(std::string* err) {
    std::vector<InputDevice> devs;
    std::string last_error;
    const RtAudioErrorCallback on_error =
        [&last_error](RtAudioErrorType, const std::string& text) {
            last_error = text;
        };

    // The API is selected here rather than by passing RtAudio::UNSPECIFIED,
    // and the reason is measured, not stylistic: UNSPECIFIED probes devices
    // inside the RtAudio constructor, which is before any error callback can
    // exist, so a device that fails to answer prints to stderr and nothing
    // downstream can stop it. On this machine an iPhone offered as a
    // microphone but not connected fails its CoreAudio sample-rate query
    // every run. This loop is the same selection rule — the compiled APIs in
    // RtAudio's own order, first one that reports a device wins — with the
    // callback installed first, so a GUI does not narrate to a terminal
    // nobody is reading. `showWarnings` does not cover this: warnings and
    // errors are separate channels in RtAudio 6.
    std::vector<RtAudio::Api> apis;
    RtAudio::getCompiledApi(apis);
    if (apis.empty()) {
        if (err) *err = "RtAudio was compiled with no audio API";
        return devs;
    }

    for (const RtAudio::Api api : apis) {
        RtAudio audio(api, RtAudioErrorCallback(on_error));
        audio.showWarnings(false);
        const std::vector<unsigned int> ids = audio.getDeviceIds();
        if (ids.empty()) continue;
        for (const unsigned int id : ids) {
            const RtAudio::DeviceInfo info = audio.getDeviceInfo(id);
            if (info.inputChannels == 0) continue;
            InputDevice d;
            d.id = info.ID;
            d.name = info.name;
            d.channels = info.inputChannels;
            d.preferred_rate = info.preferredSampleRate;
            d.is_default = info.isDefaultInput;
            devs.push_back(std::move(d));
        }
        break;
    }

    if (devs.empty() && err)
        *err = last_error.empty() ? "no input devices" : last_error;
    return devs;
}

// ---------------------------------------------------------------------------
// The window. Creation and placement are deliberately separate: `create()`
// makes every widget once, `layout()` is the only code that decides where
// anything goes, and the window's resize() calls layout() again.
//
// This is not tidiness. FLTK's Fl_Group::resize scales any child that
// overlaps the resizable widget's span, and with resizable(image_pane) that
// is most of the window: dragging 980x700 to 1400x900 stretched the Device
// menu from 240 px to 370, grew the status rows from 20 px to 27, and — the
// one that matters — moved the ruler to x=7 over a pane whose interior
// starts at 6, so every tick named the wrong image column. Measured, on this
// machine, with --resize. One layout function means a window built at a size
// and a window dragged to it cannot disagree.
struct Shell;

class ShellWindow : public Fl_Double_Window {
public:
    ShellWindow(int w, int h, const char* t) : Fl_Double_Window(w, h, t) {}
    void resize(int X, int Y, int W, int H) override;
    Shell* shell = nullptr;
};

// The ruler above the image pane. It is the phase-entry affordance
// [docs/04: the ruler/coordinate pattern, re-confirmed session 16] and it
// reads IMAGE COLUMNS, tracking zoom and horizontal scroll [docs/05 §8.3
// items 1-3]. Every number it draws comes from live/ruler.hpp; there is no
// column arithmetic in this class.
//
// While the image width is unknown it is blank and disabled [§8.3 item 1]:
// in AUTO, before a start tone, Nova does not know whether the chart is
// 1810 or 905 columns wide, and a ruler drawn on a guess would be a lie in
// the one place a lie is most expensive.
class Ruler : public Fl_Widget {
public:
    Ruler(int x, int y, int w, int h) : Fl_Widget(x, y, w, h) {}
    Shell* shell = nullptr;
    void draw() override;
};

// The image pane's scrolling viewport [§8.3 item 3: scrollbars appear only
// when the image exceeds the pane, in both axes]. The subclass exists for
// one reason: the ruler tracks horizontal scroll, and FLTK has no "the user
// scrolled" callback — so a scroll position that changed under the drawing
// code redraws the ruler.
class ImageScroll : public Fl_Scroll {
public:
    ImageScroll(int x, int y, int w, int h) : Fl_Scroll(x, y, w, h) {}
    Ruler* ruler = nullptr;
    void draw() override {
        Fl_Scroll::draw();
        if (ruler && xposition() != last_x_) {
            last_x_ = xposition();
            ruler->redraw();
        }
    }

private:
    int last_x_ = 0;
};

// What lives inside the scroll: the picture. Its SIZE is what makes the
// scrollbars and the ruler agree about how wide the image is, and since
// session 23 it also has pixels — a copy of the engine's display image,
// taken on the GUI thread under the engine's image lock and then drawn
// with nothing held [engine.hpp: `copy_image`].
//
// The copy is not a waste. Rows arrive at two a second on a 120 lpm
// chart, and the alternative — drawing straight out of the live decode
// thread's buffer — is holding a lock across an FLTK blit, which is the
// one thing §2 says thread 4 must not do.
// §8.2's compact receiving indicator: state name, line count, thumbnail —
// what a transmission arriving BEHIND an edit gets instead of the pane.
//
// It is a widget rather than three fields because it is one control: the
// whole of it is the click target that brings the buffered picture forward,
// and §8.2 gives the pane exactly one way to change hands [Sara, session
// 30]. It shows nothing at all when nothing is buffered — an empty frame
// saying "not receiving" would be a permanent fixture reporting the normal
// case, and the sidebar's lower area is small.
class RecvIndicator : public Fl_Widget {
public:
    RecvIndicator(int x, int y, int w, int h) : Fl_Widget(x, y, w, h) {}

    // Nearest-neighbour into a small buffer rather than holding the whole
    // picture and scaling on draw: the buffered chart is up to 1810 px wide
    // and this is a 44 px thumbnail updated on every tick.
    void set(bool active, const char* state, int rows, bool complete,
             const nova::Image& src) {
        active_ = active;
        state_ = state ? state : "";
        rows_ = rows;
        complete_ = complete;
        thumb_.clear();
        rgb_.reset();
        if (active_ && src.width > 0 && src.height > 0) {
            thumb_w_ = kThumbW;
            thumb_h_ = std::min(
                kThumbH, std::max(1, static_cast<int>(
                                         static_cast<long long>(src.height) *
                                         kThumbW / src.width)));
            thumb_.resize(static_cast<std::size_t>(thumb_w_) *
                          static_cast<std::size_t>(thumb_h_));
            for (int ty = 0; ty < thumb_h_; ty++) {
                const int sy = static_cast<int>(
                    static_cast<long long>(ty) * src.height / thumb_h_);
                for (int tx = 0; tx < thumb_w_; tx++) {
                    const int sx = static_cast<int>(
                        static_cast<long long>(tx) * src.width / thumb_w_);
                    thumb_[static_cast<std::size_t>(ty) *
                               static_cast<std::size_t>(thumb_w_) +
                           static_cast<std::size_t>(tx)] =
                        src.px[static_cast<std::size_t>(sy) *
                                   static_cast<std::size_t>(src.width) +
                               static_cast<std::size_t>(sx)];
                }
            }
            rgb_.reset(new Fl_RGB_Image(thumb_.data(), thumb_w_, thumb_h_, 1));
        }
        redraw();
    }

    bool active() const { return active_; }
    int rows() const { return rows_; }
    bool complete() const { return complete_; }
    const std::string& state_name() const { return state_; }

    // The whole widget is the target [§8.2: "switches to the live view when
    // clicked"]. Inert when nothing is buffered, so a click on empty
    // sidebar cannot promote — the same rule as session 29's unarmed
    // picture click, for the same reason.
    int handle(int e) override {
        if (e == FL_PUSH && active_) {
            do_callback();
            return 1;
        }
        return 0;
    }

    void draw() override {
        if (!active_) return;
        draw_box(FL_UP_BOX, FL_BACKGROUND_COLOR);
        const int pad = 4;
        int tx = x() + w() - pad - kThumbW;
        if (rgb_) {
            rgb_->draw(tx, y() + pad);
            fl_color(FL_INACTIVE_COLOR);
            fl_rect(tx, y() + pad, thumb_w_, thumb_h_);
        }
        fl_color(FL_FOREGROUND_COLOR);
        fl_font(FL_HELVETICA_BOLD, kFontSize - 1);
        fl_draw("RECEIVING", x() + pad, y() + pad + 10);
        fl_font(FL_HELVETICA, kFontSize - 2);
        char buf[96];
        // The count is of the BUFFERED picture, never of the one on the
        // pane, so a glance says how much of the new transmission exists.
        std::snprintf(buf, sizeof(buf), "%s \xc2\xb7 %d %s", state_.c_str(),
                      rows_, rows_ == 1 ? "line" : "lines");
        fl_draw(buf, x() + pad, y() + pad + 24);
        fl_draw(complete_ ? "click to show (complete)" : "click to show",
                x() + pad, y() + pad + 37);
    }

private:
    static constexpr int kThumbW = 44;
    static constexpr int kThumbH = 40;
    bool active_ = false;
    bool complete_ = false;
    int rows_ = 0;
    std::string state_;
    std::vector<unsigned char> thumb_;
    int thumb_w_ = 0;
    int thumb_h_ = 0;
    std::unique_ptr<Fl_RGB_Image> rgb_;
};

class ImageView : public Fl_Widget {
public:
    ImageView(int x, int y, int w, int h) : Fl_Widget(x, y, w, h) {}

    // Takes the new picture. Returns true when the geometry changed, so
    // the caller knows to re-run layout rather than only redraw.
    bool set_image(const nova::Image& img) {
        const bool resized = img.width != img_.width ||
                             img.height != img_.height;
        img_ = img;
        // Fl_RGB_Image does not own the buffer it is given, so it is
        // rebuilt whenever `img_.px` may have moved.
        rgb_.reset(img_.width > 0 && img_.height > 0
                       ? new Fl_RGB_Image(img_.px.data(), img_.width,
                                          img_.height, 1)
                       : nullptr);
        redraw();
        return resized;
    }

    bool has_image() const { return img_.width > 0 && img_.height > 0; }
    int image_rows() const { return img_.height; }
    int image_cols() const { return img_.width; }

    void draw() override {
        fl_color(FL_BACKGROUND2_COLOR);
        fl_rectf(x(), y(), w(), h());
        if (!rgb_) return;
        // Scaled by the widget's own size, which layout_view() has already
        // set from the zoom. FLTK 1.4 scales on draw when told the target
        // size; the aspect flag is off because the two axes are not the
        // same thing — columns are IOC-derived, rows are lines received.
        const int dh = std::min(h(), static_cast<int>(std::lround(
                                         img_.height * scale_y())));
        rgb_->scale(w(), dh, 0, 1);
        rgb_->draw(x(), y());
    }

    // One image row is one received line, and one image column is
    // 1/(IOC*pi) of a line. The pane draws a row at the same scale as a
    // column so the chart is not stretched; `scale_y` is that scale.
    void set_scale_y(double s) { scale_y_ = s; }
    double scale_y() const { return scale_y_; }

    // --- click-to-set-PHASE [docs/05 §8.3 item 1, ROADMAP M4 item 5] -----
    // The image is a control now, and an image cannot go grey to say so.
    // So the CURSOR carries what a greyed button would: a crosshair
    // exactly where a click can act, the plain arrow where it cannot.
    // That is §3's "not offered and then found not to work" for a surface
    // with no button to grey — the reason line under Apply/Auto is still
    // the words, this is the affordance.
    // Session 29: this now follows the ARMING rather than "a correction is
    // possible" [see Arm]. The crosshair used to be on for the whole of a
    // correctable chart, which made it scenery; now it appears when a
    // gesture is waiting for a click and vanishes when the gesture is done,
    // so it says something true about the very next click.
    void set_click_enabled(bool e) { click_enabled_ = e; }
    bool click_enabled() const { return click_enabled_; }

    int handle(int event) override {
        switch (event) {
            case FL_ENTER:
            // FL_MOVE too, because arming happens while the pointer is
            // somewhere else entirely (on the button) and the operator may
            // come back onto the picture without crossing a boundary FLTK
            // reports as an enter — the cursor would then still be the
            // arrow over an armed picture, which is the affordance saying
            // the opposite of the truth.
            case FL_MOVE:
                fl_cursor(click_enabled_ ? FL_CURSOR_CROSS
                                         : FL_CURSOR_DEFAULT);
                return 1;
            case FL_LEAVE:
                fl_cursor(FL_CURSOR_DEFAULT);
                return 1;
            case FL_PUSH:
                // Not consumed when it cannot act: a click that is
                // swallowed and does nothing is session 26's finding 2,
                // and the pane below may still want it.
                if (!click_enabled_) return 0;
                do_callback();
                return 1;
            default:
                return Fl_Widget::handle(event);
        }
    }

private:
    nova::Image img_;
    std::unique_ptr<Fl_RGB_Image> rgb_;
    double scale_y_ = 1.0;
    bool click_enabled_ = false;
};

// ---------------------------------------------------------------------------
// The input level meter that stays in M4 while the waterfall goes to M4.5
// [docs/05 §8, decided session 17]: without a level readout, a muted or
// clipping input has no diagnosis and every failure looks like "no signal".
// With no capture running there is no level, and it says so rather than
// drawing an empty bar that could be read as silence.
class LevelMeter : public Fl_Widget {
public:
    LevelMeter(int x, int y, int w, int h) : Fl_Widget(x, y, w, h) {}

    // -120 dBFS means "no capture running", which is not the same as
    // silence and must not look like it.
    void set_level(double dbfs, bool live) {
        dbfs_ = dbfs;
        live_ = live;
        redraw();
    }

    void draw() override {
        fl_draw_box(FL_DOWN_BOX, x(), y(), w(), h(), FL_BACKGROUND_COLOR);
        const int bx = x() + 90, bw = w() - 90 - 90;
        if (live_ && bw > 0) {
            // -60 dBFS to 0, which is the range an HF receiver's line
            // output actually occupies; below that the diagnosis is "no
            // signal" and the bar has nothing useful to say.
            const double f = std::min(1.0, std::max(0.0, (dbfs_ + 60.0) / 60.0));
            const int fill = static_cast<int>(std::lround(bw * f));
            // Clipping is the failure the meter exists to catch, so the
            // top of the range is coloured rather than merely long.
            fl_color(dbfs_ >= -1.0 ? FL_RED
                                   : (dbfs_ >= -6.0 ? FL_DARK_YELLOW
                                                    : FL_DARK_GREEN));
            fl_rectf(bx, y() + 4, fill, h() - 8);
        }
        fl_font(FL_HELVETICA, kFontSize);
        fl_color(live_ ? FL_FOREGROUND_COLOR : FL_INACTIVE_COLOR);
        fl_draw("input level", x() + kPad, y(), w(), h(),
                FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        char buf[32];
        if (live_ && dbfs_ > -119.0)
            std::snprintf(buf, sizeof buf, "%.0f dBFS", dbfs_);
        else
            std::snprintf(buf, sizeof buf, "-- dBFS");
        fl_draw(buf, x(), y(), w() - kPad, h(),
                FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
    }

private:
    double dbfs_ = -120.0;
    bool live_ = false;
};

// ---------------------------------------------------------------------------
// M4.5's tuning strip [ROADMAP M4.5; cut from M4 on 2026-08-13, session 17;
// built session 36]. A waterfall with the instantaneous spectrum along its
// top edge and the two WMO tones marked, over 800-3000 Hz.
//
// It is the one region of the window that serves TUNING rather than
// decoding, and the one with no precedent in the sixteen-manual receiver
// corpus [docs/04] — an SDR-era affordance, which is exactly why it
// deferred out of M4 cleanly. What earns it a place now is that every other
// readout in the window answers "is Nova decoding this", and none of them
// answers "is the radio on the right frequency".
//
// The widget DECIDES NOTHING. Columns, their frequencies and the marker
// positions all arrive from `nova::SpectrumAnalyzer` through the engine, so
// the ticks under the picture and the columns above them cannot disagree
// about where 1500 Hz is — the mistake live/ruler.hpp exists to prevent,
// made once already in session 20.
class TuningStrip : public Fl_Widget {
public:
    TuningStrip(int x, int y, int w, int h) : Fl_Widget(x, y, w, h) {}

    // The band's geometry: how many columns it has, and which of them hold
    // the two WMO tones (-1 if the band does not contain one). Set ONCE, at
    // construction, from `nova::spectrum_hz_column` — it is pure arithmetic
    // over the band, so the marker lines do not wait for a sound card and
    // are reportable by --metrics on a machine with no audio at all.
    void set_markers(int columns, int black, int white) {
        columns_ = columns;
        black_ = black;
        white_ = white;
        redraw();
    }

    // Newest row first, `columns` wide, values 0..1.
    void set_data(std::vector<float> rows, int columns, int nrows) {
        rows_ = std::move(rows);
        if (columns > 0) columns_ = columns;
        nrows_ = nrows;
        redraw();
    }
    int strip_rows() const { return nrows_; }
    int black_col() const { return black_; }
    int white_col() const { return white_; }

    void draw() override {
        fl_draw_box(FL_DOWN_BOX, x(), y(), w(), h(), FL_BLACK);
        const int ix = x() + kFrame, iw = w() - 2 * kFrame;
        if (iw <= 0) return;
        const int trace_y = y() + kFrame;
        const int wf_y = trace_y + kStripTraceH;
        const int axis_y = wf_y + kStripWaterfallH;

        // Nothing measured is not a quiet band, and must not be drawn as
        // one — the same distinction the level meter makes with "-- dBFS".
        // An operator with a dead input who reads an empty waterfall as
        // "nothing on this frequency" will go and tune the radio.
        if (nrows_ <= 0 || columns_ <= 0) {
            fl_font(FL_HELVETICA, kFontSize);
            fl_color(FL_INACTIVE_COLOR);
            fl_draw("tuning strip — no input measured", x(), y(), w(), h(),
                    FL_ALIGN_CENTER | FL_ALIGN_INSIDE);
            return;
        }

        draw_waterfall(ix, wf_y, iw);
        draw_trace(ix, trace_y, iw);
        draw_markers(ix, trace_y, iw, axis_y);
        draw_axis(ix, axis_y, iw);
    }

private:
    // The colour ramp: black -> blue -> cyan -> yellow -> white. Chosen so
    // that the marker colour (magenta) is nowhere in it — a marker line the
    // signal can imitate is a marker line that can be misread.
    static void ramp(float v, unsigned char* rgb) {
        const float t = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        float r, g, b;
        if (t < 0.25f) {
            const float u = t * 4.0f;
            r = 0.0f; g = 0.0f; b = 0.06f + 0.49f * u;
        } else if (t < 0.5f) {
            const float u = (t - 0.25f) * 4.0f;
            r = 0.0f; g = 0.78f * u; b = 0.55f + 0.45f * u;
        } else if (t < 0.75f) {
            const float u = (t - 0.5f) * 4.0f;
            r = u; g = 0.78f + 0.22f * u; b = 1.0f - u;
        } else {
            const float u = (t - 0.75f) * 4.0f;
            r = 1.0f; g = 1.0f; b = u;
        }
        rgb[0] = static_cast<unsigned char>(r * 255.0f + 0.5f);
        rgb[1] = static_cast<unsigned char>(g * 255.0f + 0.5f);
        rgb[2] = static_cast<unsigned char>(b * 255.0f + 0.5f);
    }

    // One row of data per row of pixels — the analyser is asked for exactly
    // kStripWaterfallH rows of history, so this neither decimates nor
    // stretches, and a fade that lasted one column occupies one line.
    void draw_waterfall(int ix, int wy, int iw) {
        buf_.resize(static_cast<std::size_t>(iw) * kStripWaterfallH * 3);
        for (int py = 0; py < kStripWaterfallH; py++) {
            // Newest at the TOP, history falling away below it.
            const int r = py;
            const float* src =
                r < nrows_ ? &rows_[static_cast<std::size_t>(r) * columns_]
                           : nullptr;
            for (int px = 0; px < iw; px++) {
                unsigned char* o =
                    &buf_[(static_cast<std::size_t>(py) * iw + px) * 3];
                if (!src) {
                    o[0] = o[1] = o[2] = 0;
                    continue;
                }
                const int c = px * columns_ / iw;
                ramp(src[c], o);
            }
        }
        fl_draw_image(buf_.data(), ix, wy, iw, kStripWaterfallH, 3);
    }

    void draw_trace(int ix, int ty, int iw) {
        if (nrows_ <= 0) return;
        const float* now = &rows_[0];
        fl_color(FL_GREEN);
        fl_begin_line();
        for (int px = 0; px < iw; px++) {
            const int c = px * columns_ / iw;
            const float v = now[c] < 0.0f ? 0.0f : (now[c] > 1.0f ? 1.0f
                                                                  : now[c]);
            fl_vertex(ix + px,
                      ty + kStripTraceH - 1 -
                          static_cast<int>(v * (kStripTraceH - 1)));
        }
        fl_end_line();
    }

    // The two tones, marked through the trace AND the waterfall so that
    // "the signal sits on the line" is one judgement and not two.
    void draw_markers(int ix, int ty, int iw, int axis_y) {
        fl_color(fl_rgb_color(255, 64, 255));
        for (int c : {black_, white_}) {
            if (c < 0 || columns_ <= 0) continue;
            const int px = ix + c * iw / columns_;
            fl_line(px, ty, px, axis_y - 1);
        }
    }

    void draw_axis(int ix, int ay, int iw) {
        fl_font(FL_HELVETICA, 9);
        fl_color(FL_GRAY);
        // The band ends, so a signal off the edge has a direction.
        fl_draw("800", ix + 2, ay, 30, kStripAxisH,
                FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        fl_draw("3000", ix, ay, iw - 2, kStripAxisH,
                FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
        fl_color(fl_rgb_color(255, 64, 255));
        if (black_ >= 0 && columns_ > 0)
            fl_draw("1500", ix + black_ * iw / columns_ - 14, ay, 28,
                    kStripAxisH, FL_ALIGN_CENTER | FL_ALIGN_INSIDE);
        if (white_ >= 0 && columns_ > 0)
            fl_draw("2300", ix + white_ * iw / columns_ - 14, ay, 28,
                    kStripAxisH, FL_ALIGN_CENTER | FL_ALIGN_INSIDE);
    }

    std::vector<float> rows_;
    std::vector<unsigned char> buf_;
    int columns_ = 0;
    int nrows_ = 0;
    int black_ = -1;
    int white_ = -1;
};

constexpr int kFields = 6;  // Mode, IOC, Rate, State, Quality, Started

struct Shell {
    ShellWindow* win = nullptr;
    Fl_Menu_Bar* menu = nullptr;
    Fl_Box* cap_device = nullptr;
    Fl_Choice* device = nullptr;
    Fl_Box* cap_ioc = nullptr;
    Fl_Choice* ioc = nullptr;
    Fl_Box* cap_rate = nullptr;
    Fl_Choice* rate = nullptr;
    Fl_Box* cap_zoom = nullptr;
    Fl_Choice* zoom = nullptr;
    Fl_Button* start = nullptr;
    Fl_Button* force = nullptr;
    Ruler* ruler = nullptr;
    ImageScroll* pane = nullptr;
    ImageView* view = nullptr;
    Fl_Box* panel = nullptr;
    Fl_Box* title = nullptr;
    Fl_Box* field_cap[kFields] = {};
    Fl_Box* field_val[kFields] = {};
    Fl_Box* cap_label = nullptr;
    Fl_Input* label_input = nullptr;
    Fl_Box* rule = nullptr;
    Fl_Box* cap_phase = nullptr;
    Fl_Int_Input* phase_input = nullptr;
    Fl_Box* cap_sync = nullptr;
    Fl_Float_Input* sync_input = nullptr;
    // The two arming buttons [see Arm]. One per gesture, beside the box the
    // gesture fills in, because that is the only place they can be that
    // says which value the click is going to move.
    Fl_Button* phase_arm = nullptr;
    Fl_Button* sync_arm = nullptr;
    // Coarse and fine, either way [see sync_step]. They follow the BOX,
    // never the buttons: a stepper is only ever offered where the value it
    // steps can be typed.
    static constexpr int kSyncSteps = 4;
    Fl_Button* sync_step_btn[kSyncSteps] = {nullptr, nullptr, nullptr,
                                            nullptr};
    Fl_Button* apply = nullptr;
    Fl_Button* autob = nullptr;
    // §8.2's receiving indicator, in the sidebar's lower area — the space
    // §8.3 item 5 already reserved for it when it declined to reserve any
    // for the M4.5 waterfall.
    RecvIndicator* recv = nullptr;
    Fl_Box* correct_why = nullptr;   // §3's "with the reason shown"
    std::string correct_reason;      // ...and the string it is showing

    // §8.5 item 4: an edit is dirty controls, not a mode. It BEGINS at the
    // first change to PHASE or SYNC and ENDS at Apply, at Auto, or when the
    // pane stops showing the chart being corrected — which is this shell's
    // form of "a switch to the live view" until §8.2's background buffer
    // (ROADMAP item 6) gives the operator a second picture to switch away
    // from. A typed-but-not-applied value is an edit in progress.
    bool edit_dirty = false;
    // Which gesture the next click on the picture belongs to, or kNone for
    // "the picture is not listening" [see Arm]. Session 29: this is now the
    // ONLY thing that lets a click act.
    Arm arm = Arm::kNone;
    // The first click of a two-click SYNC measurement, in image
    // coordinates; -1 for none. Only ever set while `arm` is kSync, so it
    // is a step within a declared gesture rather than a mode of its own.
    int pending_col = -1;
    int pending_row = -1;
    // What the last completed slant measurement was worth, as a sentence,
    // or empty. Session 29: a short baseline MEASURES and says what it is
    // worth [see slant_error_ppm], so this is the "says what it is worth"
    // half and it has to outlive the click that produced it — the operator
    // reads it after the number lands in the box, not during.
    std::string slant_note;
    bool was_rerendering = false;   // so drain notices one start or finish
    // What the picture on the pane was last re-rendered WITH, so Auto knows
    // whether it has anything to undo. Empty means the picture is the
    // measured render, which is what every transmission starts as [§8.5
    // item 6: measured-or-blank, and no memory between transmissions].
    nova::Correction applied;
    LevelMeter* meter = nullptr;
    // M4.5's tuning strip and whether it is showing. Default ON [Sara,
    // session 36]: a tuning aid nobody sees is a tuning aid nobody uses.
    TuningStrip* strip = nullptr;
    bool strip_on = true;
    // One description of the strip's band, shared by the widget's markers
    // and the engine's analyser. `history` is the waterfall's pixel height
    // so that every retained row is a drawn row.
    nova::SpectrumOptions strip_opt = [] {
        nova::SpectrumOptions o;
        o.history = kStripWaterfallH;
        return o;
    }();
    Fl_Box* status_bg = nullptr;
    Fl_Box* status_state = nullptr;
    Fl_Box* status_lines = nullptr;
    Fl_Progress* progress = nullptr;
    Fl_Window* about = nullptr;

    std::vector<InputDevice> devices;
    std::string device_error;
    std::vector<std::pair<const char*, Fl_Widget*>> named;

    Prefs prefs;
    std::string image_folder;

    LiveState state = LiveState::kIdle;
    // Whether anything is behind the transport. Under --metrics there is
    // no engine and no device, and --state sets this so the §8.3/§8.4
    // rules stay inspectable without the shell pretending; in a real
    // window it is true once an engine exists.
    bool capture = false;
    // The measured image width, once a decode has produced one. Zero means
    // "not measured", and then only an explicit IOC gives the ruler a width.
    int measured_cols = 0;

    // --- the live half [docs/05 §2] ----------------------------------------
    // Created only when a window is actually shown: an inspection run
    // (--metrics, --devices) must not open a sound card, and the two GUI
    // screamers depend on that staying true.
    std::unique_ptr<nova::LiveEngine> engine;
    std::unique_ptr<RtAudio> audio;
    bool audio_open = false;
    // Index into `devices` of the device the stream is open on, so picking
    // the entry that is already live does not tear down a healthy stream.
    int open_device = -1;
    std::string audio_error;
    unsigned int audio_rate = 0;
    // What the drained messages last said, for the status panel.
    double level_dbfs = -120.0;
    unsigned long long overruns = 0;
    int rows_drawn = 0;
    std::string last_saved;
    std::string last_error;
    // How far thread 2 has got through the audio, and how many saves it has
    // announced. Neither is shown to the operator: they are what the offline
    // capture waits on [see settle, stop_capture_and_wait], and they are read
    // off the SAME message stream the panel is, so an inspection cannot wait
    // on a progress signal the shell itself does not see.
    double consumed_sec = 0.0;
    int saves_seen = 0;

    // One fixture, its samples, and how far into it the capture has fed
    // [see feed_wav]. The cursor is what lets one recording be two
    // transmissions.
    struct Feed {
        std::vector<float> samples;
        int rate = 0;
        std::size_t at = 0;
    };
    std::map<std::string, Feed> feeds;
    // Fl_Widget::label stores the POINTER, not the text, so every
    // formatted label needs storage that outlives the call.
    char quality_buf[64] = "--";
    char lines_buf[32] = "line --";
    char status_buf[160] = "IDLE";
    // The clock the picture on the pane was DRAWN on — the same number the
    // Quality field is showing. It is where a SYNC nudge starts from when
    // the box is blank [see sync_step], so it is recorded wherever that
    // field is written and nowhere else.
    double shown_ppm = 0.0;
    bool shown_ppm_valid = false;

    // The RtAudio callback [thread 1, realtime]. It does exactly one
    // thing, which is §2's whole rule for this thread: no allocation, no
    // lock, no log, no throw.
    static int audio_cb(void*, void* in, unsigned int frames, double,
                        RtAudioStreamStatus status, void* user) {
        Shell* s = static_cast<Shell*>(user);
        if (in && s->engine)
            s->engine->push_audio(static_cast<const float*>(in), frames);
        // An RtAudio overflow is the same species of loss as a ring
        // overrun and is counted the same way — by pushing nothing and
        // letting the sample accounting show the gap.
        (void)status;
        return 0;
    }

    void note(const char* name, Fl_Widget* w) { named.emplace_back(name, w); }

    // A left-hand caption for a control. FLTK draws a widget's own label
    // outside its box, which makes a row's geometry depend on text width;
    // an explicit box keeps every column a number chosen here.
    Fl_Box* caption(const char* text) {
        Fl_Box* b = new Fl_Box(0, 0, 0, 0, text);
        b->box(FL_NO_BOX);
        b->labelsize(kFontSize);
        b->align(FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
        return b;
    }

    // A status value. It reads "--" rather than being blank, because blank
    // is a legal value for the operator label and the two must not look
    // alike [docs/05 §8.1].
    Fl_Box* value(const char* text) {
        Fl_Box* b = new Fl_Box(0, 0, 0, 0, text);
        b->box(FL_NO_BOX);
        b->labelsize(kFontSize);
        b->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        return b;
    }

    void build(int win_w, int win_h, const char* argv0,
               bool strip_given = false, bool strip_arg = true) {
        FL_NORMAL_SIZE = kFontSize;
        prefs.open(executable_dir(argv0));
        image_folder = prefs.get("image_folder");
        // Read BEFORE the window is built: the first layout has to be the
        // remembered one, or the window visibly reflows on startup.
        // Default on, so a missing key and a "1" mean the same thing and
        // only an explicit "0" hides it.
        strip_on = prefs.get("tuning_strip") != "0";
        // --strip outranks the remembered value and does NOT write it back:
        // an inspection run must be able to ask for a layout without
        // changing what the operator will see next time they open Nova.
        if (strip_given) strip_on = strip_arg;
        win = new ShellWindow(win_w, win_h, "Nova");
        win->shell = this;
        note("window", win);
        create();
        win->end();
        // No resizable() child: the layout is computed, not scaled. FLTK
        // still lets the user resize because size_range says so.
        // kMinH is unchanged by M4.5, checked rather than assumed: at
        // 880x420 with the strip showing the picture area is still 258 px.
        // Raising the minimum would have forbidden window sizes that
        // worked before, to protect a layout that does not need it.
        win->size_range(kMinW, kMinH);
        layout(win_w, win_h);
        populate_devices();
        apply_state();
    }

    // Which of the two surfaces the correction boxes serve RIGHT NOW: the
    // live preview being drawn [§7], or a decoded picture to be re-rendered
    // [§7.1, §8.5]. **One definition, because `apply_state` and `cb_apply`
    // must never disagree about it** — a reason line reading "applies from
    // the next row drawn" over a button that re-renders a file is two
    // answers to one question, and the operator only sees one of them.
    //
    // **The buffer is part of the question** [session 31]. Until session 30
    // the answer was the session state alone, resting on a premise stated in
    // `apply_state` and true for thirteen sessions: a picture is either
    // being drawn or has been decoded, never both. §8.2's background buffer
    // is exactly the case where both hold — a decoded chart on the pane, a
    // transmission drawing behind the indicator — and the old rule then
    // answered "live" about a preview THE OPERATOR CANNOT SEE. Measured, not
    // reasoned: with a transmission buffered, Apply left `edit_dirty` set
    // and re-rendered nothing, while Auto — which never asked the question —
    // correctly re-rendered the held chart. Two buttons side by side giving
    // one state two answers is a defect, not a decision.
    //
    // So the surface follows THE PANE, not the session: while anything is
    // buffered the pane is showing a decoded picture, whatever the session
    // is doing. `background().active` and not `pane_held` deliberately —
    // the buffer outlives the hold [Sara, session 30: it stays buffered
    // until the indicator is clicked], and so does the pane's contents.
    bool live_surface() const {
        return capture && state == LiveState::kDrawingPreview &&
               !(engine && engine->background().active);
    }

    // --- the two dropdowns the transport reads ------------------------------
    // "Auto" is index 0 in both, and it is a value in the same list as the
    // numbers, never a separate mode toggle [docs/04 Finding 2].
    bool ioc_explicit() const { return ioc->value() > 0; }
    bool rate_explicit() const { return rate->value() > 0; }
    int ioc_value() const {
        switch (ioc->value()) {
            case 1: return 576;
            case 2: return 288;
            default: return 0;
        }
    }
    double rate_value() const {
        switch (rate->value()) {
            case 1: return 60.0;
            case 2: return 90.0;
            case 3: return 120.0;
            default: return 0.0;
        }
    }

    // The image width in columns, or 0 while it is unknown. A measured
    // width wins; an operator-set IOC is a declaration, not a guess, so it
    // counts too; AUTO before a start tone is the unknown case that keeps
    // the ruler blank [§8.3 item 1].
    int image_cols() const {
        if (measured_cols > 0) return measured_cols;
        const int v = ioc_value();
        return v > 0 ? static_cast<int>(std::lround(v * kPi)) : 0;
    }

    nova::Zoom zoom_value() const {
        switch (zoom->value()) {
            case 1: return nova::Zoom::k25;
            case 2: return nova::Zoom::k50;
            case 3: return nova::Zoom::k100;
            case 4: return nova::Zoom::k200;
            default: return nova::Zoom::kFit;
        }
    }

    // The pane's interior width: the FL_DOWN_BOX bevel off both edges, less
    // the vertical scrollbar when one is showing. This is the width the
    // ruler spans and the width Fit scales into, so the two cannot drift.
    int pane_interior_w() const {
        return pane->w() - 2 * kFrame - (vscroll_visible() ? Fl::scrollbar_size()
                                                           : 0);
    }
    int pane_interior_h() const {
        return pane->h() - 2 * kFrame - (hscroll_visible() ? Fl::scrollbar_size()
                                                           : 0);
    }

    // Scrollbar visibility is computed here rather than read off Fl_Scroll,
    // because --metrics runs without ever showing a window and Fl_Scroll
    // decides its scrollbars while drawing. The rule is the same one it
    // uses: a bar appears exactly when the image exceeds the pane [§8.3
    // item 3].
    //
    // Vertically, once a chart is taller than the pane — which session 23
    // made possible and session 20's version of this function said would
    // come: a ten-minute chart is ~1200 rows against a 613 px pane. With
    // no picture the answer is still no, because the scrolled child is
    // then sized to the pane.
    bool vscroll_visible() const {
        if (!view || !view->has_image()) return false;
        const int cols = image_cols();
        if (cols <= 0) return false;
        const double scale = nova::zoom_scale(zoom_value(), cols,
                                              pane->w() - 2 * kFrame);
        return view->image_rows() * scale > pane->h() - 2 * kFrame;
    }
    // Horizontally, whenever a known image width at a FIXED zoom exceeds
    // the pane. Fit cannot scroll by construction — its scale is defined
    // as pane / cols — which is also why this cannot recurse through
    // pane_interior_w(): only the fixed zooms reach the comparison, and
    // their scale does not depend on the pane at all.
    bool hscroll_visible() const {
        const int cols = image_cols();
        if (cols <= 0 || zoom_value() == nova::Zoom::kFit) return false;
        const double scale = nova::zoom_scale(zoom_value(), cols, 0);
        return cols * scale > pane->w() - 2 * kFrame;
    }

    // The view the ruler draws from and the click handler will read. Built
    // fresh from the widgets each time, so no copy of the geometry can go
    // stale.
    nova::RulerView view_state() const {
        nova::RulerView v;
        v.image_cols = image_cols();
        v.pane_px = pane_interior_w();
        v.scale = nova::zoom_scale(zoom_value(), v.image_cols, v.pane_px);
        return nova::scrolled(v, pane->xposition());
    }

    void create() {
        create_menu_bar();
        create_control_row();
        create_image_pane();
        create_status_panel();
        create_correction_boxes();
        create_correction_controls();
        create_meter_and_status();
    }

    // --- menu bar [§8.3 items 7-8] --------------------------------------
    // No receiver in the corpus has one, and that is not an objection:
    // the survey constrains the picture-correction surface, not whether
    // a desktop application has desktop chrome. Buttons were rejected
    // because the control row is already the window's width constraint.
    void create_menu_bar() {
        menu = new Fl_Menu_Bar(0, 0, 0, 0);
        menu->textsize(kFontSize);
        menu->add("File/Quit", FL_COMMAND + 'q', cb_quit, this);
        menu->add("Settings/Image folder...", 0, cb_folder, this);
        // View sits between Settings and Help, which is where a desktop
        // application puts it. FL_MENU_TOGGLE draws the tick; the state is
        // set from prefs below, so the menu agrees with the window from
        // the first frame rather than after the first click.
        menu->add("View/Tuning strip", 0, cb_strip, this, FL_MENU_TOGGLE);
        menu->add("Help/About Nova", 0, cb_about, this);
        sync_strip_menu();
        note("menu_bar", menu);
    }

    // --- control row [§8 row 1] -----------------------------------------
    void create_control_row() {
        cap_device = caption("Device");
        device = new Fl_Choice(0, 0, 0, 0);
        device->textsize(kFontSize);
        // The only control on this row whose choice moves a sound card:
        // switching reopens the stream on the new device [§8.3 item 9].
        device->callback(cb_device, this);
        note("device_choice", device);

        cap_ioc = caption("IOC");
        ioc = new Fl_Choice(0, 0, 0, 0);
        ioc->textsize(kFontSize);
        // AUTO is a value in the same list as the numbers, never a separate
        // mode toggle [docs/04 Finding 2]. Same idiom the core already uses,
        // where ioc = 0 means "measure it".
        ioc->add("Auto");
        ioc->add("576");
        ioc->add("288");
        ioc->value(0);
        ioc->callback(cb_geometry, this);
        note("ioc_choice", ioc);

        cap_rate = caption("Rate");
        rate = new Fl_Choice(0, 0, 0, 0);
        rate->textsize(kFontSize);
        rate->add("Auto");
        rate->add("60 lpm");
        rate->add("90 lpm");
        rate->add("120 lpm");
        rate->value(0);
        rate->callback(cb_geometry, this);
        note("rate_choice", rate);

        // Zoom [§8.3 item 2]. Fit is a value in the same dropdown, never a
        // separate checkbox. The range extends below Fit because a
        // 1810-column chart at 100% shows 43% of itself in the default
        // window, and above 100% because at Fit one screen pixel is 2.3
        // image columns and PHASE placement is a per-column judgement.
        cap_zoom = caption("Zoom");
        zoom = new Fl_Choice(0, 0, 0, 0);
        zoom->textsize(kFontSize);
        zoom->add("Fit");
        zoom->add("25%");
        zoom->add("50%");
        zoom->add("100%");
        zoom->add("200%");
        zoom->value(0);
        zoom->callback(cb_zoom, this);
        note("zoom_choice", zoom);

        // Forced start is on every one of the sixteen receivers without
        // exception [docs/04 Finding 2], so it is a peer of Start here, not
        // something folded into a menu.
        start = new Fl_Button(0, 0, 0, 0, "Start");
        start->labelsize(kFontSize);
        start->callback(cb_start, this);
        start->deactivate();
        note("start_button", start);
        force = new Fl_Button(0, 0, 0, 0, "Force Start");
        force->labelsize(kFontSize);
        force->callback(cb_force, this);
        force->deactivate();
        note("force_start_button", force);
    }

    // --- image pane, its scroll and its ruler -----------------------------
    void create_image_pane() {
        ruler = new Ruler(0, 0, 0, 0);
        ruler->shell = this;
        ruler->deactivate();  // blank until the image width is known
        note("ruler", ruler);
        pane = new ImageScroll(0, 0, 0, 0);
        pane->box(FL_DOWN_BOX);
        pane->color(FL_BACKGROUND2_COLOR);
        pane->ruler = ruler;
        note("image_pane", pane);
        pane->begin();
        view = new ImageView(0, 0, 0, 0);
        view->callback(cb_image_click, this);
        note("image_view", view);
        pane->end();
    }

    // --- status panel [§8 right, fields per §8.1] -------------------------
    // §8.1 is why there is no frequency, channel or call sign here: every
    // receiver in the corpus contains its own radio, and Nova is a
    // decoder on the end of a cable from someone else's.
    void create_status_panel() {
        panel = new Fl_Box(0, 0, 0, 0);
        panel->box(FL_UP_BOX);
        note("status_panel", panel);
        title = new Fl_Box(0, 0, 0, 0, "STATUS");
        title->box(FL_NO_BOX);
        title->labelsize(kFontSize);
        title->labelfont(FL_HELVETICA_BOLD);
        title->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

        static const char* kFieldNames[kFields] = {"Mode", "IOC",     "Rate",
                                                   "State", "Quality", "Started"};
        static const char* kFieldValues[kFields] = {"AUTO", "--",   "--",
                                                    "IDLE", "--",   "--"};
        static const char* kFieldTags[kFields] = {
            "field_mode",  "field_ioc",     "field_rate",
            "field_state", "field_quality", "field_started"};
        for (int i = 0; i < kFields; i++) {
            field_cap[i] = caption(kFieldNames[i]);
            field_val[i] = value(kFieldValues[i]);
            note(kFieldTags[i], field_val[i]);
        }

        // The operator-typed label [§8.1, decided session 17]. Blank by
        // default and legitimately blank — it is not a detected station.
        cap_label = caption("Label");
        label_input = new Fl_Input(0, 0, 0, 0);
        label_input->textsize(kFontSize);
        note("label_input", label_input);

        rule = new Fl_Box(0, 0, 0, 0);
        rule->box(FL_THIN_DOWN_FRAME);
    }

    // The two manual corrections, and the only two on any receiver in
    // the corpus that has controls [docs/04, confirmed twice]. They are
    // asymmetric behind the glass — PHASE seeds the batch anchor search,
    // SYNC is only a fallback where the batch fit has no baseline
    // [docs/05 §7.1] — but that asymmetry is not in this shell, which
    // has no batch decode to hand them to. They stay deactivated for the
    // same reason: an image with no raw behind it shows them visibly
    // disabled rather than silently inert [§3].
    // Both captions are named regions from session 29, because the
    // layout makes a CLAIM through them. The claim changed in session 30
    // — the two controls are now matching caption/box/arm rows with
    // SYNC's steppers adjacent beneath, rather than SYNC's caption
    // holding a row alone so the steppers could flank the box — but the
    // reason for measuring it did not: a claim nobody can measure is a
    // comment.
    void create_correction_boxes() {
        cap_phase = caption("PHASE");
        note("cap_phase", cap_phase);
        phase_input = new Fl_Int_Input(0, 0, 0, 0);
        phase_input->textsize(kFontSize);
        phase_input->when(FL_WHEN_CHANGED);
        phase_input->callback(cb_edit, this);
        phase_input->deactivate();
        note("phase_input", phase_input);
        cap_sync = caption("SYNC");
        note("cap_sync", cap_sync);
        sync_input = new Fl_Float_Input(0, 0, 0, 0);
        sync_input->textsize(kFontSize);
        sync_input->when(FL_WHEN_CHANGED);
        sync_input->callback(cb_edit, this);
        sync_input->deactivate();
        note("sync_input", sync_input);
    }

    void create_correction_controls() {
        // The two arming buttons [see Arm]. FL_TOGGLE_BUTTON because armed
        // is a state the operator can see and leave, not an action: the
        // pushed box IS the third witness that the gesture is live, beside
        // the reason line and the crosshair. The tooltip carries what the
        // symbol cannot, because a circle is not self-explaining and this
        // surface's rule is that it explains itself.
        phase_arm = new Fl_Button(0, 0, 0, 0, "@circle");
        phase_arm->type(FL_TOGGLE_BUTTON);
        phase_arm->labelsize(kFontSize - 3);
        phase_arm->tooltip("Click the dead sector on the picture to set PHASE");
        phase_arm->callback(cb_arm_phase, this);
        phase_arm->deactivate();
        note("phase_arm", phase_arm);
        sync_arm = new Fl_Button(0, 0, 0, 0, "@circle");
        sync_arm->type(FL_TOGGLE_BUTTON);
        sync_arm->labelsize(kFontSize - 3);
        sync_arm->tooltip(
            "Click one feature twice, far apart, to measure the slant");
        sync_arm->callback(cb_arm_sync, this);
        sync_arm->deactivate();
        note("sync_arm", sync_arm);

        // The four steppers [see sync_step]. Labels carry the sign so the
        // direction is readable without the caption, and the coarse pair
        // sits outside the fine pair so the row reads as a scale.
        static const char* kStepLabels[kSyncSteps] = {"-10", "-1", "+1",
                                                      "+10"};
        static const char* kStepTags[kSyncSteps] = {
            "sync_step_m10", "sync_step_m1", "sync_step_p1", "sync_step_p10"};
        static Fl_Callback* const kStepCbs[kSyncSteps] = {
            cb_sync_m10, cb_sync_m1, cb_sync_p1, cb_sync_p10};
        for (int i = 0; i < kSyncSteps; i++) {
            sync_step_btn[i] = new Fl_Button(0, 0, 0, 0, kStepLabels[i]);
            sync_step_btn[i]->labelsize(kFontSize - 1);
            sync_step_btn[i]->callback(kStepCbs[i], this);
            sync_step_btn[i]->deactivate();
            note(kStepTags[i], sync_step_btn[i]);
        }

        apply = new Fl_Button(0, 0, 0, 0, "Apply");
        apply->labelsize(kFontSize);
        apply->callback(cb_apply, this);
        apply->deactivate();
        note("apply_button", apply);
        autob = new Fl_Button(0, 0, 0, 0, "Auto");
        autob->labelsize(kFontSize);
        autob->callback(cb_auto, this);
        autob->deactivate();
        note("auto_button", autob);

        // §3: "The PHASE/SYNC controls must then be visibly disabled with
        // the reason shown — not silently inert. Manual adjustment [ISO
        // §4.2.6, §5.4.3] ... is not offered and then found not to work."
        // This is that sentence. It sits directly under the two buttons it
        // explains rather than in the sidebar's empty lower area, which §8
        // already spoke for (§8.2's receiving indicator).
        correct_why = new Fl_Box(0, 0, 0, 0);
        correct_why->labelsize(kFontSize - 1);
        correct_why->labelfont(FL_HELVETICA_ITALIC);
        correct_why->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
        correct_why->labelcolor(FL_INACTIVE_COLOR);
        note("correct_why", correct_why);

        // §8.2's receiving indicator [ROADMAP M4 item 6]. It draws nothing
        // until a transmission is actually buffered behind an edit, so the
        // sidebar looks exactly as it did before in the normal case.
        recv = new RecvIndicator(0, 0, 0, 0);
        recv->callback(cb_recv, this);
        note("recv_indicator", recv);
    }

    // --- level meter and status line -------------------------------------
    void create_meter_and_status() {
        strip = new TuningStrip(0, 0, 0, 0);
        note("tuning_strip", strip);
        // The markers are geometry, not measurement: they come from the
        // same SpectrumOptions the engine will be handed, through the same
        // function the analyser itself uses. So the line under the picture
        // and the columns above it cannot disagree about where 1500 Hz is
        // — and they are right before any audio has arrived, which is when
        // an operator is most likely to be looking at them.
        strip->set_markers(strip_opt.columns,
                           nova::spectrum_hz_column(strip_opt, kBlackHz),
                           nova::spectrum_hz_column(strip_opt, kWhiteHz));
        meter = new LevelMeter(0, 0, 0, 0);
        note("level_meter", meter);

        status_bg = new Fl_Box(0, 0, 0, 0);
        status_bg->box(FL_UP_BOX);
        note("status_line", status_bg);
        // The state name, never a percentage [docs/04 Finding 3: zero
        // progress percentages in sixteen manuals].
        status_state = value("IDLE");
        note("status_state", status_state);
        status_lines = value("line --");
        note("status_lines", status_lines);
        // Populated only during DECODING, from the nine decode stages
        // [docs/05 §8].
        progress = new Fl_Progress(0, 0, 0, 0);
        progress->minimum(0.0f);
        progress->maximum(1.0f);
        progress->value(0.0f);
        progress->deactivate();
        note("status_progress", progress);
    }

    // The single source of geometry. Everything here is a function of the
    // window size and the metric constants; nothing reads a widget's current
    // position, so calling it twice is the same as calling it once.
    void layout(int W, int H) {
        menu->resize(0, 0, W, kMenuH);

        int x = kPad;
        const int row_y = kMenuH;
        cap_device->resize(x, row_y, 44, kControlRowH);
        x += 46;
        device->resize(x, row_y + 2, 240, kControlRowH - 4);
        x += 250;
        cap_ioc->resize(x, row_y, 26, kControlRowH);
        x += 28;
        ioc->resize(x, row_y + 2, 84, kControlRowH - 4);
        x += 94;
        cap_rate->resize(x, row_y, 32, kControlRowH);
        x += 34;
        rate->resize(x, row_y + 2, 92, kControlRowH - 4);
        x += 102;
        cap_zoom->resize(x, row_y, 34, kControlRowH);
        x += 36;
        zoom->resize(x, row_y + 2, 84, kControlRowH - 4);
        force->resize(W - kPad - 96, row_y + 2, 96, kControlRowH - 4);
        start->resize(W - kPad - 164, row_y + 2, 64, kControlRowH - 4);

        const int main_y = kMenuH + kControlRowH;
        // The strip takes its height from the picture area when it is
        // showing and gives every pixel back when it is not — the whole
        // point of it being a toggle [§8.3 item 5's "adding a region later
        // is an edit, not a redesign"].
        const int strip_h = strip_on ? kStripH : 0;
        const int main_h = H - main_y - strip_h - kMeterH - kStatusH;
        const int pane_w = W - kPanelW - 2 * kPad;

        pane->resize(kPad, main_y + kRulerH, pane_w, main_h - kRulerH - kPad);
        // The ruler is aligned to the pane's INTERIOR, not to the window or
        // to the pane's outer box, and it stops where the image area stops —
        // a vertical scrollbar takes width off both together. It is the
        // phase-entry affordance [docs/04, the ruler/coordinate pattern], so
        // a tick that does not name the image column beneath it is the one
        // thing it must never do.
        ruler->resize(kPad + kFrame, main_y, pane_interior_w(), kRulerH);
        layout_view();

        layout_sidebar(W, main_y, main_h);

        // Directly above the meter, full width, both of them below the
        // picture: the strip EXTENDS the meter strip rather than competing
        // with the sidebar [docs/05 §8.3 item 5].
        strip->resize(0, main_y + main_h, W, strip_h);
        if (strip_on) strip->show(); else strip->hide();
        meter->resize(0, main_y + main_h + strip_h, W, kMeterH);
        const int sy = main_y + main_h + strip_h + kMeterH;
        status_bg->resize(0, sy, W, kStatusH);
        status_state->resize(kPad, sy, 240, kStatusH);
        status_lines->resize(kPad + 250, sy, 120, kStatusH);
        progress->resize(W - kPad - 160, sy + 3, 160, kStatusH - 6);
    }

    // The sidebar, top to bottom in the panel's fixed width: the status
    // fields, the correction block and, in the lower area §8.3 item 5 kept
    // clear for it, the receiving indicator.
    void layout_sidebar(int W, int main_y, int main_h) {
        const int px = W - kPanelW;
        const int fw = kPanelW - 2 * kPad;
        panel->resize(px, main_y, kPanelW, main_h);
        int py = main_y + kPad;
        title->resize(px + kPad, py, fw, kPanelRowH);
        py += kPanelRowH;
        for (int i = 0; i < kFields; i++) {
            field_cap[i]->resize(px + kPad, py, 62, kPanelRowH);
            field_val[i]->resize(px + kPad + 66, py, fw - 66, kPanelRowH);
            py += kPanelRowH;
        }
        cap_label->resize(px + kPad, py, 62, kPanelRowH);
        label_input->resize(px + kPad + 66, py + 1, fw - 66, kPanelRowH - 2);
        py += kPanelRowH + kPad;
        rule->resize(px + kPad, py, fw, 2);
        py += 2 + kPad;
        // The correction block, relaid out in session 30 [Sara, after using
        // session 29's version rather than only looking at it]. The two
        // controls now read as two matching rows — caption, box, arming
        // button — with SYNC's steppers on a row of their own beneath.
        //
        // What this gives up, stated plainly because it was given up on
        // purpose. Session 29 FLANKED the SYNC box with the steppers, on the
        // reasoning that a control touching a box on both sides cannot be
        // read as belonging to some other box; that was the fix for exactly
        // this shape, whose earlier version Sara read as PHASE's steppers.
        // Flanking cost the SYNC caption its place on the box's row, which
        // left that row holding a caption, a dead gap and an arming button —
        // and it was the gap, not the steppers, that made the block look
        // heavy. So the tie between the steppers and SYNC is now ADJACENCY
        // (directly under SYNC's box, two rows clear of PHASE's) rather than
        // enclosure. Adjacency is the weaker claim and `gui_shell` now pins
        // the weaker claim honestly instead of restating the old one.
        //
        // PHASE still has no steppers, and that is still the deliberate
        // asymmetry rather than a hole in the layout [see sync_step]: a nudge
        // smaller than the ±54-column window PHASE is refined within moves
        // nothing at all. Its instrument is the click.
        // Same three rows as before, so nothing below moves.
        const int arm_w = 48;
        const int arm_x = px + kPanelW - kPad - arm_w;
        const int box_x = px + kPad + 66;
        const int box_w = 70;
        cap_phase->resize(px + kPad, py, 62, kPanelRowH);
        phase_input->resize(box_x, py + 1, box_w, kPanelRowH - 2);
        phase_arm->resize(arm_x, py + 1, arm_w, kPanelRowH - 2);
        py += kPanelRowH;
        cap_sync->resize(px + kPad, py, 62, kPanelRowH);
        sync_input->resize(box_x, py + 1, box_w, kPanelRowH - 2);
        sync_arm->resize(arm_x, py + 1, arm_w, kPanelRowH - 2);
        py += kPanelRowH;
        // Four equal cells across the panel's full width: -10 -1 +1 +10. The
        // coarse pair stays outside the fine pair so the row reads as a
        // scale, and the sign stays in the label because the direction has
        // to be readable without a caption on the row.
        {
            const int gap = 3;
            const int bw = (fw - (kSyncSteps - 1) * gap) / kSyncSteps;
            int bx = px + kPad;
            for (int i = 0; i < kSyncSteps; i++) {
                sync_step_btn[i]->resize(bx, py + 1, bw, kPanelRowH - 2);
                bx += bw + gap;
            }
        }
        py += kPanelRowH + kPad;
        apply->resize(px + kPad, py, 70, 21);
        autob->resize(px + kPad + 74, py, 70, 21);
        py += 24;
        correct_why->resize(px + kPad, py, fw, 28);
        py += 28 + kPad;
        // The sidebar's lower area, which §8.3 item 5 kept clear for this
        // and nothing else. Fixed height rather than "the rest of the
        // panel": the indicator is compact by specification, and a click
        // target that grows to fill a tall window would be a click target
        // mostly made of empty space.
        recv->resize(px + kPad, py, fw, 52);
    }

    // The scrolled child's size is the image's size on screen, which is what
    // makes the scrollbars appear exactly when the image exceeds the pane
    // [§8.3 item 3]. With no width known the child is the pane, so nothing
    // scrolls and no bar appears over an empty pane.
    void layout_view() {
        const nova::RulerView v = view_state();
        const int iw = v.image_cols > 0
                           ? static_cast<int>(std::lround(v.image_cols * v.scale))
                           : pane_interior_w();
        // Rows are drawn at the same scale as columns, so the chart is not
        // stretched. With no picture the child is the pane, so nothing
        // scrolls over an empty pane [§8.3 item 3].
        view->set_scale_y(v.scale);
        int ih = pane_interior_h();
        if (view->has_image()) {
            const int drawn = static_cast<int>(
                std::lround(view->image_rows() * v.scale));
            ih = std::max(drawn, pane_interior_h());
        }
        // **Fl_Scroll scrolls by MOVING its child, so the child's position
        // IS the scroll offset and `yposition()` is only a cached copy of
        // it.** Resizing the child to the pane's top-left therefore
        // silently scrolls the picture back to the top while Fl_Scroll goes
        // on reporting the old offset — and the repair below cannot repair
        // it, because `scroll_to` early-returns when its arguments equal
        // the cached values. Every later `scroll_to` then moves by a delta
        // measured from a number that is no longer true, which is the
        // bouncing Sara saw once the pane started following the newest row
        // [session 27; measured against FLTK 1.4.5]: with a batch of rows
        // arriving between redraws the offset lands at `max_y - previous`,
        // so the chart alternates between its bottom and its top.
        //
        // Resize the child AT the current offset instead. The invariant
        // Fl_Scroll re-derives on draw then holds continuously, and every
        // actual move goes through `scroll_to` — the only place that knows
        // it is moving something.
        view->resize(pane->x() + kFrame - pane->xposition(),
                     pane->y() + kFrame - pane->yposition(), iw, ih);
        // Both axes are clamped to what the new geometry can show. Y needs
        // it for the same reason X always did: a new transmission restarts
        // the picture at row 0, and an offset from the previous chart's
        // height would point past the end of this one.
        const double keep = std::min(static_cast<double>(pane->xposition()),
                                     nova::max_scroll_px(v));
        const int keep_y = std::min(pane->yposition(),
                                    std::max(0, ih - pane_interior_h()));
        pane->scroll_to(static_cast<int>(std::lround(keep)), keep_y);
    }

    // Where the child ACTUALLY sits. The only authority on the scroll
    // offset, for the reason above, and what the --follow inspection
    // reports so a screamer can tell a followed row from a cached lie.
    int scroll_y_actual() const { return pane->y() + kFrame - view->y(); }

    // While rows arrive the pane follows them [Sara, session 26]: the
    // bottom of the pane IS the newest line, so the operator never chases
    // the scrollbar to watch the chart come in. A manual scroll up is
    // corrected on the next row — the price of a promise that cannot be
    // misunderstood. Once the state leaves DRAWING the scroll is the
    // operator's again: nothing moves it after SAVED.
    void follow_newest_row() {
        const int max_y = std::max(0, view->h() - pane_interior_h());
        if (pane->yposition() != max_y)
            pane->scroll_to(pane->xposition(), max_y);
    }

    // A new picture reached the pane. One method rather than a block
    // inside `drain`, because the --follow inspection must take the SAME
    // path the operator does — an inspection that reimplements the rule
    // pins the reimplementation.
    void show_image(const nova::Image& img) {
        const bool resized = view->set_image(img);
        if (resized) {
            layout(win->w(), win->h());
            ruler->redraw();
        }
        if (state == LiveState::kDrawingPreview) follow_newest_row();
    }

    // Everything the state decides, in one place, so a state change cannot
    // update the button and forget the status line.
    void apply_state() {
        const Transport t = transport_for(state, ioc_explicit(), rate_explicit(),
                                          capture);
        // A re-render is a decode the session did not make a state for — it
        // stays in SAVED throughout [§8.5 items 2-4, the shape Sara chose
        // session 27] — so the transport has to be held still by the engine's
        // answer rather than by the state. The shell is serialized: one
        // decode at a time [§8.3 item 4], and starting a capture on top of a
        // running re-render is exactly the second one.
        const bool rerendering = engine && engine->redecoding();
        start->label(t.label);
        if (t.start_active && !rerendering) start->activate();
        else start->deactivate();
        if (t.force_active && !rerendering) force->activate();
        else force->deactivate();

        // The Device menu reopens the stream, which would kill a live
        // chart, so it is insensitive from Start until the transmission
        // ends — IDLE and SAVED are the two states with nothing to lose
        // [§8.3 item 9, decided session 25]. Same deactivate-never-prompt
        // idiom as Force Start.
        if (!devices.empty() &&
            (state == LiveState::kIdle || state == LiveState::kSaved) &&
            !rerendering)
            device->activate();
        else
            device->deactivate();

        status_state->label(state_text(state));
        field_val[3]->label(state_text(state));

        // (The surface question itself is `live_surface`, above.)
        // The two manual corrections go live exactly where they have a
        // picture to correct [docs/05 §7]: while the preview is being
        // drawn. PHASE reports where the dead sector IS, as a column;
        // SYNC trims the line rate in ppm. Both apply FORWARD from the
        // next row — drawn rows are never revised — which is the whole
        // contract of the provisional renderer.
        //
        // The two boxes serve TWO surfaces, and never both at once, because
        // a picture is either being drawn or has been decoded:
        //   live [§7]      — forward-only corrections to a preview; the
        //                    correction takes effect from the next row and
        //                    drawn rows are never revised;
        //   post-decode    — a re-render of a picture already on disk, from
        //   [§7.1, §8.5]     the raw stream retained behind it [§3].
        const nova::RetainedVideo retained =
            engine ? engine->retained_video() : nova::RetainedVideo{};
        const bool busy = rerendering;
        const bool overrides_live = live_surface();
        const bool can_rerender = retained.can_correct() && !busy;

        const bool no_surface = !retained.can_correct() && !overrides_live;

        end_edit_on_surface_loss(no_surface);
        hold_pane_for_edit(retained);
        update_recv_indicator();

        // One place decides all three, and it is a pure function so the
        // truth table is checkable without a window [see correction_for].
        const CorrectionUi cu = correction_for(
            overrides_live, can_rerender, edit_dirty,
            applied.phase_set || applied.sync_set);
        update_correction_widgets(cu);
        update_reason_line(busy, overrides_live, can_rerender, retained);
        update_progress_and_fields(busy);
        if (win) win->redraw();
    }

    // §8.5 item 4's other end: the edit ends when the pane stops showing
    // the chart it was correcting, and the controls go back to
    // measured-or-blank, because there is no memory between
    // transmissions [§8.5 item 6].
    void end_edit_on_surface_loss(bool no_surface) {
        if (no_surface &&
            (edit_dirty || applied.phase_set || applied.sync_set)) {
            edit_dirty = false;
            applied = nova::Correction{};
            phase_input->value("");
            sync_input->value("");
        }
        // The anchor, the arming and the measurement's note die with the
        // surface REGARDLESS of whether an edit was dirty — and that
        // separation is session 29's, found by hand on the built code.
        //
        // Session 28 cleared the anchor inside the block above, which was
        // correct then and became wrong here without anything touching it:
        // back then the first click of a slant also SET PHASE, so an anchor
        // could not exist without a dirty edit and the guard was free.
        // Declared gestures broke that coupling — the SYNC gesture's first
        // click changes no value the operator can see, deliberately, so it
        // does not dirty the edit — and the anchor started surviving into
        // states with no picture behind it. It was still unreachable,
        // because arming clears it and arming is the only way to a second
        // click; net-correct for an incidental reason is not correct, and
        // the reason was one refactor away from going.
        if (no_surface) {
            pending_col = -1;
            pending_row = -1;
            arm = Arm::kNone;
            slant_note.clear();
        }
    }

    // §8.2 / ROADMAP M4 item 6: the edit holds the pane. The predicate
    // lives on this thread because "an edit is in progress" is a fact
    // about typed boxes and clicks [§8.5 item 4] that only thread 4 can
    // see; the engine is told, and does the buffering.
    //
    // `retained.can_correct()` is part of the predicate rather than
    // decoration: a dirty edit with no correctable surface behind it is
    // not an edit anyone can finish, and holding the pane for it would
    // freeze the live view with nothing to show for it.
    // **Nothing promotes automatically** [Sara, session 30]. §8.2 said
    // the buffered picture comes forward when the operator "switches or
    // finishes", but §8.5 item 4 — written ten sessions later — made an
    // edit END at Apply, and the two compose into something neither
    // decided: the correction you just asked for is replaced by the
    // incoming transmission at the moment it finishes rendering, so you
    // never see it. Sara's call is that the pane changes hands ONLY at
    // the indicator, which is also the plainest reading of §8.2's own
    // purpose ("nothing interrupts a human mid-correction"). So this
    // sets the hold and does not promote; `cb_recv` is the only caller
    // of `promote_background` in the program.
    void hold_pane_for_edit(const nova::RetainedVideo& retained) {
        const bool hold_pane = edit_dirty && retained.can_correct();
        if (engine) engine->set_pane_held(hold_pane);
    }

    // The indicator reports the ENGINE's buffer, not the shell's
    // predicate. They differ on purpose and the difference is the whole
    // of Sara's decision: once a transmission is buffered the engine
    // goes on buffering after `hold_pane` drops, until the click below
    // promotes it. An indicator driven from `hold_pane` would go dark
    // at Apply while the buffer it names was still there.
    void update_recv_indicator() {
        if (recv) {
            const nova::LiveEngine::Background bg =
                engine ? engine->background() : nova::LiveEngine::Background{};
            nova::Image thumb;
            if (bg.active && engine) engine->copy_background_image(&thumb);
            recv->set(bg.active, state_text(state), bg.rows, bg.complete,
                      thumb);
        }
    }

    void update_correction_widgets(const CorrectionUi& cu) {
        if (cu.inputs_active) {
            phase_input->activate();
            sync_input->activate();
        } else {
            phase_input->deactivate();
            sync_input->deactivate();
        }
        // The steppers are the SYNC box in another shape, so they are
        // active exactly when it is — never a separate rule that could
        // drift from it and leave a live button over a dead box.
        for (int i = 0; i < kSyncSteps; i++) {
            if (cu.inputs_active)
                sync_step_btn[i]->activate();
            else
                sync_step_btn[i]->deactivate();
        }
        // The two arming buttons follow the boxes for exactly the reason
        // the steppers do: a live arm over a dead box would offer a gesture
        // that cannot deliver its result anywhere [§3].
        if (cu.inputs_active) {
            phase_arm->activate();
            sync_arm->activate();
        } else {
            phase_arm->deactivate();
            sync_arm->deactivate();
            // No `arm = kNone` here, deliberately: `no_surface` above is
            // exactly `!cu.inputs_active`, so this branch would be a SECOND
            // place holding the same rule, and two places holding one rule
            // is how they come to disagree. The clearing happens once,
            // where the anchor's does.
        }
        // Session 29: the image listens only while a gesture is ARMED, not
        // wherever a correction is possible. That is the whole of Sara's
        // accidental-click objection — an unarmed click cannot move her
        // data because it never reaches the handler's effect [see Arm] —
        // and it is what turns the crosshair from ambient decoration into a
        // statement that THIS click will act.
        view->set_click_enabled(arm != Arm::kNone);
        // The buttons show the arming too, and they are set from `arm`
        // rather than trusted to hold it: `set_arm` is not the only way it
        // changes — completing a gesture and losing the surface both clear
        // it, and neither goes near a widget.
        phase_arm->value(arm == Arm::kPhase ? 1 : 0);
        sync_arm->value(arm == Arm::kSync ? 1 : 0);
        if (cu.apply_active) apply->activate(); else apply->deactivate();
        if (cu.auto_active) autob->activate(); else autob->deactivate();
    }

    void update_reason_line(bool busy, bool overrides_live, bool can_rerender,
                            const nova::RetainedVideo& retained) {
        // §3: a correction that cannot be made says why. So does one that
        // can — what Apply will DO is the thing an operator most needs
        // told, because it overwrites a file with no prompt [§8.5 items
        // 2-3].
        //
        // A half-made slant measurement outranks all of them while it is
        // pending, because it is the ONE thing on this surface an operator
        // cannot discover by looking: the two-click gesture has no widget
        // of its own [see click_image]. §3's sentence is about controls
        // that cannot act; this is the same duty for a control with
        // nothing to point at. It names the baseline the CURRENT zoom
        // needs, which is the number that decides whether the next click
        // measures a slant or re-picks PHASE.
        const int need_rows =
            min_baseline_rows(view_state().scale, image_cols());
        // No `engine` term on the armed branches: an arming can only exist
        // where the boxes are live, which already implies a surface. Adding
        // one would be redundant in production and would suppress the
        // prompt on the inspection path, where it is the only way to check
        // it.
        //
        // Session 29 order-of-precedence, and it is a ranking of how
        // PERISHABLE each sentence is. An armed gesture is waiting on the
        // operator's very next click, so it outranks everything; the note
        // on a just-finished measurement outranks the standing description
        // of Apply, because it describes something that happened once and
        // will be gone as soon as anything else does; the rest is the
        // situation, which is still true a minute from now.
        if (arm == Arm::kPhase && !busy) {
            correct_reason = "click the dead sector on the picture";
        } else if (arm == Arm::kSync && pending_row < 0 && !busy) {
            correct_reason = "click one end of a slanted feature";
        } else if (arm == Arm::kSync && !busy) {
            // The baseline is no longer a gate [see min_baseline_rows], so
            // this names it as ADVICE rather than as a requirement: any
            // second click measures, and this is what it takes for the
            // answer to be worth +/-5 ppm at the current zoom.
            char why[128];
            std::snprintf(why, sizeof why,
                          "now click the same feature again; %d+ rows apart "
                          "for +/-5 ppm",
                          need_rows);
            correct_reason = why;
        } else if (!slant_note.empty() && !busy) {
            correct_reason = slant_note;
        } else if (!engine)
            correct_reason = "no capture running";
        else if (busy)
            correct_reason = "re-rendering the saved image";
        else if (overrides_live)
            correct_reason = "applies from the next row drawn";
        else if (can_rerender)
            correct_reason = "Apply re-renders and overwrites the saved file";
        else
            correct_reason = retained.unavailable_reason;
        correct_why->copy_label(correct_reason.c_str());
    }

    void update_progress_and_fields(bool busy) {
        // The progress bar is populated ONLY during a decode, from the nine
        // decode stages [docs/05 §8, §4] — and a re-render is a decode. The
        // session stays in SAVED throughout one (it is the operator's
        // decode, not the machine's), so the state alone cannot answer this.
        if (state == LiveState::kDecoding || busy) progress->activate();
        else progress->deactivate();

        // The ruler is blank and disabled until the image width is known,
        // and lights up with no transition when it is [§8.3 item 1, §8.4
        // item 5].
        if (image_cols() > 0) ruler->activate(); else ruler->deactivate();
        field_val[1]->label(image_cols() > 0 ? ioc->text() : "--");
        field_val[2]->label(rate_explicit() ? rate->text() : "--");
        field_val[0]->label(ioc_explicit() && rate_explicit() ? "FORCED"
                                                             : "AUTO");
    }

    // --- the live half: bring it up, drive it, take it down -----------------

    // Called once, after the window is shown. A machine with no input
    // device gets no engine and keeps the honest grey transport of the
    // pre-wiring shell.
    void start_live() {
        if (devices.empty()) return;
        const InputDevice& d = devices[static_cast<std::size_t>(
            std::max(0, device->value()))];
        audio_rate = d.preferred_rate ? d.preferred_rate : 48000;

        nova::EngineOptions opt;
        opt.image_folder = image_folder;
        // The same band description the markers were drawn from — one
        // object, so the analyser and the widget cannot be configured
        // differently. live/spectrum.hpp never learns what a widget is;
        // this is the one place that knows the strip's pixel height.
        opt.spectrum = strip_opt;
        engine.reset(new nova::LiveEngine(static_cast<int>(audio_rate), opt));
        engine->run();

        RtAudio::StreamParameters p;
        p.deviceId = d.id;
        // One channel. §2 says "deinterleave, pick channel"; asking
        // RtAudio for one input channel is that, done by the library, and
        // it keeps the realtime callback down to a single memcpy-shaped
        // loop.
        p.nChannels = 1;
        p.firstChannel = 0;
        unsigned int frames = 1024;
        audio.reset(new RtAudio());
        audio->showWarnings(false);
        const RtAudioErrorType err =
            audio->openStream(nullptr, &p, RTAUDIO_FLOAT32, audio_rate,
                              &frames, &Shell::audio_cb, this);
        if (err != RTAUDIO_NO_ERROR) {
            audio_error = audio->getErrorText();
            audio.reset();
            engine.reset();
            return;
        }
        if (audio->startStream() != RTAUDIO_NO_ERROR) {
            audio_error = audio->getErrorText();
            audio->closeStream();
            audio.reset();
            engine.reset();
            return;
        }
        audio_open = true;
        open_device = device->value();
        capture = true;
        Fl::add_timeout(kTickSec, cb_tick, this);
        apply_state();
    }

    void stop_live() {
        Fl::remove_timeout(cb_tick, this);
        if (audio && audio_open) {
            audio->stopStream();
            audio->closeStream();
            audio_open = false;
        }
        audio.reset();
        open_device = -1;
        // After this there is nothing behind the buttons — not "paused",
        // gone — and the transport should say so.
        capture = false;
        // The engine's shutdown flushes the session, so a transmission in
        // progress when the window closes is still decoded and saved
        // rather than dropped [§8.3 item 6].
        engine.reset();
    }

    // --- the offline capture [session 31, ROADMAP M4 item 6] ----------------
    // The real engine, fed from a file instead of a sound card.
    //
    // **This is not a second live path, and the distinction is the whole
    // reason it is allowed to exist.** The engine is the one `start_live`
    // builds, from the same `EngineOptions`; the samples go in through the
    // same `push_audio` the realtime callback calls; the messages come out
    // through the same `drain()` the tick calls, into the same `apply_state`.
    // RtAudio and the FLTK timer are the only things missing and neither of
    // them is a RULE — one is a source of samples and the other is a clock.
    //
    // Why it had to be built: §8.2's receiving indicator needs a
    // transmission arriving BEHIND an edit, which needs a receiver and two
    // transmissions, so `gui_shell` could not reach the state at all. Every
    // cheaper seam considered would have made the indicator's rules pass
    // vacuously — with no engine, `cb_recv` returns at its first line and
    // `recv->active()` is false forever, so "a click with nothing buffered
    // promotes nothing" would be true of a program that promotes on every
    // click. A check that cannot reach the state is not checking it
    // [session 30's standing lesson].
    //
    // The cursor wraps, so one fixture makes two transmissions: a second
    // pass begins with the same start tone, and a start tone is what ENDS
    // the transmission before it. Feeding a FRACTION is what makes the
    // buffered picture partial while the pane's is complete — the two
    // numbers the indicator's line count has to be told apart by.
    bool feed_wav(const std::string& path, int pct) {
        auto it = feeds.find(path);
        if (it == feeds.end()) {
            nova::Wav w = nova::read_wav(path);
            if (w.samples.empty()) return false;
            it = feeds.emplace(path, Feed{std::move(w.samples), w.sample_rate,
                                          0})
                     .first;
        }
        Feed& f = it->second;
        if (!engine) {
            nova::EngineOptions opt;
            opt.image_folder = image_folder;
            // The engine's own poll interval, not the GUI's: this loop
            // drains as fast as it can push, and the default would make a
            // capture wait on a clock nothing here is running.
            opt.poll_ms = 1;
            audio_rate = static_cast<unsigned int>(f.rate);
            engine.reset(new nova::LiveEngine(f.rate, opt));
            engine->run();
            engine->start_capture();
            capture = true;
        } else if (static_cast<unsigned int>(f.rate) != audio_rate) {
            // One engine is built for one capture rate. Two fixtures at
            // two rates through one engine would resample nothing and
            // report a timebase error as if the band had caused it.
            return false;
        }
        if (f.at >= f.samples.size()) f.at = 0;
        const std::size_t want =
            static_cast<std::size_t>(static_cast<double>(f.samples.size()) *
                                     std::max(0, std::min(100, pct)) / 100.0);
        const std::size_t end = std::min(f.samples.size(), f.at + want);
        while (f.at < end) {
            const std::size_t k = std::min<std::size_t>(4096, end - f.at);
            // push_audio returns what the ring ACCEPTED. A realtime
            // callback has nothing to do with the shortfall; a fixture
            // does, and pretending otherwise would drop samples and call
            // the resulting timebase error a finding.
            f.at += engine->push_audio(f.samples.data() + f.at, k);
            drain();
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        settle(25);
        return true;
    }

    // Pump until thread 2 stops making progress. `consumed_sec` is the only
    // honest quiescence signal available here: kStats keeps arriving while
    // the engine lives, so "no messages" never happens and would settle
    // instantly on a capture that had not started.
    void settle(int stable_for) {
        double last = -1.0;
        int stable = 0;
        for (int i = 0; i < 4000 && stable < stable_for; i++) {
            drain();
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
            if (consumed_sec == last) {
                stable++;
            } else {
                stable = 0;
                last = consumed_sec;
            }
        }
    }

    // Wait for a re-render to finish. `settle` CANNOT do this and the
    // difference is not academic: settle waits on `consumed_sec`, which is
    // audio, and a re-render consumes no audio — so on a shell with nothing
    // being fed it returns at once, before thread 3 has started, and every
    // question asked afterwards is asked too early. That is how the first
    // control run of session 31 reported "no save" for a re-render that had
    // simply not happened yet.
    //
    // The grace period is why this cannot just poll `redecoding()`:
    // `redecode` is QUEUED to thread 2, so the flag is still false for the
    // first few ticks and a naive wait would fall straight through.
    void wait_rerender() {
        if (!engine) return;
        for (int i = 0; i < 8000; i++) {
            drain();
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
            if (i > 25 && !engine->redecoding()) break;
        }
        settle(15);
    }

    // Stop, then wait for the transmission to come down the freeze-decode-
    // save path. Bounded: a capture that never reaches SAVED is a failure to
    // report, not a reason to hang the inspection [live_ring, session 23].
    void stop_capture_and_wait() {
        if (!engine) return;
        cb_start(nullptr, this);
        const int before = saves_seen;
        for (int i = 0; i < 8000 && saves_seen == before; i++) {
            drain();
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }
        settle(15);
    }

    // Thread 4's one drain point [§2.3]: everything the worker threads
    // have to say arrives here, on a timer, and the window repaints at
    // most once per tick.
    static void cb_tick(void* p) {
        static_cast<Shell*>(p)->drain();
        Fl::repeat_timeout(kTickSec, cb_tick, p);
    }

    void drain() {
        if (!engine) return;
        bool state_changed = false;
        bool rows_changed = false;
        for (const nova::EngineMessage& m : engine->drain()) {
            switch (m.kind) {
                case nova::EngineMsg::kStateChanged:
                    state = m.state;
                    state_changed = true;
                    if (m.ioc > 0)
                        measured_cols =
                            static_cast<int>(std::lround(m.ioc * kPi));
                    break;
                case nova::EngineMsg::kRowsDrawn:
                    rows_drawn = m.rows_total;
                    rows_changed = true;
                    break;
                case nova::EngineMsg::kStats:
                    level_dbfs = m.level_dbfs;
                    overruns = m.overruns;
                    consumed_sec = m.consumed_sec;
                    meter->set_level(level_dbfs, true);
                    break;
                case nova::EngineMsg::kBatchProgress:
                    progress->value(static_cast<float>(m.fraction));
                    progress->redraw();
                    break;
                case nova::EngineMsg::kBatchDone:
                    if (m.result) {
                        measured_cols = m.result->img.width;
                        // The clock and timebase readouts stay blank until
                        // the batch decode produces them [§4] — this is
                        // where they stop being blank.
                        set_quality(*m.result);
                        rows_changed = true;
                    }
                    break;
                case nova::EngineMsg::kBatchFailed:
                    last_error = "decode failed";
                    break;
                case nova::EngineMsg::kSaved:
                    last_saved = m.path;
                    saves_seen++;
                    break;
                case nova::EngineMsg::kSaveFailed:
                    last_error = "not saved: " + m.detail;
                    break;
            }
        }
        if (rows_changed) {
            nova::Image img;
            if (engine->copy_image(&img)) show_image(img);
        }
        // A re-render starting or finishing changes no state and draws no
        // rows — the session stays in SAVED throughout — so it is its own
        // reason to re-apply the transport and the progress bar.
        const bool rr = engine && engine->redecoding();
        if (state_changed || rows_changed || rr != was_rerendering) {
            was_rerendering = rr;
            apply_state();
            update_status();
        }
        refresh_strip();
    }

    // M4.5's tuning strip, refreshed on the same 50 ms tick as everything
    // else [docs/05 §2.3] — the analyser produces one column per 50 ms, so
    // the strip advances exactly one row per repaint.
    //
    // Skipped entirely while hidden. That is not only an economy: the
    // waterfall the operator gets back on re-showing should be the CURRENT
    // few seconds, and there is nothing to be gained by keeping a buffer
    // warm that nobody is looking at.
    void refresh_strip() {
        if (!strip_on || !engine) return;
        int cols = 0;
        std::vector<float> rows;
        const int n = engine->copy_spectrum(&rows, &cols);
        strip->set_data(std::move(rows), cols, n);
    }

    void set_quality(const nova::DecodeResult& r) {
        // The seam count is said out loud [ROADMAP M4 item 8(c), session
        // 26]: seams are skips in the delivered AUDIO, not decode quality,
        // and a stepping capture chain (the KiwiSDR browser hop, twice
        // caught) is exactly what the operator can still act on — switch
        // SDRs — while the broadcast is on. Zero is information too: it
        // is what cleared Nova's own capture on the clean JMH pair.
        std::snprintf(quality_buf, sizeof quality_buf,
                      "%d/%d, %+.0f ppm, %d seams", r.locked_lines, r.lines,
                      r.clock_ppm, r.seams);
        field_val[4]->label(quality_buf);
        // ...and this is the clock a blank SYNC box means. A re-render
        // posts kBatchDone like any other decode, so after an Apply this
        // tracks the CORRECTED picture and the next nudge is relative to
        // what is on the pane rather than to the original measurement.
        shown_ppm = r.clock_ppm;
        shown_ppm_valid = true;
    }

    void update_status() {
        if (rows_drawn > 0)
            std::snprintf(lines_buf, sizeof lines_buf, "line %d", rows_drawn);
        else
            std::snprintf(lines_buf, sizeof lines_buf, "line --");
        status_lines->label(lines_buf);
        // The overrun count is shown, never hidden [§2.1]: a picture with
        // our own dropped samples in it must say so.
        if (!last_error.empty())
            std::snprintf(status_buf, sizeof status_buf, "%s \xe2\x80\x94 %s",
                          state_text(state), last_error.c_str());
        else if (overruns > 0)
            std::snprintf(status_buf, sizeof status_buf,
                          "%s \xe2\x80\x94 %llu samples dropped",
                          state_text(state), overruns);
        else if (!last_saved.empty() && state == LiveState::kSaved)
            std::snprintf(status_buf, sizeof status_buf, "%s \xe2\x80\x94 %s",
                          state_text(state), basename_of(last_saved).c_str());
        else
            std::snprintf(status_buf, sizeof status_buf, "%s",
                          state_text(state));
        status_state->label(status_buf);
        win->redraw();
    }

    static std::string basename_of(const std::string& p) {
        const size_t slash = p.find_last_of('/');
        return slash == std::string::npos ? p : p.substr(slash + 1);
    }

    void populate_devices() {
        devices = list_input_devices(&device_error);
        if (devices.empty()) {
            device->add(escape_menu_label("(no input device)").c_str());
            device->value(0);
            device->deactivate();
            return;
        }
        // The remembered choice wins over the system default [§8.3 item 9].
        // Matched by NAME, never by the enumerated id: CoreAudio ids are
        // per-boot, and a persisted id would be a dice roll that opens
        // somebody's microphone. A remembered device that is not plugged
        // in falls back to the default, not to an error.
        const std::string want = prefs.get("device");
        int def = 0;
        for (size_t i = 0; i < devices.size(); i++) {
            device->add(escape_menu_label(devices[i].name).c_str());
            if (devices[i].is_default) def = static_cast<int>(i);
            if (devices[i].name == want) def = static_cast<int>(i);
        }
        device->value(def);
    }

    // --- callbacks ----------------------------------------------------------
    static void cb_quit(Fl_Widget*, void* p) {
        Shell* s = static_cast<Shell*>(p);
        s->stop_live();
        s->win->hide();
    }

    // Settings sets the folder; the file type is not a choice [§8.3 item 7 —
    // greyscale PNG only, decided session 16]. The chosen folder persists in
    // the preference file beside the program [§8.4 item 1].
    static void cb_folder(Fl_Widget*, void* p) {
        Shell* s = static_cast<Shell*>(p);
        Fl_Native_File_Chooser chooser;
        chooser.title("Nova \xe2\x80\x94 image folder");
        chooser.type(Fl_Native_File_Chooser::BROWSE_DIRECTORY);
        if (!s->image_folder.empty())
            chooser.directory(s->image_folder.c_str());
        if (chooser.show() != 0) return;  // cancelled or failed: change nothing
        const char* picked = chooser.filename();
        if (!picked || !*picked) return;
        s->image_folder = picked;
        s->prefs.set("image_folder", s->image_folder);
        // The folder is where the NEXT completed decode writes [§8.5
        // item 1]; files already written keep the name they were written
        // with, and Nova never renames one.
        if (s->engine) s->engine->set_image_folder(s->image_folder);
    }

    static void cb_about(Fl_Widget*, void* p) {
        Shell* s = static_cast<Shell*>(p);
        if (!s->about) {
            // Built outside any open group: the main window is ended by now,
            // and a stray current group would adopt this one.
            Fl_Group* const prev = Fl_Group::current();
            Fl_Group::current(nullptr);
            s->about = new Fl_Window(560, 430, "About Nova");
            Fl_Box* text = new Fl_Box(kPad * 3, kPad * 3, 560 - kPad * 6,
                                      430 - kPad * 6 - 30, kAboutText);
            text->labelsize(kFontSize);
            text->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE |
                        FL_ALIGN_WRAP);
            Fl_Button* close = new Fl_Button(560 - kPad * 3 - 70,
                                             430 - kPad * 3 - 24, 70, 24,
                                             "Close");
            close->labelsize(kFontSize);
            close->callback(cb_about_close, s);
            s->about->end();
            s->about->set_non_modal();
            Fl_Group::current(prev);
        }
        s->about->show();
    }

    static void cb_about_close(Fl_Widget*, void* p) {
        static_cast<Shell*>(p)->about->hide();
    }

    // --- the transport [§8.3 item 4, §8.4 items 3-4] ------------------------
    // One button. Its meaning is the state's, so it asks `transport_for`
    // what it currently is rather than keeping a flag of its own.
    // The Device menu is the one control whose choice moves a sound card
    // [§8.3 item 9]. Changing it reopens the stream on the new device —
    // until session 25 it had no callback at all, so the menu relabelled
    // and the stream stayed on whatever was default at window-show (found
    // by the M4 item-1 run: the meter answered the operator's voice with
    // BlackHole selected). It is insensitive from Start until the
    // transmission ends, so this can only fire while monitoring or after a
    // save: there is never a live chart for the reopen to kill.
    // Keep the tick in the menu and the region in the window saying the
    // same thing. Called at construction and after every toggle, so the two
    // cannot drift — a checkbox that disagrees with the window is worse
    // than no checkbox.
    void sync_strip_menu() {
        Fl_Menu_Item* it =
            const_cast<Fl_Menu_Item*>(menu->find_item("View/Tuning strip"));
        if (!it) return;
        if (strip_on) it->set(); else it->clear();
    }

    static void cb_strip(Fl_Widget*, void* p) {
        Shell* s = static_cast<Shell*>(p);
        s->strip_on = !s->strip_on;
        s->prefs.set("tuning_strip", s->strip_on ? "1" : "0");
        s->sync_strip_menu();
        // The window does not change size: the strip takes its height from
        // the picture area and gives it back. Toggling it must not move the
        // operator's window on their desktop.
        s->layout(s->win->w(), s->win->h());
        s->win->redraw();
    }

    static void cb_device(Fl_Widget*, void* p) {
        Shell* s = static_cast<Shell*>(p);
        const int v = s->device->value();
        if (v < 0 || v >= static_cast<int>(s->devices.size())) return;
        if (v == s->open_device && s->audio_open) return;  // re-picked live one
        s->prefs.set("device", s->devices[static_cast<size_t>(v)].name);
        s->stop_live();
        // The new engine posts no initial state, so without this the shell
        // would sit in SAVED — and keep the menu grey — until a tone moved
        // it. The restart IS a new session: idle, listening, nothing drawn.
        s->state = LiveState::kIdle;
        s->start_live();
        s->apply_state();  // start_live's failure paths return before theirs
    }

    static void cb_start(Fl_Widget*, void* p) {
        Shell* s = static_cast<Shell*>(p);
        if (!s->engine) return;
        const Transport t = transport_for(s->state, s->ioc_explicit(),
                                          s->rate_explicit(), s->capture);
        s->last_error.clear();
        if (!std::strcmp(t.label, "Stop")) {
            // Stop HOLDS the image; it never discards it [docs/04
            // Finding 6, the SR-97 precedent]. The session takes it down
            // the same freeze-decode-save path a stop tone takes.
            s->engine->stop_capture();
        } else {
            s->engine->set_label(s->label_input->value()
                                     ? s->label_input->value()
                                     : "");
            s->engine->start_capture();
        }
    }

    // Forced start [docs/04 Finding 2]: on every one of the sixteen
    // receivers surveyed, without exception. Gated on both dropdowns
    // being explicit, which `transport_for` decides and this only reads.
    static void cb_force(Fl_Widget*, void* p) {
        Shell* s = static_cast<Shell*>(p);
        if (!s->engine || !s->ioc_explicit() || !s->rate_explicit()) return;
        s->last_error.clear();
        s->engine->set_label(s->label_input->value() ? s->label_input->value()
                                                     : "");
        s->engine->start_capture();
        s->engine->force_start(s->ioc_value(), s->rate_value());
    }

    // PHASE is typed as an image COLUMN, because that is what the ruler
    // above the pane names [§8.3 item 1] and what the operator can read off
    // the picture. Both surfaces want a fraction of the line.
    bool typed_phase(double* frac) const {
        const int cols = image_cols();
        const char* pv = phase_input->value();
        if (!pv || !*pv || cols <= 0) return false;
        *frac = std::min(0.999, std::max(0.0, std::atof(pv) / cols));
        return true;
    }
    bool typed_sync(double* ppm) const {
        const char* sv = sync_input->value();
        if (!sv || !*sv) return false;
        *ppm = std::atof(sv);
        return true;
    }

    // Apply serves both surfaces [§7 live, §8.5 post-decode], because they
    // are the same two boxes and the operator does the same thing with
    // them. Which one is live is decided by what is on the pane, not by a
    // mode the operator has to hold in their head.
    static void cb_apply(Fl_Widget*, void* p) {
        Shell* s = static_cast<Shell*>(p);
        if (!s->engine) return;
        double frac = 0.0, ppm = 0.0;
        const bool has_phase = s->typed_phase(&frac);
        const bool has_sync = s->typed_sync(&ppm);

        // The same question `apply_state` asked to write the reason line, so
        // the button cannot do one thing while the line above it says
        // another [see live_surface].
        if (s->live_surface()) {
            // The live surface: "touch once and wait several lines before
            // judging". The row where each takes effect is marked, so the
            // operator can see where their correction began.
            if (has_phase) s->engine->set_phase(frac);
            if (has_sync) s->engine->set_sync(ppm);
            return;
        }
        // The post-decode surface: re-render the saved image from its
        // retained raw stream and OVERWRITE the file [§8.5 items 2-3].
        // There is no Save button and no prompt, which is why the reason
        // line under these buttons says what Apply does before it is
        // pressed.
        nova::Correction c;
        c.phase_set = has_phase;
        c.phase_frac = frac;
        c.sync_set = has_sync;
        c.sync_ppm = ppm;
        s->engine->redecode(c);
        s->applied = c;
        s->edit_dirty = false;   // the edit ends at Apply [§8.5 item 4]
        s->pending_col = -1;     // ...and so does a half-made measurement
        s->pending_row = -1;
        s->arm = Arm::kNone;     // ...and a gesture armed for the old values
        s->slant_note.clear();
        s->apply_state();
    }

    // Auto restores the measured values and re-renders [§8.5 item 4]. It is
    // not a third mode: "as measured" is the ABSENCE of the two values, so
    // Auto is the empty correction, and the boxes go back to blank because
    // blank is how this shell writes "measured" [§8.5 item 6].
    // §8.2's one and only way for the pane to change hands [Sara, session
    // 30]. Promotion is queued to thread 2 — the picture does not travel
    // alone, and `saved_path_` is thread 2's — so the pane changes on a
    // later tick, not inside this call.
    //
    // The edit ends here, and it ends because the operator LEFT it, which
    // is §8.5 item 4's third ending ("or when the operator switches to the
    // live view") finally being the operator's own action rather than the
    // stand-in session 27 had to write. The values go back to blank because
    // there is no memory between transmissions [§8.5 item 6], and the
    // arming and pending anchor go with them: they name a picture that is
    // about to stop being on screen.
    static void cb_recv(Fl_Widget*, void* p) {
        Shell* s = static_cast<Shell*>(p);
        if (!s->engine || !s->recv->active()) return;
        s->engine->promote_background();
        s->edit_dirty = false;
        s->applied = nova::Correction{};
        s->phase_input->value("");
        s->sync_input->value("");
        s->pending_col = -1;
        s->pending_row = -1;
        s->arm = Arm::kNone;
        s->slant_note.clear();
        s->apply_state();
        s->update_status();
    }

    static void cb_auto(Fl_Widget*, void* p) {
        Shell* s = static_cast<Shell*>(p);
        if (!s->engine) return;
        s->phase_input->value("");
        s->sync_input->value("");
        s->engine->redecode(nova::Correction{});
        s->applied = nova::Correction{};
        s->edit_dirty = false;   // ...and at Auto [§8.5 item 4]
        s->pending_col = -1;
        s->pending_row = -1;
        s->arm = Arm::kNone;
        s->slant_note.clear();
        s->apply_state();
    }

    // §8.5 item 4: the edit BEGINS at the first change to PHASE or SYNC.
    // FLTK reports a changed input only if asked to, so the two boxes are
    // set to FL_WHEN_CHANGED — without it the boundary would be "when the
    // operator pressed Enter", which is not what the sentence says.
    static void cb_edit(Fl_Widget*, void* p) {
        Shell* s = static_cast<Shell*>(p);
        if (s->edit_dirty) return;
        s->edit_dirty = true;
        s->apply_state();
    }

    // A nudge is an edit, and it has to say so ITSELF: FLTK does not fire
    // an input's callback for a programmatic `value()`, so a stepper that
    // only wrote the box would move the number and leave Apply grey — the
    // operator's change sitting in a control that claims nothing changed.
    void nudge_sync(double delta) {
        const SyncStep st =
            sync_step(sync_input->value(), shown_ppm, shown_ppm_valid, delta);
        sync_input->value(sync_text(st.value).c_str());
        if (!edit_dirty) edit_dirty = true;
        apply_state();
    }

    // --- arming [see Arm] --------------------------------------------------
    // One entry point for both buttons, so the two can never drift into
    // different lifecycles. Pressing the armed gesture again disarms;
    // pressing the other one switches, because "switch directly" is what an
    // operator who pressed the wrong button will try and there is no reason
    // to make them press twice.
    //
    // Any half-made measurement dies here. Re-arming SYNC mid-gesture is
    // the operator saying "start again", and an anchor that survived it
    // would silently pair their fresh first click with a stale one — the
    // same across-two-pictures failure the edit-end rule exists to stop,
    // one gesture earlier and just as plausible-looking.
    void set_arm(Arm want) {
        arm = (arm == want) ? Arm::kNone : want;
        pending_col = -1;
        pending_row = -1;
        // The measurement's quality note belongs to the measurement. Once
        // the operator arms anything, the number in the box is about to be
        // replaced and a note describing the OLD one is a false label.
        slant_note.clear();
        apply_state();
    }

    static void cb_arm_phase(Fl_Widget*, void* p) {
        static_cast<Shell*>(p)->set_arm(Arm::kPhase);
    }
    static void cb_arm_sync(Fl_Widget*, void* p) {
        static_cast<Shell*>(p)->set_arm(Arm::kSync);
    }

    static void cb_sync_m10(Fl_Widget*, void* p) {
        static_cast<Shell*>(p)->nudge_sync(-10.0);
    }
    static void cb_sync_m1(Fl_Widget*, void* p) {
        static_cast<Shell*>(p)->nudge_sync(-1.0);
    }
    static void cb_sync_p1(Fl_Widget*, void* p) {
        static_cast<Shell*>(p)->nudge_sync(1.0);
    }
    static void cb_sync_p10(Fl_Widget*, void* p) {
        static_cast<Shell*>(p)->nudge_sync(10.0);
    }

    // A click on the image belongs to whichever gesture is ARMED [see Arm],
    // and to nothing at all if none is. It SETS, it does not apply: §8.5
    // item 4 already names "the first click on the image" as a thing that
    // BEGINS an edit, and an edit ends at Apply. A click that re-rendered
    // immediately would also make the operator's aim un-correctable — the
    // natural motion is click, look, click again, then Apply.
    //
    // TWO declared gestures, replacing session 28's one undeclared one:
    //   PHASE — one click, naming the column the dead sector is at. Done.
    //   SYNC  — two clicks on the same feature at two ROWS; the slant
    //           between them is the residual clock error. Done.
    //
    // **The SYNC gesture no longer touches PHASE, and that is the change
    // arming pays for.** Session 28 had to set PHASE from the first click
    // because it could not know yet that a second one was coming — which
    // then forced the "PHASE takes the UPPER click" correction, since on a
    // slanted chart the lower column is wrong by exactly the slant being
    // measured. A declared gesture does one thing, so both the coupling and
    // its correction are simply gone. The operator who wants both arms
    // PHASE, clicks, then arms SYNC and clicks twice: three presses instead
    // of two, and no rule to learn about which click won.
    //
    // Returns what it DID, because that is what makes the guard below
    // observable: `apply_state`'s edit-end rule [§8.5 item 4] clears the
    // boxes whenever a correction is not possible, so a click that wrongly
    // acted on a dead surface would be wiped a moment later and the shell
    // would look correct afterwards. Net-correct for an incidental reason
    // is not the same as correct. kAnchor is the SYNC gesture's first
    // click: it changes no value the operator can see, so without a name of
    // its own it would be indistinguishable from a click that did nothing.
    enum class ClickAction { kNone, kPhase, kAnchor, kSync };
    struct ClickResult {
        ClickAction action = ClickAction::kNone;
        int column = -1;
        double ppm = 0.0;
    };

    ClickResult click_image(double x_in_pane, double y_in_view) {
        ClickResult r;
        // The guard lives HERE, at the point of effect, and not only in
        // `ImageView::handle`. The two are asking different questions —
        // handle decides whether to CONSUME the event (it does not, so the
        // pane below may still want it), this decides whether the click
        // may ACT — and only the second one is a rule about the operator's
        // data. Putting it only in the widget would also leave it on the
        // far side of the FLTK seam, where no screamer can reach it.
        //
        // Session 29: `arm` is read rather than `click_enabled()`, and the
        // difference matters even though `apply_state` keeps them in step.
        // The widget flag is the AFFORDANCE (crosshair, and whether to
        // consume); this is the RULE. A click that acted because the cursor
        // happened to be a crosshair would be the same class of defect as
        // the one session 28 found by putting the guard only in the widget.
        if (arm == Arm::kNone) return r;
        const nova::RulerView v = view_state();
        const int col = clicked_column(v, x_in_pane);
        if (col < 0) return r;   // named nothing; say nothing
        const int row = v.scale > 0.0
                            ? static_cast<int>(y_in_view / v.scale)
                            : -1;

        if (arm == Arm::kPhase) {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%d", col);
            phase_input->value(buf);
            // The gesture is complete, so it disarms itself — hamfax's
            // lifecycle, and the reason arming is not a mode you can be
            // stuck in. The next stray click on the picture does nothing.
            arm = Arm::kNone;
            // The same declaration `nudge_sync` has to make, for the same
            // reason: FLTK did not fire the input's callback, we did.
            if (!edit_dirty) edit_dirty = true;
            apply_state();
            r.action = ClickAction::kPhase;
            r.column = col;
            return r;
        }

        // The SYNC gesture. First click is the anchor and nothing else.
        if (pending_row < 0 || row < 0) {
            pending_col = col;
            pending_row = row;
            apply_state();   // the reason line now asks for the second point
            r.action = ClickAction::kAnchor;
            r.column = col;
            return r;
        }
        return measure_sync_click(col, row, v);
    }

    // Second click. The one measurement that cannot be made is two
    // points on the SAME row: there is no baseline at all, and the
    // slant is 0/0. hamfax divides by zero here. Nova keeps the anchor
    // and stays armed, because the operator's gesture is not finished —
    // they aimed badly, and asking again is the whole of the fix.
    ClickResult measure_sync_click(int col, int row,
                                   const nova::RulerView& v) {
        ClickResult r;
        const int drow = row - pending_row;
        if (drow == 0) {
            apply_state();
            return r;   // kNone: nothing named, nothing changed, still armed
        }

        // The measurement is a residual on the picture AS DRAWN, so it
        // is added to the clock that drew it — the same rule the
        // steppers' blank box obeys, and the same error if it is
        // skipped.
        const double residual =
            slant_ppm(col - pending_col, drow, v.image_cols);
        // The SAME operation a stepper nudge performs — a delta on the
        // clock the picture was drawn on — so it is the same function,
        // and `--sync-step`'s table screams for both. The empty
        // `typed` is deliberate and is the one difference worth
        // stating: a nudge starts from whatever the operator has
        // typed, but a SLANT IS READ OFF THE PICTURE, and the picture
        // was drawn on the shown clock no matter what is sitting in
        // the box unapplied. The evidence outranks the draft.
        const SyncStep st = sync_step("", shown_ppm, shown_ppm_valid, residual);
        sync_input->value(sync_text(st.value).c_str());
        // Every measurement is worth something specific, and a short one is
        // worth little [see slant_error_ppm]. Session 29 stopped REFUSING
        // short baselines and started labelling them: the operator gets the
        // number they asked for and the error bar that goes with it, which
        // is the only form in which "this is 55 ppm of your own aim" can
        // reach a person who has already decided they want it.
        const double err = slant_error_ppm(v.scale, drow, v.image_cols);
        char note[96];
        if (std::abs(drow) < min_baseline_rows(v.scale, v.image_cols))
            std::snprintf(note, sizeof note,
                          "SYNC measured over %d rows: +/-%.0f ppm, a short "
                          "baseline", std::abs(drow), err);
        else
            std::snprintf(note, sizeof note,
                          "SYNC measured over %d rows: +/-%.0f ppm",
                          std::abs(drow), err);
        slant_note = note;
        pending_row = -1;
        pending_col = -1;
        arm = Arm::kNone;   // the gesture is complete
        if (!edit_dirty) edit_dirty = true;
        apply_state();
        r.action = ClickAction::kSync;
        r.column = col;
        r.ppm = st.value;
        return r;
    }

    static void cb_image_click(Fl_Widget*, void* p) {
        Shell* s = static_cast<Shell*>(p);
        // x relative to the pane's INTERIOR left edge, which is the frame
        // `column_at` is defined in and the one the ruler is aligned to
        // [§8.0 correction 2]. y is relative to the CHILD, because the
        // child's own position is the truth about vertical scroll —
        // session 27 found `Fl_Scroll::yposition` lying while the pane was
        // visibly wrong. Horizontally the cached copy is used instead, and
        // deliberately: the ruler reads it, so click and ruler agree by
        // construction [see clicked_column].
        s->click_image(Fl::event_x() - (s->pane->x() + kFrame),
                       Fl::event_y() - s->view->y());
    }

    // IOC or Rate changed: the transport gating and the ruler's width both
    // depend on them [§8.4 item 3, §8.3 item 1].
    static void cb_geometry(Fl_Widget*, void* p) {
        Shell* s = static_cast<Shell*>(p);
        s->layout_view();
        s->apply_state();
        s->ruler->redraw();
    }

    // Zoom keeps the left edge [§8.4 item 2]: the column at the pane's left
    // edge stays put, no re-centering and no jump to the start. The
    // arithmetic is nova::rezoomed, the same function ruler_mapping tests.
    static void cb_zoom(Fl_Widget*, void* p) {
        Shell* s = static_cast<Shell*>(p);
        const nova::RulerView after =
            nova::rezoomed(s->view_state(), s->zoom_value());
        s->layout_view();
        s->pane->scroll_to(static_cast<int>(std::lround(after.scroll_px)),
                           s->pane->yposition());
        s->pane->redraw();
        s->ruler->redraw();
    }
};

void Ruler::draw() {
    fl_color(FL_BACKGROUND_COLOR);
    fl_rectf(x(), y(), w(), h());
    const int base = y() + h() - 1;
    fl_color(active() ? FL_FOREGROUND_COLOR : FL_INACTIVE_COLOR);
    fl_line(x(), base, x() + w() - 1, base);
    // Blank while the image width is unknown: a ruler drawn on a guess
    // would be a lie in the one place a lie is most expensive [§8.3 item 1].
    if (!active() || !shell) return;

    const nova::RulerView v = shell->view_state();
    if (v.image_cols <= 0) return;
    fl_font(FL_HELVETICA, 10);
    const int step = nova::tick_step(v.scale);
    for (int c = 0; c < v.image_cols; c += step) {
        const double sx = nova::x_at(v, c);
        if (sx < 0.0 || sx >= w()) continue;
        const int px = x() + static_cast<int>(std::lround(sx));
        const bool major = (c % (step * 5)) == 0;
        fl_line(px, base, px, base - (major ? 7 : 4));
        if (!major) continue;
        char buf[16];
        std::snprintf(buf, sizeof buf, "%d", c);
        const int tw = static_cast<int>(fl_width(buf));
        // The HTML mockup's last label ran off the end of the ruler and
        // into the status panel. A label that would overflow is pulled
        // back inside instead of being drawn where its tick is.
        int tx = px + 2;
        if (tx + tw > x() + w()) tx = x() + w() - tw;
        fl_draw(buf, tx, base - 9);
    }
}

// Fl_Double_Window::resize is still called, because it is what actually
// moves the platform window; layout() then overwrites whatever the group
// scaling did to the children.
void ShellWindow::resize(int X, int Y, int W, int H) {
    Fl_Double_Window::resize(X, Y, W, H);
    if (shell) shell->layout(W, H);
}

int print_devices() {
    std::string err;
    const std::vector<InputDevice> devs = list_input_devices(&err);
    if (devs.empty()) {
        std::printf("nova-gui: %s\n", err.c_str());
        return 1;
    }
    std::printf("%zu input device(s):\n", devs.size());
    for (const InputDevice& d : devs)
        std::printf("  [%u] %-40s %u ch  preferred %u Hz%s\n", d.id,
                    d.name.c_str(), d.channels, d.preferred_rate,
                    d.is_default ? "  (default)" : "");
    return 0;
}

// A one-line snapshot of §8.2's indicator, printed WHERE IT IS ASKED FOR in
// the action sequence [see --mark]. The indicator's rules are all about
// transitions — a buffer that survives an Apply, a count that follows the
// buffered picture while the pane holds still — and a transition is exactly
// what a run that only prints at the end cannot show.
//
// Read off the widget and the engine, never recomputed: an inspection that
// restates a rule pins the restatement, which is this file's standing lesson.
void print_mark(const char* name, const Shell& s) {
    std::printf("mark %-12s recv_active=%d recv_rows=%d recv_complete=%d "
                "pane_held=%d pane_rows=%d edit_dirty=%d saves=%d state=%s\n",
                name, s.recv && s.recv->active() ? 1 : 0,
                s.recv ? s.recv->rows() : 0,
                s.recv && s.recv->complete() ? 1 : 0,
                s.engine && s.engine->pane_held() ? 1 : 0,
                s.view ? s.view->image_rows() : 0, s.edit_dirty ? 1 : 0,
                s.saves_seen, state_token(s.state));
}

void print_metrics_regions(const Shell& s) {
    const unsigned bg = Fl::get_color(FL_BACKGROUND_COLOR);
    std::printf("# nova-gui layout, FLTK %d.%d.%d\n", FL_MAJOR_VERSION,
                FL_MINOR_VERSION, FL_PATCH_VERSION);
    std::printf("# background %02x%02x%02x  normal font size %d\n",
                (bg >> 24) & 0xff, (bg >> 16) & 0xff, (bg >> 8) & 0xff,
                FL_NORMAL_SIZE);
    std::printf("# %-20s %5s %5s %5s %5s\n", "region", "x", "y", "w", "h");
    for (const auto& n : s.named)
        std::printf("  %-20s %5d %5d %5d %5d\n", n.first, n.second->x(),
                    n.second->y(), n.second->w(), n.second->h());
}

void print_metrics_state(const Shell& s) {
    std::printf("# shell state [docs/05 8.3, 8.4]\n");
    std::printf("  state                \"%s\"\n", state_token(s.state));
    std::printf("  status_text          \"%s\"\n", state_text(s.state));
    std::printf("  start_label          \"%s\"\n", s.start->label());
    std::printf("  start_active         \"%d\"\n", s.start->active() ? 1 : 0);
    std::printf("  force_active         \"%d\"\n", s.force->active() ? 1 : 0);
    // Whether the Device menu can be touched in this state — the widget's
    // own sensitivity, so the §8.3 item 9 greying is pinned the same way
    // the transport's is.
    std::printf("  device_active        \"%d\"\n", s.device->active() ? 1 : 0);
    std::printf("  progress_active      \"%d\"\n", s.progress->active() ? 1 : 0);
    // §3: the correction controls are disabled WITH THE REASON. Both halves
    // are inspectable, because "grey" and "grey and says why" are different
    // states and only the second one satisfies §3.
    std::printf("  phase_active         \"%d\"\n",
                s.phase_input->active() ? 1 : 0);
    std::printf("  sync_active          \"%d\"\n",
                s.sync_input->active() ? 1 : 0);
    // One number, not four: the steppers are the SYNC box in another shape
    // and the shell activates them together, so what is worth inspecting is
    // whether ALL of them agree with the box. A disagreement shows up as a
    // count that is neither 0 nor 4.
    int steps_active = 0;
    for (int i = 0; i < Shell::kSyncSteps; i++)
        if (s.sync_step_btn[i]->active()) steps_active++;
    std::printf("  sync_steps_active    \"%d\"\n", steps_active);
    // §8.5 item 4's edit boundary, as the shell actually holds it — so that
    // "a nudge is an edit" is a checkable claim and not just a comment on
    // `nudge_sync`. See --nudge.
    std::printf("  edit_dirty           \"%d\"\n", s.edit_dirty ? 1 : 0);
    // §8.2's indicator [ROADMAP M4 item 6]. Read off the WIDGET rather than
    // recomputed here: an inspection that restates the rule pins the
    // restatement, which is this file's standing lesson.
    std::printf("  recv_active          \"%d\"\n",
                s.recv && s.recv->active() ? 1 : 0);
    std::printf("  recv_rows            \"%d\"\n", s.recv ? s.recv->rows() : 0);
    std::printf("  recv_complete        \"%d\"\n",
                s.recv && s.recv->complete() ? 1 : 0);
    std::printf("  pane_held            \"%d\"\n",
                s.engine && s.engine->pane_held() ? 1 : 0);
    // M4.5's tuning strip. Read off the WIDGET, not recomputed here: an
    // inspection that restates the rule pins the restatement, which is this
    // file's standing lesson.
    std::printf("  strip_visible        \"%d\"\n",
                s.strip && s.strip->visible() ? 1 : 0);
    std::printf("  strip_rows           \"%d\"\n",
                s.strip ? s.strip->strip_rows() : 0);
    // The two marker columns, so that "the tick is where 1500 Hz is" is a
    // checkable claim rather than a comment. -1 means the band does not
    // contain the tone, which would be a band nobody should ship.
    std::printf("  strip_black_col      \"%d\"\n",
                s.strip ? s.strip->black_col() : -1);
    std::printf("  strip_white_col      \"%d\"\n",
                s.strip ? s.strip->white_col() : -1);
}

void print_metrics_detail(const Shell& s, const nova::RulerView& v) {
    // The picture actually ON the pane, which is the number `recv_rows` has
    // to be told apart from. Without it the indicator's line count could
    // only be compared with itself, and "the count names the BUFFERED
    // picture" would be a claim with nothing on the other side of it —
    // session 30's identity trap, which is worth suspecting hardest on the
    // check one cares about most.
    std::printf("  pane_rows            \"%d\"\n",
                s.view ? s.view->image_rows() : 0);
    // How many files the capture has announced, and the last one's name.
    // A transmission buffered behind an edit is still SAVED — §8.2 holds
    // the pane, never the disk — so a count that stops rising here is the
    // shape that defect would take.
    std::printf("  saves_seen           \"%d\"\n", s.saves_seen);
    std::printf("  last_saved           \"%s\"\n",
                s.last_saved.empty()
                    ? ""
                    : s.last_saved.substr(s.last_saved.find_last_of('/') + 1)
                          .c_str());
    std::printf("  sync_value           \"%s\"\n", s.sync_input->value());
    std::printf("  phase_value          \"%s\"\n", s.phase_input->value());
    // The image is a control now [§8.3 item 1], so whether it can act is
    // as inspectable as whether a button can.
    std::printf("  image_click_enabled  \"%d\"\n",
                s.view->click_enabled() ? 1 : 0);
    // Fl_Scroll's cached xposition against the child's own left edge — the
    // horizontal twin of `scroll_y_actual`, which session 27 needed after
    // the cached copy lied. Reported rather than used: the ruler and the
    // click both read the cached one, so they agree with each other by
    // construction, and this is how a divergence would become visible
    // instead of silently moving both.
    std::printf("  scroll_x_actual      \"%d\"\n",
                s.pane->x() + kFrame - s.view->x());
    // The two-click gesture's whole state, and the number that decides
    // what the NEXT click does [see click_image].
    std::printf("  pending_row          \"%d\"\n", s.pending_row);
    std::printf("  min_baseline_rows    \"%d\"\n",
                min_baseline_rows(v.scale, v.image_cols));
    // Which gesture the picture is listening for [see Arm]. This is the
    // value the click guard, the cursor and both buttons all read, so it is
    // the one thing that has to be inspectable for any of them to be
    // checkable — and the three are compared against it rather than against
    // each other, so a disagreement names which one drifted.
    std::printf("  arm                  \"%s\"\n", arm_token(s.arm));
    std::printf("  phase_arm_active     \"%d\"\n",
                s.phase_arm->active() ? 1 : 0);
    std::printf("  sync_arm_active      \"%d\"\n",
                s.sync_arm->active() ? 1 : 0);
    std::printf("  phase_arm_pushed     \"%d\"\n", s.phase_arm->value());
    std::printf("  sync_arm_pushed      \"%d\"\n", s.sync_arm->value());
    // What the last measurement was worth [see slant_error_ppm]. Separate
    // from `correct_reason` even though the reason line shows it, because
    // the two answer different questions and the note has to be checkable
    // in the states where something more perishable is being shown instead.
    std::printf("  slant_note           \"%s\"\n", s.slant_note.c_str());
    std::printf("  apply_active         \"%d\"\n", s.apply->active() ? 1 : 0);
    std::printf("  auto_active          \"%d\"\n", s.autob->active() ? 1 : 0);
    std::printf("  correct_reason       \"%s\"\n", s.correct_reason.c_str());
    std::printf("  ruler_active         \"%d\"\n", s.ruler->active() ? 1 : 0);
    std::printf("  zoom                 \"%s\"\n", s.zoom->text());
    std::printf("  image_cols           \"%d\"\n", v.image_cols);
    std::printf("  tick_step            \"%d\"\n",
                v.image_cols > 0 ? nova::tick_step(v.scale) : 0);
    std::printf("  pane_interior_w      \"%d\"\n", s.pane_interior_w());
    // In pixels rather than as a flag, because the ruler's width is the
    // pane interior LESS the vertical scrollbar: the check in gui_layout
    // subtracts this and so stays exact once images arrive.
    std::printf("  vscroll_px           \"%d\"\n",
                s.vscroll_visible() ? Fl::scrollbar_size() : 0);
    std::printf("  hscroll_px           \"%d\"\n",
                s.hscroll_visible() ? Fl::scrollbar_size() : 0);
    std::printf("  prefs_writable       \"%d\"\n", s.prefs.writable() ? 1 : 0);
    std::printf("  image_folder         \"%s\"\n",
                s.image_folder.empty() ? "(unset)" : s.image_folder.c_str());
}

int print_metrics(const Shell& s) {
    // Real FLTK geometry, so docs/05 §8 is checked against pixels rather
    // than against the HTML mockup that predicted them.
    print_metrics_regions(s);
    // The shell's state, so the §8.3/§8.4 behaviour rules are checkable
    // without a window. Values are quoted, and no line here carries four
    // integers, so the region table above stays unambiguous to parse.
    const nova::RulerView v = s.view_state();
    print_metrics_state(s);
    print_metrics_detail(s, v);
    return 0;
}

// --follow BATCHESxROWS: drive the newest-row follow [§8.3 item 3, Sara
// session 26] with synthetic row batches and report, per batch, where the
// picture ACTUALLY sits against where it should be.
//
// Why the actual offset and not `yposition()`. Fl_Scroll scrolls by moving
// its child, so `yposition()` is a cached copy of the child's position and
// a child resize invalidates it without telling anyone. Session 26's follow
// was written against that cached number and read correct while the picture
// bounced; a check that asks Fl_Scroll where it thinks it is would have
// passed just as happily. `scroll_y_actual` asks the child.
//
// Like --metrics this shows no window and opens no sound card, so it runs
// on a machine with no audio device — the property both GUI screamers rest
// on. It needs no draw either: the divergence is in the widget positions,
// which is why it is catchable without one.
int print_follow(Shell& s, int batches, int rows_per_batch) {
    const int cols = s.image_cols();
    if (cols <= 0) {
        std::fprintf(stderr, "--follow needs an explicit --ioc\n");
        return 2;
    }
    std::printf("# nova-gui follow, FLTK %d.%d.%d\n", FL_MAJOR_VERSION,
                FL_MINOR_VERSION, FL_PATCH_VERSION);
    std::printf("# %5s %7s %7s %11s %8s\n", "batch", "rows", "max_y",
                "yposition", "actual");
    nova::Image img;
    img.width = cols;
    for (int b = 1; b <= batches; b++) {
        img.height = b * rows_per_batch;
        img.px.assign(static_cast<std::size_t>(img.width) * img.height, 128);
        s.show_image(img);
        const int max_y = std::max(0, s.view->h() - s.pane_interior_h());
        std::printf("  %5d %7d %7d %11d %8d\n", b, img.height, max_y,
                    s.pane->yposition(), s.scroll_y_actual());
    }
    return 0;
}

// --correction: the whole truth table of `correction_for`, so §8.5 item 4's
// edit boundary and the "no active button that does nothing" rule are
// checkable without a decoded image behind the shell. Sixteen rows, because
// four booleans is small enough to state completely and a rule stated
// completely cannot be half-wrong somewhere nobody looked.
int print_correction() {
    std::printf("# nova-gui correction surface [docs/05 §7, §8.5 item 4]\n");
    std::printf("# %4s %11s %5s %7s | %6s %5s %4s\n", "live", "can_rerender",
                "dirty", "applied", "inputs", "apply", "auto");
    for (int bits = 0; bits < 16; bits++) {
        const bool live = bits & 1, can = bits & 2, dirty = bits & 4,
                   applied = bits & 8;
        const CorrectionUi c = correction_for(live, can, dirty, applied);
        std::printf("  %4d %11d %5d %7d | %6d %5d %4d\n", live ? 1 : 0,
                    can ? 1 : 0, dirty ? 1 : 0, applied ? 1 : 0,
                    c.inputs_active ? 1 : 0, c.apply_active ? 1 : 0,
                    c.auto_active ? 1 : 0);
    }
    return 0;
}

// --sync-step: where a nudge STARTS, which is the whole of `sync_step` worth
// checking and the one place it can be quietly wrong. A blank box means "as
// measured", so the answer must be the shown clock and not zero: on the
// white-only fixtures those differ by 70 to 118 ppm, which is the entire
// error the control exists to remove. Printed as a table so the check reads
// the RULE rather than a copy of the implementation.
int print_sync_step() {
    struct Case {
        const char* typed;
        double shown;
        bool valid;
        double delta;
    };
    static const Case cases[] = {
        // blank box, a clock behind it: start from the clock, both ways
        {"", -118.0, true, 1.0},   {"", -118.0, true, -1.0},
        {"", -118.0, true, 10.0},  {"", -118.0, true, -10.0},
        {"", -75.2, true, 10.0},
        // blank box, nothing decoded yet: zero is all there is
        {"", 0.0, false, 1.0},     {"", 0.0, false, -10.0},
        // a typed value always wins over the shown clock — it IS the
        // operator's answer, and a nudge moves THAT
        {"-93", -118.0, true, 1.0}, {"-93", -118.0, true, -10.0},
        {"0", -118.0, true, 1.0},   {"12.5", -118.0, true, 1.0},
        // half-typed and unparseable: falls back to the shown clock rather
        // than silently reading as zero
        {"-", -118.0, true, 1.0},   {"-", 0.0, false, 1.0},
    };
    std::printf("# nova-gui SYNC steppers [sync_step]\n");
    std::printf("# %8s %8s %5s %6s | %9s %10s\n", "typed", "shown", "valid",
                "delta", "value", "from_shown");
    for (const Case& c : cases) {
        const SyncStep s = sync_step(c.typed, c.shown, c.valid, c.delta);
        std::printf("  %8s %8.1f %5d %6.1f | %9s %10d\n",
                    c.typed[0] ? c.typed : "\"\"", c.shown, c.valid ? 1 : 0,
                    c.delta, sync_text(s.value).c_str(),
                    s.from_shown ? 1 : 0);
    }
    return 0;
}

// --arm and --click share ONE ordered list, and that is the whole point
// of it: a gesture is a SEQUENCE — arm, click, click — and two separate
// lists could only express "all the arming, then all the clicking",
// which is not any gesture an operator makes. Session 28 learned the
// general form of this the hard way (a check spanning two processes
// cannot observe a transition); this is the same lesson applied before
// the fact rather than after it.
// Session 31 puts the offline capture into the SAME list, for the same
// reason and one step further: §8.2's rules are transitions, so what has
// to be expressible is "feed, edit, feed, mark, apply, mark, click,
// mark" — an interleaving no set of separate lists can say.
struct Action {
    enum Kind {
        kArm,
        kClick,
        kFeed,
        kStopCapture,
        kType,
        kApply,
        kAuto,
        kRecvClick,
        kMark
    } kind;
    Arm arm = Arm::kNone;   // kArm
    int x = 0, y = 0;       // kClick
    std::string text;       // kFeed path, kType value, kMark name
    int pct = 0;            // kFeed
    bool is_sync = false;   // kType
};

// Everything the flag parser fills in [see parse_args]. The defaults are
// the plain run: no inspection, no state, no actions, the §8 window size.
struct ParsedArgs {
    bool devices_only = false;
    bool metrics_only = false;
    int win_w = kWinW;
    int win_h = kWinH;
    int resize_w = 0;
    int resize_h = 0;
    bool correction_only = false;
    bool sync_step_only = false;
    int nudges = 0;
    int click_rows = 0;
    std::string image_folder_arg;
    std::vector<Action> actions;
    LiveState then_state = LiveState::kIdle;
    bool then_state_given = false;
    int follow_batches = 0;
    int follow_rows = 0;
    LiveState state = LiveState::kIdle;
    bool state_given = false;
    int zoom_index = 0;
    int ioc_index = 0;
    int rate_index = 0;
    // M4.5's tuning strip. `strip_given` is separate from `strip_on`
    // because the flag has to be able to say "off" and OUTRANK the
    // remembered preference — otherwise a test's answer would depend on
    // whatever the person running it last chose in the View menu.
    bool strip_on = true;
    bool strip_given = false;
};

// The flags that decide what the run IS — an inspection, a window at a
// size — and how the shell behind it is set up. Returns false when
// argv[*i] is none of them; a recognized flag with a missing or malformed
// value sets *bad instead, which is the same usage error it always was.
bool parse_inspection_flag(int argc, char** argv, int* i, ParsedArgs* a,
                           bool* bad) {
    const char* arg = argv[*i];
    if (!std::strcmp(arg, "--devices")) {
        a->devices_only = true;
        return true;
    }
    if (!std::strcmp(arg, "--metrics")) {
        a->metrics_only = true;
        return true;
    }
    if (!std::strcmp(arg, "--size")) {
        if (*i + 1 < argc &&
            std::sscanf(argv[++(*i)], "%dx%d", &a->win_w, &a->win_h) == 2 &&
            a->win_w >= kMinW && a->win_h >= kMinH)
            return true;
        *bad = true;
        return true;
    }
    if (!std::strcmp(arg, "--resize")) {
        if (*i + 1 < argc &&
            std::sscanf(argv[++(*i)], "%dx%d", &a->resize_w,
                        &a->resize_h) == 2 &&
            a->resize_w >= kMinW && a->resize_h >= kMinH)
            return true;
        *bad = true;
        return true;
    }
    if (!std::strcmp(arg, "--strip")) {
        if (*i + 1 < argc) {
            const char* v = argv[++(*i)];
            if (!std::strcmp(v, "on") || !std::strcmp(v, "off")) {
                a->strip_on = !std::strcmp(v, "on");
                a->strip_given = true;
                return true;
            }
        }
        *bad = true;
        return true;
    }
    if (!std::strcmp(arg, "--correction")) {
        a->correction_only = true;
        return true;
    }
    if (!std::strcmp(arg, "--sync-step")) {
        a->sync_step_only = true;
        return true;
    }
    if (!std::strcmp(arg, "--nudge")) {
        if (*i + 1 < argc && std::sscanf(argv[++(*i)], "%d", &a->nudges) == 1)
            return true;
        *bad = true;
        return true;
    }
    if (!std::strcmp(arg, "--click-rows")) {
        if (*i + 1 < argc &&
            std::sscanf(argv[++(*i)], "%d", &a->click_rows) == 1)
            return true;
        *bad = true;
        return true;
    }
    if (!std::strcmp(arg, "--image-folder")) {
        if (*i + 1 < argc) {
            a->image_folder_arg = argv[++(*i)];
            return true;
        }
        *bad = true;
        return true;
    }
    return false;
}

// The flags that put the shell in a state or a geometry. Same contract as
// parse_inspection_flag.
bool parse_preset_flag(int argc, char** argv, int* i, ParsedArgs* a,
                       bool* bad) {
    const auto index_of = [](const char* text, const char* const* names,
                             int count, int* out) {
        for (int j = 0; j < count; j++)
            if (!std::strcmp(text, names[j])) {
                *out = j;
                return true;
            }
        return false;
    };
    static const char* const kZoomArgs[] = {"fit", "25", "50", "100", "200"};
    static const char* const kIocArgs[] = {"auto", "576", "288"};
    static const char* const kRateArgs[] = {"auto", "60", "90", "120"};
    const char* arg = argv[*i];
    if (!std::strcmp(arg, "--then-state")) {
        if (*i + 1 < argc && parse_state(argv[++(*i)], &a->then_state)) {
            a->then_state_given = true;
            return true;
        }
        *bad = true;
        return true;
    }
    if (!std::strcmp(arg, "--follow")) {
        if (*i + 1 < argc &&
            std::sscanf(argv[++(*i)], "%dx%d", &a->follow_batches,
                        &a->follow_rows) == 2 &&
            a->follow_batches > 0 && a->follow_rows > 0)
            return true;
        *bad = true;
        return true;
    }
    if (!std::strcmp(arg, "--state")) {
        if (*i + 1 < argc && parse_state(argv[++(*i)], &a->state)) {
            a->state_given = true;
            return true;
        }
        *bad = true;
        return true;
    }
    if (!std::strcmp(arg, "--zoom")) {
        if (*i + 1 < argc &&
            index_of(argv[++(*i)], kZoomArgs, 5, &a->zoom_index))
            return true;
        *bad = true;
        return true;
    }
    if (!std::strcmp(arg, "--ioc")) {
        if (*i + 1 < argc &&
            index_of(argv[++(*i)], kIocArgs, 3, &a->ioc_index))
            return true;
        *bad = true;
        return true;
    }
    if (!std::strcmp(arg, "--rate")) {
        if (*i + 1 < argc &&
            index_of(argv[++(*i)], kRateArgs, 4, &a->rate_index))
            return true;
        *bad = true;
        return true;
    }
    return false;
}

// The flags that append to the action list. Same contract again; `push` is
// the one line all nine of these used to write out by hand. The argument
// after the flag comes from `value`, or the flag is not this group's at
// all — a missing argument falls through to the same usage error a
// malformed one gets.
bool parse_action_flag(int argc, char** argv, int* i, ParsedArgs* a,
                       bool* bad) {
    const auto push = [a](Action::Kind kind) -> Action& {
        Action action;
        action.kind = kind;
        a->actions.push_back(action);
        return a->actions.back();
    };
    const auto value = [argc, argv, i](const char* name) -> const char* {
        if (std::strcmp(argv[*i], name) || *i + 1 >= argc) return nullptr;
        return argv[++(*i)];
    };
    if (const char* w = value("--arm")) {
        Action& act = push(Action::kArm);
        if (!std::strcmp(w, "phase")) act.arm = Arm::kPhase;
        else if (!std::strcmp(w, "sync")) act.arm = Arm::kSync;
        else if (!std::strcmp(w, "none")) act.arm = Arm::kNone;
        else *bad = true;
        // "none" presses whichever button is currently down, which is
        // what an operator disarming actually does; `set_arm` toggles,
        // so it is expressed by re-pressing rather than by a third verb.
        return true;
    }
    if (const char* xy = value("--click")) {
        Action& act = push(Action::kClick);
        // X,Y in image-pane coordinates. Repeatable, because
        // the gesture under test is a SEQUENCE of clicks.
        if (std::sscanf(xy, "%d,%d", &act.x, &act.y) != 2 &&
            std::sscanf(xy, "%d", &act.x) != 1)
            *bad = true;
        return true;
    }
    if (const char* f = value("--feed")) {
        Action& act = push(Action::kFeed);
        // PATH,PCT — the comma last, so a path may contain anything but
        // a trailing ",<digits>".
        const std::string spec = f;
        const std::size_t comma = spec.find_last_of(',');
        if (comma == std::string::npos ||
            std::sscanf(spec.c_str() + comma + 1, "%d", &act.pct) != 1 ||
            act.pct <= 0 || act.pct > 100)
            *bad = true;
        else
            act.text = spec.substr(0, comma);
        return true;
    }
    if (!std::strcmp(argv[*i], "--stop-capture")) {
        push(Action::kStopCapture);
        return true;
    }
    if (const char* which = value("--type")) {
        if (*i + 1 >= argc) {   // --type takes TWO: phase|sync, then V
            *bad = true;
            return true;
        }
        Action& act = push(Action::kType);
        if (!std::strcmp(which, "phase")) act.is_sync = false;
        else if (!std::strcmp(which, "sync")) act.is_sync = true;
        else *bad = true;
        act.text = argv[++(*i)];
        return true;
    }
    if (!std::strcmp(argv[*i], "--apply")) {
        push(Action::kApply);
        return true;
    }
    if (!std::strcmp(argv[*i], "--auto")) {
        push(Action::kAuto);
        return true;
    }
    if (!std::strcmp(argv[*i], "--recv-click")) {
        push(Action::kRecvClick);
        return true;
    }
    if (const char* name = value("--mark")) {
        push(Action::kMark).text = name;
        return true;
    }
    return false;
}

void print_usage() {
    std::fprintf(stderr,
                 "usage: nova-gui [--devices] [--metrics] [--size WxH] "
                 "[--resize WxH]\n"
                 "                [--state NAME] [--zoom fit|25|50|100|200]"
                 " [--strip on|off]"
                 "\n                [--ioc auto|576|288] "
                 "[--rate auto|60|90|120]\n"
                 "                [--follow BATCHESxROWS] "
                 "[--correction] [--sync-step]\n"
                 "                [--nudge N] [--click X,Y ...] [--click-rows N]\n"
                 "                [--arm phase|sync|none ...] "
                 "[--then-state NAME]\n"
                 "                [--image-folder DIR] [--feed WAV,PCT "
                 "...] [--stop-capture]\n"
                 "                [--type phase|sync V] [--apply] "
                 "[--auto] [--recv-click]\n"
                 "                [--mark NAME] [--version]\n"
                 "  window minimum %dx%d; states: idle ready start-tone "
                 "phasing\n  drawing stop-tone decoding saved\n",
                 kMinW, kMinH);
}

// The flags, left to right, stopping at the first one nothing recognizes
// or whose value is bad — the usage error it has always been.
bool parse_args(int argc, char** argv, ParsedArgs* a) {
    bool bad = false;
    for (int i = 1; i < argc && !bad; i++)
        if (!parse_inspection_flag(argc, argv, &i, a, &bad) &&
            !parse_preset_flag(argc, argv, &i, a, &bad) &&
            !parse_action_flag(argc, argv, &i, a, &bad))
            bad = true;
    if (bad) {
        print_usage();
        return false;
    }
    return true;
}

// Everything the parsed flags ask the shell to BE before the first action
// runs: built at the window size, dropdowns and state set, nudges pressed,
// and a picture on the pane if a click is coming.
void configure_shell(Shell& shell, const ParsedArgs& args,
                     const char* argv0) {
    shell.build(args.win_w, args.win_h, argv0, args.strip_given,
                args.strip_on);
    // Before anything can feed: `build` has just read the remembered folder
    // out of prefs, and this is what replaces it.
    if (!args.image_folder_arg.empty())
        shell.image_folder = args.image_folder_arg;
    shell.ioc->value(args.ioc_index);
    shell.rate->value(args.rate_index);
    shell.zoom->value(args.zoom_index);
    shell.state = args.state;
    // --state means "as nova-live will drive it": the transport rules are
    // only inspectable once something is behind them, and on a plain run
    // nothing is [see the file header].
    shell.capture = args.state_given;
    shell.layout_view();
    shell.apply_state();
    // A window built at a size and a window dragged to it went through two
    // different code paths until this file stopped using resizable(); the
    // flag stays so that the equivalence keeps being checkable.
    if (args.resize_w > 0)
        shell.win->resize(0, 0, args.resize_w, args.resize_h);
    // --nudge: press the +1 stepper N times, then report. FLTK does not
    // fire an input's callback for a programmatic value(), so "a nudge is
    // an edit" is a claim about code that had to be written by hand and
    // can therefore be wrong by omission — the box would move and Apply
    // would stay grey. This is the seam that makes it checkable.
    for (int i = 0; i < args.nudges; i++) shell.nudge_sync(1.0);
    // --click X: set PHASE from a click X px into the pane's interior,
    // through the shell's real handler [see click_phase]. --click-rows
    // gives the pane a picture first, because a click on an empty pane
    // is a different case and both are worth being able to ask for.
    if (args.click_rows > 0) {
        nova::Image img;
        img.width = shell.image_cols();
        img.height = args.click_rows;
        img.px.assign(static_cast<std::size_t>(img.width) * img.height,
                      128);
        shell.show_image(img);
    }
}

// What the click actions left behind, for --metrics to report after the
// sequence has run; rc is the feed failure that ends the run.
struct ActionOutcome {
    const char* click_action = "none";
    int click_named = -1;
    double click_ppm = 0.0;
    int rc = 0;
};

// Each action goes through the shell's real handler, in order, so the
// sequence under test is the sequence an operator makes. Arming goes
// through `set_arm` — the buttons' own callback body — rather than
// assigning `arm`, or the toggle-and-clear lifecycle would be untested
// exactly where it is easiest to get wrong.
ActionOutcome run_actions(Shell& shell, const std::vector<Action>& actions) {
    ActionOutcome out;
    for (const Action& a : actions) {
        if (a.kind == Action::kArm) {
            shell.set_arm(a.arm);
            continue;
        }
        if (a.kind == Action::kFeed) {
            if (!shell.feed_wav(a.text, a.pct)) {
                std::fprintf(stderr, "nova-gui: cannot feed %s\n",
                             a.text.c_str());
                out.rc = 3;
                return out;
            }
            continue;
        }
        if (a.kind == Action::kStopCapture) {
            shell.stop_capture_and_wait();
            continue;
        }
        if (a.kind == Action::kType) {
            // Through the box AND the box's own callback, in that order,
            // because that is what typing is. Setting the value alone
            // would move the number and leave `edit_dirty` false — the
            // same omission `nudge_sync` exists to make checkable, and a
            // §8.2 scenario built on it would never hold the pane at all.
            Fl_Input* box = a.is_sync
                                ? static_cast<Fl_Input*>(shell.sync_input)
                                : static_cast<Fl_Input*>(shell.phase_input);
            box->value(a.text.c_str());
            Shell::cb_edit(box, &shell);
            continue;
        }
        if (a.kind == Action::kApply) {
            Shell::cb_apply(shell.apply, &shell);
            shell.wait_rerender();
            continue;
        }
        if (a.kind == Action::kAuto) {
            Shell::cb_auto(shell.autob, &shell);
            shell.wait_rerender();
            continue;
        }
        if (a.kind == Action::kRecvClick) {
            // The indicator's own callback, so the guard that makes a click
            // on an empty sidebar inert is the one under test rather than a
            // condition restated here.
            Shell::cb_recv(shell.recv, &shell);
            // Promotion is QUEUED to thread 2 [see promote_background], so
            // it lands on a later drain and not inside the click. An
            // inspection that looked immediately would see the pane
            // unchanged and call the click broken.
            shell.settle(15);
            continue;
        }
        if (a.kind == Action::kMark) {
            print_mark(a.text.c_str(), shell);
            continue;
        }
        const Shell::ClickResult r = shell.click_image(a.x, a.y);
        switch (r.action) {
            case Shell::ClickAction::kNone: out.click_action = "none"; break;
            case Shell::ClickAction::kPhase:
                out.click_action = "phase";
                break;
            case Shell::ClickAction::kAnchor:
                out.click_action = "anchor";
                break;
            case Shell::ClickAction::kSync: out.click_action = "sync"; break;
        }
        out.click_named = r.column;
        out.click_ppm = r.ppm;
    }
    return out;
}

// **A capture WRITES, and the folder it writes to is remembered from the
// operator's own settings.** Every other inspection flag is read-only —
// that is why they can be run on anyone's machine without asking — and
// --feed is the first one that is not. Refusing without an explicit
// folder keeps that property true by construction rather than by the
// test scripts remembering to pass one.
bool feed_without_folder(const ParsedArgs& args, bool any_feed) {
    if (!any_feed || !args.image_folder_arg.empty()) return false;
    std::fprintf(stderr,
                 "nova-gui: --feed needs --image-folder DIR; a capture "
                 "saves images and must never write to the remembered "
                 "folder\n");
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    // Ahead of parse_args, and ahead of every window and sound card:
    // --version is a question about the program [E-GAP-001].
    if (nova::handled_version_flag(argc, argv, "nova-gui")) return 0;
    ParsedArgs args;
    if (!parse_args(argc, argv, &args)) return 2;
    bool any_feed = false;
    for (const Action& a : args.actions)
        if (a.kind == Action::kFeed) any_feed = true;
    if (feed_without_folder(args, any_feed)) return 2;

    if (args.devices_only) return print_devices();
    // Before the Shell is built: it is a pure function, and asking about it
    // must not need a window any more than it needs a sound card.
    if (args.correction_only) return print_correction();
    if (args.sync_step_only) return print_sync_step();

    Shell shell;
    configure_shell(shell, args, argv[0]);
    const ActionOutcome outcome = run_actions(shell, args.actions);
    if (outcome.rc != 0) return outcome.rc;
    // --then-state: drive the shell into a SECOND state after the clicks,
    // which is the only way this seam can express a TRANSITION. Rules that
    // fire when the surface changes under a half-finished edit [§8.5 item
    // 4's edit-end] are otherwise unreachable without a window: --state
    // builds the shell already in a state, so nothing ever changes.
    if (args.then_state_given) {
        shell.state = args.then_state;
        shell.apply_state();
    }
    if (args.metrics_only) {
        // Before the shutdown below, never after: taking the engine down
        // clears `pane_held` and leaves the indicator naming a buffer that
        // no longer exists, so a capture's final metrics have to be read
        // while the capture is still standing.
        const int rc = print_metrics(shell);
        if (!args.actions.empty()) {
            std::printf("  click_action         \"%s\"\n",
                        outcome.click_action);
            std::printf("  click_named          \"%d\"\n",
                        outcome.click_named);
            std::printf("  click_ppm            \"%s\"\n",
                        sync_text(outcome.click_ppm).c_str());
        }
        shell.stop_live();
        return rc;
    }
    if (args.follow_batches > 0)
        return print_follow(shell, args.follow_batches, args.follow_rows);
    // An offline capture is an inspection like all the others: it never
    // reaches the window, and it brings its engine down the same way
    // closing the window does — through the flush that decodes and saves a
    // transmission still in progress [§8.3 item 6], rather than by letting
    // a destructor find the threads still running.
    if (any_feed) {
        shell.stop_live();
        return 0;
    }

    shell.win->show();
    // The live half comes up only now — after the window exists and after
    // every inspection path has already returned. --metrics and --devices
    // never reach this line, which is what keeps them runnable on a
    // machine with no sound card and is what the two GUI screamers rest
    // on.
    shell.start_live();
    const int rc = Fl::run();
    // Closing the window ends the capture through the same path an
    // operator Stop takes, so a transmission in progress is decoded and
    // saved rather than dropped [§8.3 item 6].
    shell.stop_live();
    return rc;
}
