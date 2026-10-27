#pragma once

#include <string>
#include <vector>

#include "boringshot/command_stack.h"
#include "boringshot/geometry.h"
#include "boringshot/mleader.h"

namespace bs {

enum class ShapeKind { Rect, Ellipse, Line, Arrow, Pen, Text, MLeader };

struct Color {
    float r = 1.0f, g = 0.25f, b = 0.25f, a = 1.0f;
    constexpr bool operator==(const Color&) const = default;
};

struct Shape {
    ShapeKind kind = ShapeKind::Rect;
    Color color;
    float strokeWidth = 3.0f;
    Vec2 a, b;                  // drag start/end; Text: a = top-left
    std::vector<Vec2> points;   // Pen
    std::wstring text;          // Text / MLeader label
    float fontSize = 20.0f;
    MLeaderInput leader;        // MLeader
};

// Shift: square Rect/Ellipse, 45°-snapped Line/Arrow. Other kinds pass through.
Vec2 constrainDrag(ShapeKind kind, Vec2 start, Vec2 current, bool shift);

// Appends p unless it is closer than minDist to the last point (stroke decimation).
void appendPenPoint(std::vector<Vec2>& points, Vec2 p, float minDist);

// A finished drag shape is kept only if it has visible extent.
bool isMeaningful(const Shape& s);

// Annotation list with undo/redo; every mutation goes through the history.
class Document {
public:
    const std::vector<Shape>& shapes() const { return shapes_; }
    bool empty() const { return shapes_.empty(); }

    bool add(Shape s);  // false (and no history entry) if not meaningful
    bool undo() { return history_.undo(); }
    bool redo() { return history_.redo(); }
    bool canUndo() const { return history_.canUndo(); }
    bool canRedo() const { return history_.canRedo(); }
    void clear();

private:
    std::vector<Shape> shapes_;
    CommandStack history_{512};
};

}  // namespace bs
