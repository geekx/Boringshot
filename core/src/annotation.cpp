#include "boringshot/annotation.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace bs {
namespace {

class AddShape : public Command {
public:
    AddShape(std::vector<Shape>& list, Shape s) : list_(list), shape_(std::move(s)) {}
    void apply() override { list_.push_back(shape_); }
    void revert() override { list_.pop_back(); }  // history is LIFO, so ours is last

private:
    std::vector<Shape>& list_;
    Shape shape_;
};

}  // namespace

Vec2 constrainDrag(ShapeKind kind, Vec2 start, Vec2 cur, bool shift) {
    if (!shift) return cur;
    switch (kind) {
        case ShapeKind::Rect:
        case ShapeKind::Ellipse: {
            const Vec2 d = cur - start;
            const float side = std::max(std::fabs(d.x), std::fabs(d.y));
            return start + Vec2{d.x < 0 ? -side : side, d.y < 0 ? -side : side};
        }
        case ShapeKind::Line:
        case ShapeKind::Arrow:
            return snapKink(start, cur, 45.0f);
        default:
            return cur;
    }
}

void appendPenPoint(std::vector<Vec2>& points, Vec2 p, float minDist) {
    if (points.empty() || length(p - points.back()) >= minDist) points.push_back(p);
}

bool isMeaningful(const Shape& s) {
    switch (s.kind) {
        case ShapeKind::Rect:
        case ShapeKind::Ellipse:
            return std::fabs(s.b.x - s.a.x) >= 2.0f && std::fabs(s.b.y - s.a.y) >= 2.0f;
        case ShapeKind::Line:
        case ShapeKind::Arrow:
            return length(s.b - s.a) >= 3.0f;
        case ShapeKind::Pen:
            return s.points.size() >= 2;
        case ShapeKind::Text:
            return !s.text.empty();
        case ShapeKind::MLeader:
            return true;  // a bare leader is still a valid pointer
    }
    return false;
}

bool Document::add(Shape s) {
    if (!isMeaningful(s)) return false;
    history_.execute(std::make_unique<AddShape>(shapes_, std::move(s)));
    return true;
}

void Document::clear() {
    shapes_.clear();
    history_.clear();
}

}  // namespace bs
