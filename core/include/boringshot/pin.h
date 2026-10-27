#pragma once

#include "boringshot/geometry.h"

namespace bs {

inline constexpr float kPinMinScale = 0.1f;
inline constexpr float kPinMaxScale = 8.0f;

// Mouse wheel zoom for a pinned image: ×1.1 per notch (120 units), clamped.
float nextPinScale(float scale, int wheelDelta);

// Window rect after zooming so the point under `anchor` stays put.
Rect zoomAroundPoint(const Rect& window, Vec2 anchor, float oldScale, float newScale);

}  // namespace bs
