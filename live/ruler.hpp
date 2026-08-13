// ruler.hpp — the image-pane ruler's column mapping [docs/05 §8.3 items
// 1-3]. First component of nova-live.
//
// The ruler is the PHASE-entry surface [docs/04 answer 8], so it reads
// IMAGE columns — 0..1809 at IOC 576, 0..904 at IOC 288, width =
// round(IOC * pi) — and it tracks zoom and horizontal scroll. Its
// correctness claim is the mapping invariant [docs/05 §9 screamer 8]:
//
//   the image column under a given screen x is the column the ruler
//   names at that x,
//
// at every zoom value, every horizontal scroll offset, and both IOC
// widths. That is a pure function of numbers, which is why it lives in
// nova-live rather than as arithmetic inside a widget: it is testable
// with no window and no audio device, and the GUI session consumes it
// verbatim — the click handler and the ruler's draw code both call these
// functions, so they cannot disagree.
//
// While IOC is unknown the ruler is blank and disabled [docs/05 §8.3
// item 1]; that is GUI behaviour, and it is why a RulerView is always
// constructed with a known image_cols — there is no "unknown width"
// state here to test.
#pragma once

namespace nova {

// The zoom control's five values [docs/05 §8.3 item 2]. kFit is a value
// in the same list, never a separate mode [docs/04 Finding 2].
enum class Zoom {
    kFit,  // whole image width scaled into the pane (the default)
    k25,
    k50,
    k100,
    k200,
};

// Screen px per image column. pane_px is the pane INTERIOR width (the
// ruler is aligned to the pane's interior, not its outer box [docs/05
// §8.0 correction 2]). kFit is pane_px / image_cols; the others are fixed
// ratios. A non-positive image_cols or pane_px yields 1.0 — the GUI never
// builds a view then (the ruler is disabled while IOC is unknown), so the
// value is a guard, not a case.
double zoom_scale(Zoom z, int image_cols, int pane_px);

struct RulerView {
    int image_cols = 0;     // decoded image width in columns (1810 / 905)
    double scale = 1.0;     // screen px per column, from zoom_scale()
    int pane_px = 0;        // pane interior width in px
    double scroll_px = 0.0; // horizontal scroll position, screen px
};

// The furthest the view may scroll: the image's right edge against the
// pane's right edge. Zero when the image fits inside the pane, which is
// when the scrollbar does not appear [docs/05 §8.3 item 3].
double max_scroll_px(const RulerView& v);

// v with its scroll clamped into [0, max_scroll_px]. The only way a view
// should be scrolled, so an out-of-range scrollbar position can never
// put the ruler and the image out of agreement.
RulerView scrolled(RulerView v, double scroll_px);

// The image column under screen x, where x is relative to the pane's
// interior left edge. Fractional; the click handler takes the floor and
// rejects columns outside [0, image_cols) — with the image narrower than
// the pane, screen x beyond the image's right edge legitimately maps
// past the last column, and a click there names nothing.
double column_at(const RulerView& v, double x);

// The screen x of image column col, relative to the pane's interior left
// edge. Negative or >= pane_px is off-screen. This is where the ruler
// draws the tick and label for column col.
double x_at(const RulerView& v, double col);

// v re-scaled to zoom z, keeping the image column at the pane's LEFT
// EDGE where it was [docs/05 §8.4 item 2]: the operator's frame of
// reference does not move on a zoom change — no re-centering, no jump to
// the start. The new scroll is clamped like any other, so zooming out
// far enough that the image fits lands at 0 by the same rule the
// scrollbar uses. v.pane_px must already be the interior width the new
// zoom will have, because a scrollbar appearing or leaving changes it.
RulerView rezoomed(RulerView v, Zoom z);

// The tick step in image columns: the smallest of {10, 20, 50, 100, 200,
// 500} that leaves >= 40 px between labels on screen [docs/05 §8.3
// item 1]. At 200% that is 20 columns; at Fit it depends on the window —
// 200 columns at/near the ~880 px minimum, 100 at the 980 px default.
int tick_step(double scale);

}  // namespace nova
