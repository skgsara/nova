// ruler.cpp — see ruler.hpp for the contract. Everything here is
// arithmetic on the view's four numbers; nothing reads widget state, so
// the same functions serve the click handler, the ruler's draw code and
// the screamer.
#include "ruler.hpp"

#include <algorithm>

namespace nova {

double zoom_scale(Zoom z, int image_cols, int pane_px) {
    switch (z) {
        case Zoom::k25: return 0.25;
        case Zoom::k50: return 0.50;
        case Zoom::k100: return 1.0;
        case Zoom::k200: return 2.0;
        case Zoom::kFit: break;
    }
    if (image_cols <= 0 || pane_px <= 0) return 1.0;  // guard, not a case
    return static_cast<double>(pane_px) / image_cols;
}

double max_scroll_px(const RulerView& v) {
    const double d = v.image_cols * v.scale - v.pane_px;
    // At Fit, d is cols * (pane/cols) - pane, which is one rounding error
    // away from 0 in either direction. A positive residue that small is
    // not a scrollable range — it would show a scrollbar that scrolls
    // nothing — so snap it to the "image fits" answer.
    return d > 1e-9 * v.pane_px ? d : 0.0;
}

RulerView scrolled(RulerView v, double scroll_px) {
    v.scroll_px = std::min(std::max(scroll_px, 0.0), max_scroll_px(v));
    return v;
}

double column_at(const RulerView& v, double x) {
    return (x + v.scroll_px) / v.scale;
}

double x_at(const RulerView& v, double col) {
    return col * v.scale - v.scroll_px;
}

int tick_step(double scale) {
    // docs/05 §8.3 item 1: the smallest step in {10, 20, 50, 100, 200,
    // 500} leaving >= 40 px between labels on screen.
    static const int kSteps[] = {10, 20, 50, 100, 200, 500};
    for (const int step : kSteps)
        if (step * scale >= 40.0) return step;
    return 500;  // smaller than the table covers; labels thin, never crowd
}

}  // namespace nova
