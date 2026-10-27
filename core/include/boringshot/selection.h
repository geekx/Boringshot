#pragma once

#include <optional>
#include <vector>

#include "boringshot/geometry.h"

namespace bs {

Rect normalizedRect(Vec2 a, Vec2 b);
bool contains(const Rect& r, Vec2 p);
// Overlap of a and b; zero-sized (at a's clamped corner) when disjoint.
Rect intersect(const Rect& a, const Rect& b);
bool isEmpty(const Rect& r);

// A press/release pair that barely moved is a click (select hovered window),
// otherwise a drag (custom region).
bool isClick(Vec2 down, Vec2 up, float threshold = 3.0f);

// Topmost (first) rect containing p; windows come in Z order, monitors last.
std::optional<Rect> pickWindowRect(const std::vector<Rect>& zOrder, Vec2 p);

enum class Handle { None, Move, N, S, E, W, NE, NW, SE, SW };

Handle hitTestHandle(const Rect& r, Vec2 p, float tolerance);

// Applies a drag of `delta` on `handle` to `start`. Move keeps the size and
// stays inside `bounds`; resizing may cross over (re-normalised) and is clipped.
Rect dragHandle(const Rect& start, Handle handle, Vec2 delta, const Rect& bounds);

// Toolbar: right-aligned under the selection, else above it, else inside its
// bottom edge; always clamped to `screen`.
Rect placeToolbar(const Rect& selection, float width, float height, const Rect& screen,
                  float gap);

// Popup (magnifier) at bottom-right of the cursor, flipped on each axis when it
// would leave `screen`.
Rect placeNearCursor(Vec2 cursor, float width, float height, const Rect& screen, float offset);

}  // namespace bs
