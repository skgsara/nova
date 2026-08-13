// test_ruler_mapping.cpp — §9 screamer 8 [docs/05]: the image column
// under a given screen x is the column the ruler names at that x, at
// every zoom value, every horizontal scroll offset, and both IOC widths
// (1810 and 905). It exercises the same functions the click handler and
// the ruler's draw code call (live/ruler.hpp), so it needs no window and
// no audio device — and a regression here means the ruler would name a
// column that is not under it, which is the one failure the phase-entry
// surface cannot have.
//
// Claims defended (docs/05 §8.3 items 1-3, §9 item 8):
//   - x_at and column_at are exact inverses at every zoom, scroll offset
//     and both widths, so a tick drawn for column c sits over column c;
//   - the image's left edge maps to column 0 and its right edge to
//     image_cols, so the ruler's range is 0..cols-1 and nothing else;
//   - every screen pixel inside the image's span names a valid column,
//     and a pixel past the image's right edge names none (the click
//     handler rejects column >= image_cols);
//   - scroll clamps into [0, max_scroll_px], and max_scroll_px is 0
//     exactly when the image fits the pane (no scrollbar then [§8.3
//     item 3]);
//   - the tick step is the smallest of {10,20,50,100,200,500} leaving
//     >= 40 px between labels: 20 columns at 200%, and at Fit 200 near
//     the ~880 px minimum window but 100 at the 980 px default — the
//     docs/05 example stated 200 for Fit without a window size.
#include "../live/ruler.hpp"
#include <cmath>
#include <cstdio>

namespace {
int failures = 0;

void check(bool ok, const char* what) {
    std::printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}

const double kEps = 1e-9;

// The views a session can actually produce: both IOC widths, pane
// interiors from the current minimum window (740 -> 528 px) through the
// post-Zoom minimum (~880 -> 668) and the 980 default (768) to a
// maximized window (1920 -> 1708), and all five zoom values.
struct Case {
    int cols;
    int pane;
    nova::Zoom zoom;
};

void check_view(const Case& tc, double scroll_request) {
    nova::RulerView v;
    v.image_cols = tc.cols;
    v.pane_px = tc.pane;
    v.scale = nova::zoom_scale(tc.zoom, tc.cols, tc.pane);
    v = nova::scrolled(v, scroll_request);

    char what[160];

    // Scroll is inside [0, max]; a fit image cannot scroll at all.
    const double smax = nova::max_scroll_px(v);
    std::snprintf(what, sizeof what,
                  "cols=%d pane=%d scroll_req=%.1f: scroll %.4f in [0, %.4f]",
                  tc.cols, tc.pane, scroll_request, v.scroll_px, smax);
    check(v.scroll_px >= 0.0 && v.scroll_px <= smax + kEps, what);
    if (tc.zoom == nova::Zoom::kFit) {
        std::snprintf(what, sizeof what,
                      "cols=%d pane=%d Fit: image fits, no scroll",
                      tc.cols, tc.pane);
        check(smax == 0.0 && v.scroll_px == 0.0, what);
    }

    // The invariant itself, both directions, at the extreme and interior
    // columns. A tick drawn for column c sits over column c.
    const int probe_cols[] = {0, 1, 17, tc.cols / 2, tc.cols - 2,
                              tc.cols - 1, tc.cols};
    for (const int c : probe_cols) {
        const double rt = nova::column_at(v, nova::x_at(v, c));
        std::snprintf(what, sizeof what,
                      "cols=%d pane=%d scroll=%.1f: column_at(x_at(%d)) == %d",
                      tc.cols, tc.pane, v.scroll_px, c, c);
        check(std::fabs(rt - c) < kEps, what);
    }

    // The ruler's range is exactly 0..cols-1: the image's left edge names
    // column 0 and its right edge names one past the last column.
    std::snprintf(what, sizeof what,
                  "cols=%d pane=%d scroll=%.1f: left edge is column 0",
                  tc.cols, tc.pane, v.scroll_px);
    check(std::fabs(nova::column_at(v, nova::x_at(v, 0.0))) < kEps, what);
    std::snprintf(what, sizeof what,
                  "cols=%d pane=%d scroll=%.1f: right edge is column %d",
                  tc.cols, tc.pane, v.scroll_px, tc.cols);
    check(std::fabs(nova::column_at(v, nova::x_at(v, tc.cols)) - tc.cols) <
              kEps,
          what);

    // Every screen pixel: inside the image's span it names a valid
    // column; past the image's right edge it names none. x_at(0) <= 0
    // always (scroll >= 0), so the span starts at the pane edge or later.
    bool span_ok = true;
    bool past_ok = true;
    for (int x = 0; x < tc.pane; x++) {
        const double c = nova::column_at(v, x);
        if (x < nova::x_at(v, tc.cols)) {
            if (c < 0.0 || std::floor(c) >= tc.cols) span_ok = false;
        } else {
            if (c < tc.cols - kEps) past_ok = false;
        }
    }
    std::snprintf(what, sizeof what,
                  "cols=%d pane=%d scroll=%.1f: every pixel in span names "
                  "a column in [0,%d)",
                  tc.cols, tc.pane, v.scroll_px, tc.cols);
    check(span_ok, what);
    std::snprintf(what, sizeof what,
                  "cols=%d pane=%d scroll=%.1f: pixels past the image "
                  "name nothing",
                  tc.cols, tc.pane, v.scroll_px);
    check(past_ok, what);

    // Inverse direction per pixel: x_at(column_at(x)) returns x.
    bool px_rt = true;
    for (int x = 0; x < tc.pane; x += 7)
        if (std::fabs(nova::x_at(v, nova::column_at(v, x)) - x) > kEps)
            px_rt = false;
    std::snprintf(what, sizeof what,
                  "cols=%d pane=%d scroll=%.1f: x_at(column_at(x)) == x",
                  tc.cols, tc.pane, v.scroll_px);
    check(px_rt, what);

    // Every tick the ruler would draw: the tick for column c sits at
    // x_at(c), and the column under that screen position is c.
    const int step = nova::tick_step(v.scale);
    bool ticks_ok = true;
    for (int c = 0; c < tc.cols; c += step) {
        const double x = nova::x_at(v, c);
        if (x >= 0.0 && x < tc.pane &&
            std::fabs(nova::column_at(v, x) - c) > kEps)
            ticks_ok = false;
    }
    std::snprintf(what, sizeof what,
                  "cols=%d pane=%d scroll=%.1f: every tick names its "
                  "column (step %d)",
                  tc.cols, tc.pane, v.scroll_px, step);
    check(ticks_ok, what);
}

}  // namespace

int main() {
    std::printf("ruler_mapping: the column under a screen x is the column "
                "named there\n");

    // Zoom scale factors, including Fit == pane / cols exactly.
    check(nova::zoom_scale(nova::Zoom::k25, 1810, 768) == 0.25,
          "zoom 25%");
    check(nova::zoom_scale(nova::Zoom::k50, 1810, 768) == 0.50,
          "zoom 50%");
    check(nova::zoom_scale(nova::Zoom::k100, 1810, 768) == 1.0,
          "zoom 100%");
    check(nova::zoom_scale(nova::Zoom::k200, 1810, 768) == 2.0,
          "zoom 200%");
    check(std::fabs(nova::zoom_scale(nova::Zoom::kFit, 1810, 768) -
                    768.0 / 1810.0) < kEps,
          "zoom Fit = pane / cols (IOC 576)");
    check(std::fabs(nova::zoom_scale(nova::Zoom::kFit, 905, 768) -
                    768.0 / 905.0) < kEps,
          "zoom Fit = pane / cols (IOC 288)");
    check(nova::zoom_scale(nova::Zoom::kFit, 0, 768) == 1.0,
          "Fit with unknown width is a guard, not a case");

    // Tick steps [docs/05 §8.3 item 1]. The document's two stated
    // examples are both pinned; its Fit example ("200 columns") holds
    // near the minimum window, while the default 980 px window gives
    // 100 — the doc did not say at which window size.
    check(nova::tick_step(2.0) == 20, "tick step at 200% is 20 [doc]");
    check(nova::tick_step(1.0) == 50, "tick step at 100% is 50");
    check(nova::tick_step(0.5) == 100, "tick step at 50% is 100");
    check(nova::tick_step(0.25) == 200, "tick step at 25% is 200");
    check(nova::tick_step(nova::zoom_scale(nova::Zoom::kFit, 1810, 528)) ==
              200,
          "tick step at Fit, 740 px window, is 200 [doc's example]");
    check(nova::tick_step(nova::zoom_scale(nova::Zoom::kFit, 1810, 668)) ==
              200,
          "tick step at Fit, ~880 px window, is 200");
    check(nova::tick_step(nova::zoom_scale(nova::Zoom::kFit, 1810, 768)) ==
              100,
          "tick step at Fit, 980 px default window, is 100");
    check(nova::tick_step(nova::zoom_scale(nova::Zoom::kFit, 905, 768)) ==
              50,
          "tick step at Fit, IOC 288, is 50");
    check(nova::tick_step(0.1) == 500, "tick step floor is 500");
    check(nova::tick_step(0.05) == 500, "tick step below the table is 500");
    check(nova::tick_step(4.0) == 10, "tick step ceiling is 10");

    // Scroll clamping, stated directly.
    {
        nova::RulerView v{905, 0.25, 768, 0.0};  // 226 px wide: fits
        check(nova::max_scroll_px(v) == 0.0,
              "image narrower than the pane cannot scroll");
        check(nova::scrolled(v, 500.0).scroll_px == 0.0,
              "scroll request on a fit image clamps to 0");
        nova::RulerView w{1810, 2.0, 768, 0.0};  // 3620 px wide
        const double smax = 1810 * 2.0 - 768;
        check(std::fabs(nova::max_scroll_px(w) - smax) < kEps,
              "max scroll puts the right edge at the pane edge");
        check(nova::scrolled(w, -50.0).scroll_px == 0.0,
              "negative scroll clamps to 0");
        check(std::fabs(nova::scrolled(w, smax + 50.0).scroll_px - smax) <
                  kEps,
              "overscroll clamps to max");
    }

    // The mapping invariant across the whole matrix: both IOC widths,
    // four pane widths, five zooms, and scroll positions covering 0,
    // interior, max, and both clamp directions.
    const int widths[] = {1810, 905};
    const int panes[] = {528, 668, 768, 1708};
    const nova::Zoom zooms[] = {nova::Zoom::kFit, nova::Zoom::k25,
                                nova::Zoom::k50, nova::Zoom::k100,
                                nova::Zoom::k200};
    for (const int cols : widths)
        for (const int pane : panes)
            for (const nova::Zoom z : zooms) {
                const Case tc{cols, pane, z};
                const double scale = nova::zoom_scale(z, cols, pane);
                const nova::RulerView base{cols, scale, pane, 0.0};
                const double smax = nova::max_scroll_px(base);
                check_view(tc, 0.0);
                check_view(tc, smax * 0.37);
                check_view(tc, smax);
                check_view(tc, -500.0);       // clamps to 0
                check_view(tc, smax + 500.0); // clamps to max
            }

    if (failures == 0)
        std::printf("ruler_mapping: all checks passed\n");
    else
        std::printf("ruler_mapping: %d check(s) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
