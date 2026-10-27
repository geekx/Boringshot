#include "boringshot/shape_edit.h"

#include <algorithm>
#include <cmath>

#include "boringshot/selection.h"

namespace bs {
namespace {

bool isBoxKind(ShapeKind k) { return k == ShapeKind::Rect || k == ShapeKind::Ellipse || k == ShapeKind::Mosaic; }
bool isSegmentKind(ShapeKind k) { return k == ShapeKind::Line || k == ShapeKind::Arrow; }

Rect inflate(const Rect& r, float d) { return {r.left - d, r.top - d, r.right + d, r.bottom + d}; }

Rect unite(const Rect& r, Vec2 p) {
    return {std::min(r.left, p.x), std::min(r.top, p.y), std::max(r.right, p.x), std::max(r.bottom, p.y)};
}

Rect unite(const Rect& a, const Rect& b) { return unite(unite(a, Vec2{b.left, b.top}), Vec2{b.right, b.bottom}); }

bool inside(const Rect& r, Vec2 p) { return p.x >= r.left && p.x <= r.right && p.y >= r.top && p.y <= r.bottom; }

Vec2 corner(const Shape& s, int i) {
    switch (i & 3) {
        case 0: return s.a;
        case 1: return {s.b.x, s.a.y};
        case 2: return s.b;
        default: return {s.a.x, s.b.y};
    }
}

}  // namespace

float distanceToSegment(Vec2 p, Vec2 a, Vec2 b) {
    const Vec2 ab = b - a;
    const float len2 = ab.x * ab.x + ab.y * ab.y;
    if (len2 < 1e-12f) return length(p - a);
    const float t = std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / len2, 0.0f, 1.0f);
    return length(p - (a + ab * t));
}

std::vector<Vec2> controlPoints(const Shape& s) {
    if (isBoxKind(s.kind)) return {corner(s, 0), corner(s, 1), corner(s, 2), corner(s, 3)};
    if (isSegmentKind(s.kind)) return {s.a, s.b};
    if (s.kind == ShapeKind::MLeader) {
        const auto l = leaderLayout(s);
        return {s.leader.arrowTip, s.leader.kink, l.landingEnd};
    }
    return {};
}

Shape moveControlPoint(const Shape& s, int index, Vec2 p, bool shift) {
    Shape out = s;
    if (isBoxKind(s.kind) && index >= 0 && index < 4) {
        const Vec2 q = shift ? constrainDrag(ShapeKind::Rect, corner(s, index + 2), p, true) : p;
        switch (index) {
            case 0: out.a = q; break;
            case 1: out.b.x = q.x; out.a.y = q.y; break;
            case 2: out.b = q; break;
            default: out.a.x = q.x; out.b.y = q.y; break;
        }
    } else if (isSegmentKind(s.kind) && (index == 0 || index == 1)) {
        Vec2& moved = index == 0 ? out.a : out.b;
        const Vec2 fixed = index == 0 ? s.b : s.a;
        moved = shift ? snapKink(fixed, p, 45.0f) : p;
    } else if (s.kind == ShapeKind::MLeader) {
        MLeaderInput& in = out.leader;
        switch (index) {
            case 0:
                in.arrowTip = shift ? snapKink(in.kink, p, 45.0f) : p;
                break;
            case 1: {
                const Vec2 k = shift ? snapKink(in.arrowTip, p, 45.0f) : p;
                in.textAnchor = in.textAnchor + (k - in.kink);  // landing rides with its kink
                in.kink = k;
                break;
            }
            case 2:
                in.textAnchor = {p.x, in.kink.y};
                break;
            default:
                break;
        }
    }
    return out;
}

Shape translate(const Shape& s, Vec2 d) {
    Shape out = s;
    out.a = s.a + d;
    out.b = s.b + d;
    for (auto& pt : out.points) pt = pt + d;
    out.leader.arrowTip = s.leader.arrowTip + d;
    out.leader.kink = s.leader.kink + d;
    out.leader.textAnchor = s.leader.textAnchor + d;
    return out;
}

Rect shapeBounds(const Shape& s) {
    const float half = s.strokeWidth * 0.5f;
    switch (s.kind) {
        case ShapeKind::Rect:
        case ShapeKind::Ellipse:
        case ShapeKind::Mosaic:
        case ShapeKind::Line:
        case ShapeKind::Arrow:
            return inflate(normalizedRect(s.a, s.b), half);
        case ShapeKind::Pen: {
            if (s.points.empty()) return {};
            Rect r{s.points[0].x, s.points[0].y, s.points[0].x, s.points[0].y};
            for (const auto& p : s.points) r = unite(r, p);
            return inflate(r, half);
        }
        case ShapeKind::Text:
            return {s.a.x, s.a.y, s.a.x + s.textSize.x, s.a.y + s.textSize.y};
        case ShapeKind::MLeader: {
            const auto l = leaderLayout(s);
            Rect r = normalizedRect(l.arrowTip, l.kink);
            r = unite(r, l.landingEnd);
            if (!s.text.empty()) r = unite(r, l.textRect);
            return inflate(r, half);
        }
    }
    return {};
}

bool hitShapeBody(const Shape& s, Vec2 p, float tol) {
    const float t = tol + s.strokeWidth * 0.5f;
    switch (s.kind) {
        case ShapeKind::Rect: {
            const Rect r = normalizedRect(s.a, s.b);
            return inside(inflate(r, t), p) && !inside(inflate(r, -t), p);  // outline only
        }
        case ShapeKind::Ellipse: {
            const Rect r = normalizedRect(s.a, s.b);
            const float rx = r.width() * 0.5f, ry = r.height() * 0.5f;
            if (rx < 1e-3f || ry < 1e-3f) return false;
            const float dx = (p.x - (r.left + rx)) / rx, dy = (p.y - (r.top + ry)) / ry;
            return std::fabs(std::sqrt(dx * dx + dy * dy) - 1.0f) * std::min(rx, ry) <= t;
        }
        case ShapeKind::Mosaic:
            return inside(normalizedRect(s.a, s.b), p);
        case ShapeKind::Line:
        case ShapeKind::Arrow:
            return distanceToSegment(p, s.a, s.b) <= t;
        case ShapeKind::Pen:
            for (std::size_t i = 1; i < s.points.size(); ++i)
                if (distanceToSegment(p, s.points[i - 1], s.points[i]) <= t) return true;
            return false;
        case ShapeKind::Text:
            return inside(inflate(shapeBounds(s), tol), p);
        case ShapeKind::MLeader: {
            const auto l = leaderLayout(s);
            if (distanceToSegment(p, l.arrowTip, l.kink) <= t) return true;
            if (distanceToSegment(p, l.landingStart, l.landingEnd) <= t) return true;
            return !s.text.empty() && inside(inflate(l.textRect, tol), p);
        }
    }
    return false;
}

std::optional<ShapeHit> hitTestShapes(const std::vector<Shape>& shapes, Vec2 p, float tol,
                                      std::optional<std::size_t> selected) {
    if (selected && *selected < shapes.size()) {
        const auto pts = controlPoints(shapes[*selected]);
        for (std::size_t j = 0; j < pts.size(); ++j)
            if (length(p - pts[j]) <= tol + 2.0f) return ShapeHit{*selected, static_cast<int>(j)};
    }
    for (std::size_t i = shapes.size(); i-- > 0;)
        if (hitShapeBody(shapes[i], p, tol)) return ShapeHit{i, -1};
    return std::nullopt;
}

}  // namespace bs
