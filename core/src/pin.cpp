#include "boringshot/pin.h"

#include <algorithm>
#include <cmath>

namespace bs {

float nextPinScale(float scale, int wheelDelta) {
    const float notches = static_cast<float>(wheelDelta) / 120.0f;
    const float next = std::max(scale, kPinMinScale) * std::pow(1.1f, notches);
    return std::clamp(next, kPinMinScale, kPinMaxScale);
}

Rect zoomAroundPoint(const Rect& w, Vec2 a, float oldScale, float newScale) {
    if (oldScale <= 0.0f) return w;
    const float k = newScale / oldScale;
    return {a.x - (a.x - w.left) * k, a.y - (a.y - w.top) * k, a.x + (w.right - a.x) * k,
            a.y + (w.bottom - a.y) * k};
}

}  // namespace bs
