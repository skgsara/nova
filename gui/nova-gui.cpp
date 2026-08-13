// nova-gui — the M4 walking skeleton: the window of docs/05 §8 built from
// real FLTK widgets, with nothing behind them.
//
// Why this binary exists. The §8 layout was drawn in HTML at FLTK's
// documented metrics, which makes it a prediction. This is the measurement.
// Wherever the toolkit disagrees with the mockup, `docs/05` §8 is the thing
// that gets corrected — not this file.
//
// What it deliberately does NOT do: no decode, no threads, no DSP, no audio
// stream, none of §2's concurrency. The one live wire is RtAudio device
// enumeration, because the other thing a skeleton proves is that the
// dependency wiring works. Every control that would need a decode behind it
// is created deactivated, so the window does not claim to do what it cannot
// [docs/05 §3: an image with no raw behind it shows PHASE/SYNC visibly
// disabled rather than silently inert].
//
// Two flags make the skeleton inspectable without a window, which is also
// how it gets checked on a machine with no audio device:
//   --devices   list the input devices RtAudio reports, then exit
//   --metrics   print every region's real FLTK geometry, then exit
#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Float_Input.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Int_Input.H>
#include <FL/Fl_Progress.H>
#include <FL/fl_draw.H>

#include <RtAudio.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Metrics. Every number the layout rests on, named, so that a disagreement
// with the mockup is a one-line correction rather than an archaeology
// exercise. Row heights come from docs/05 §8; the window size does not —
// §8 never fixed one, so this is a choice made here and recorded.
constexpr int kWinW = 980;
constexpr int kWinH = 700;
// The control row is the widest fixed thing in the window: captions and
// controls out to the Rate menu (548 px), then Start and Force Start against
// the right edge (168 px), plus a gap. Narrower than this and they collide.
constexpr int kMinW = 740;
constexpr int kMinH = 420;
constexpr int kControlRowH = 25;  // §8: "25 px control rows"
constexpr int kRulerH = 18;
constexpr int kMeterH = 18;  // §8
constexpr int kStatusH = 22;
constexpr int kPanelW = 200;
constexpr int kPad = 4;
constexpr int kPanelRowH = 20;
constexpr int kFontSize = 12;  // §8: "12 px Helvetica"
constexpr int kFrame = 2;      // §8: "two-pixel FL_UP_BOX / FL_DOWN_BOX"

// Ruler ticks are in image columns, so with no image loaded they are pane
// columns — the only coordinate a skeleton has.
constexpr int kMajorTick = 100;
constexpr int kMinorTick = 20;

// ---------------------------------------------------------------------------
// The ruler above the image pane. It is the phase-entry affordance
// [docs/04: the ruler/coordinate pattern, re-confirmed session 16], and in
// the skeleton it draws its scale and nothing else.
class Ruler : public Fl_Widget {
public:
    Ruler(int x, int y, int w, int h) : Fl_Widget(x, y, w, h) {}

    void draw() override {
        fl_color(FL_BACKGROUND_COLOR);
        fl_rectf(x(), y(), w(), h());
        fl_color(FL_FOREGROUND_COLOR);
        const int base = y() + h() - 1;
        fl_line(x(), base, x() + w() - 1, base);
        fl_font(FL_HELVETICA, 10);
        for (int c = 0; c < w(); c += kMinorTick) {
            const int px = x() + c;
            const bool major = (c % kMajorTick) == 0;
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

    void draw() override {
        fl_draw_box(FL_DOWN_BOX, x(), y(), w(), h(), FL_BACKGROUND_COLOR);
        fl_font(FL_HELVETICA, kFontSize);
        fl_color(FL_INACTIVE_COLOR);
        fl_draw("input level", x() + kPad, y(), w(), h(),
                FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        fl_draw("-- dBFS", x(), y(), w() - kPad, h(),
                FL_ALIGN_RIGHT | FL_ALIGN_INSIDE);
    }
};

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

// Enumeration is the whole of the skeleton's RtAudio use: no stream is
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

constexpr int kFields = 6;  // Mode, IOC, Rate, State, Quality, Started

struct Shell {
    ShellWindow* win = nullptr;
    Fl_Box* cap_device = nullptr;
    Fl_Choice* device = nullptr;
    Fl_Box* cap_ioc = nullptr;
    Fl_Choice* ioc = nullptr;
    Fl_Box* cap_rate = nullptr;
    Fl_Choice* rate = nullptr;
    Fl_Button* start = nullptr;
    Fl_Button* force = nullptr;
    Ruler* ruler = nullptr;
    Fl_Box* pane = nullptr;
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
    Fl_Button* apply = nullptr;
    Fl_Button* autob = nullptr;
    LevelMeter* meter = nullptr;
    Fl_Box* status_bg = nullptr;
    Fl_Box* status_state = nullptr;
    Fl_Box* status_lines = nullptr;
    Fl_Progress* progress = nullptr;

    std::vector<InputDevice> devices;
    std::string device_error;
    std::vector<std::pair<const char*, Fl_Widget*>> named;

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

    void build(int win_w, int win_h) {
        FL_NORMAL_SIZE = kFontSize;
        win = new ShellWindow(win_w, win_h, "Nova");
        win->shell = this;
        note("window", win);
        create();
        win->end();
        // No resizable() child: the layout is computed, not scaled. FLTK
        // still lets the user resize because size_range says so.
        win->size_range(kMinW, kMinH);
        layout(win_w, win_h);
        populate_devices();
    }

    void create() {
        // --- control row [§8 row 1] -----------------------------------------
        cap_device = caption("Device");
        device = new Fl_Choice(0, 0, 0, 0);
        device->textsize(kFontSize);
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
        note("ioc_choice", ioc);

        cap_rate = caption("Rate");
        rate = new Fl_Choice(0, 0, 0, 0);
        rate->textsize(kFontSize);
        rate->add("Auto");
        rate->add("60 lpm");
        rate->add("90 lpm");
        rate->add("120 lpm");
        rate->value(0);
        note("rate_choice", rate);

        // Forced start is on every one of the sixteen receivers without
        // exception [docs/04 Finding 2], so it is a peer of Start here, not
        // something folded into a menu.
        start = new Fl_Button(0, 0, 0, 0, "Start");
        start->labelsize(kFontSize);
        start->deactivate();
        note("start_button", start);
        force = new Fl_Button(0, 0, 0, 0, "Force Start");
        force->labelsize(kFontSize);
        force->deactivate();
        note("force_start_button", force);

        // --- image pane and its ruler ----------------------------------------
        ruler = new Ruler(0, 0, 0, 0);
        note("ruler", ruler);
        pane = new Fl_Box(0, 0, 0, 0);
        pane->box(FL_DOWN_BOX);
        pane->color(FL_BACKGROUND2_COLOR);
        note("image_pane", pane);

        // --- status panel [§8 right, fields per §8.1] -------------------------
        // §8.1 is why there is no frequency, channel or call sign here: every
        // receiver in the corpus contains its own radio, and Nova is a
        // decoder on the end of a cable from someone else's.
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

        // The two manual corrections, and the only two on any receiver in
        // the corpus that has controls [docs/04, confirmed twice]. They are
        // asymmetric behind the glass — PHASE seeds the batch anchor search,
        // SYNC is only a fallback where the batch fit has no baseline
        // [docs/05 §7.1] — but that asymmetry is not in the skeleton, which
        // has no batch decode to hand them to.
        cap_phase = caption("PHASE");
        phase_input = new Fl_Int_Input(0, 0, 0, 0);
        phase_input->textsize(kFontSize);
        phase_input->deactivate();
        note("phase_input", phase_input);
        cap_sync = caption("SYNC");
        sync_input = new Fl_Float_Input(0, 0, 0, 0);
        sync_input->textsize(kFontSize);
        sync_input->deactivate();
        note("sync_input", sync_input);

        apply = new Fl_Button(0, 0, 0, 0, "Apply");
        apply->labelsize(kFontSize);
        apply->deactivate();
        note("apply_button", apply);
        autob = new Fl_Button(0, 0, 0, 0, "Auto");
        autob->labelsize(kFontSize);
        autob->deactivate();
        note("auto_button", autob);

        // --- level meter and status line -------------------------------------
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
        // [docs/05 §8]. Drawn here empty and deactivated, because the point
        // of a skeleton is to measure the region it will occupy.
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
        int x = kPad;
        cap_device->resize(x, 0, 44, kControlRowH);
        x += 46;
        device->resize(x, 2, 240, kControlRowH - 4);
        x += 250;
        cap_ioc->resize(x, 0, 26, kControlRowH);
        x += 28;
        ioc->resize(x, 2, 84, kControlRowH - 4);
        x += 94;
        cap_rate->resize(x, 0, 32, kControlRowH);
        x += 34;
        rate->resize(x, 2, 92, kControlRowH - 4);
        force->resize(W - kPad - 96, 2, 96, kControlRowH - 4);
        start->resize(W - kPad - 164, 2, 64, kControlRowH - 4);

        const int main_y = kControlRowH;
        const int main_h = H - kControlRowH - kMeterH - kStatusH;
        const int pane_w = W - kPanelW - 2 * kPad;

        // The ruler is aligned to the pane's INTERIOR, not to the window or
        // to the pane's outer box. It is the phase-entry affordance [docs/04,
        // the ruler/coordinate pattern], so a tick that does not name the
        // image column beneath it is the one thing it must never do. kFrame
        // is the FL_DOWN_BOX bevel the pane draws inside its own edge.
        ruler->resize(kPad + kFrame, main_y, pane_w - 2 * kFrame, kRulerH);
        pane->resize(kPad, main_y + kRulerH, pane_w, main_h - kRulerH - kPad);

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
        cap_phase->resize(px + kPad, py, 62, kPanelRowH);
        phase_input->resize(px + kPad + 66, py + 1, 70, kPanelRowH - 2);
        py += kPanelRowH;
        cap_sync->resize(px + kPad, py, 62, kPanelRowH);
        sync_input->resize(px + kPad + 66, py + 1, 70, kPanelRowH - 2);
        py += kPanelRowH + kPad;
        apply->resize(px + kPad, py, 70, 21);
        autob->resize(px + kPad + 74, py, 70, 21);

        meter->resize(0, main_y + main_h, W, kMeterH);
        const int sy = main_y + main_h + kMeterH;
        status_bg->resize(0, sy, W, kStatusH);
        status_state->resize(kPad, sy, 240, kStatusH);
        status_lines->resize(kPad + 250, sy, 120, kStatusH);
        progress->resize(W - kPad - 160, sy + 3, 160, kStatusH - 6);
    }

    void populate_devices() {
        devices = list_input_devices(&device_error);
        if (devices.empty()) {
            device->add(escape_menu_label("(no input device)").c_str());
            device->value(0);
            device->deactivate();
            return;
        }
        int def = 0;
        for (size_t i = 0; i < devices.size(); i++) {
            device->add(escape_menu_label(devices[i].name).c_str());
            if (devices[i].is_default) def = static_cast<int>(i);
        }
        device->value(def);
    }
};

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

int print_metrics(const Shell& s) {
    // Real FLTK geometry, so docs/05 §8 is checked against pixels rather
    // than against the HTML mockup that predicted them.
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
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    bool devices_only = false;
    bool metrics_only = false;
    int win_w = kWinW;
    int win_h = kWinH;
    int resize_w = 0;
    int resize_h = 0;
    for (int i = 1; i < argc; i++) {
        if (!std::strcmp(argv[i], "--devices"))
            devices_only = true;
        else if (!std::strcmp(argv[i], "--metrics"))
            metrics_only = true;
        else if (!std::strcmp(argv[i], "--size") && i + 1 < argc &&
                 std::sscanf(argv[++i], "%dx%d", &win_w, &win_h) == 2 &&
                 win_w >= kMinW && win_h >= kMinH)
            ;
        else if (!std::strcmp(argv[i], "--resize") && i + 1 < argc &&
                 std::sscanf(argv[++i], "%dx%d", &resize_w, &resize_h) == 2 &&
                 resize_w >= kMinW && resize_h >= kMinH)
            ;
        else {
            std::fprintf(stderr,
                         "usage: nova-gui [--devices] [--metrics] "
                         "[--size WxH] [--resize WxH]  (min %dx%d)\n",
                         kMinW, kMinH);
            return 2;
        }
    }
    if (devices_only) return print_devices();

    Shell shell;
    shell.build(win_w, win_h);
    // A window built at a size and a window dragged to it went through two
    // different code paths until this file stopped using resizable(); the
    // flag stays so that the equivalence keeps being checkable.
    if (resize_w > 0) shell.win->resize(0, 0, resize_w, resize_h);
    if (metrics_only) return print_metrics(shell);

    shell.win->show();
    return Fl::run();
}
