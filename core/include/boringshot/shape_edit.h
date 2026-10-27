#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "boringshot/annotation.h"

namespace bs {

float distanceToSegment(Vec2 p, Vec2 a, Vec2 b);

// Draggable joints. Rect/Ellipse/Mosaic: 4 corners; Line/Arrow: a, b;
// MLeader: arrow tip, kink, landing end. Pen/Text move as a whole only.
std::vector<Vec2> controlPoints(const Shape& s);

// Moves joint `index` to p. Shift keeps rects square and snaps segments to 45°
// against the neighbouring joint. Moving an MLEADER kink carries the landing along.
Shape moveControlPoint(const Shape& s, int index, Vec2 p, bool shift);

Shape translate(const Shape& s, Vec2 d);

// Visual extent including stroke, label and caps (for selection outlines).
Rect shapeBounds(const Shape& s);

bool hitShapeBody(const Shape& s, Vec2 p, float tolerance);

struct ShapeHit {
    std::size_t index = 0;
    int handle = -1;  // -1: body
};

// Joints of the selected shape win, then bodies from the top of the stack down.
std::optional<ShapeHit> hitTestShapes(const std::vector<Shape>& shapes, Vec2 p, float tolerance,
                                      std::optional<std::size_t> selected);

}  // namespace bs
