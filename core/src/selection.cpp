#include "boringshot/selection.h"

#include <algorithm>
#include <cmath>

namespace bs {

Rect normalizedRect(Vec2 a, Vec2 b) {
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y)};
}

bool contains(const Rect& r, Vec2 p) {
    return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}

Rect intersect(const Rect& a, const Rect& b) {
    Rect r{std::max(a.left, b.left), std::max(a.top, b.top), std::min(a.right, b.right),
           std::min(a.bottom, b.bottom)};
    if (r.right < r.left) r.right = r.left;
    if (r.bottom < r.top) r.bottom = r.top;
    return r;
}

bool isEmpty(const Rect& r) { return r.width() <= 0.0f || r.height() <= 0.0f; }

bool isClick(Vec2 down, Vec2 up, float threshold) {
    return std::fabs(up.x - down.x) <= threshold && std::fabs(up.y - down.y) <= threshold;
}

std::optional<Rect> pickWindowRect(const std::vector<Rect>& zOrder, Vec2 p) {
    for (const auto& r : zOrder)
        if (!isEmpty(r) && contains(r, p)) return r;
    return std::nullopt;
}

Handle hitTestHandle(const Rect& r, Vec2 p, float tol) {
    if (isEmpty(r)) return Handle::None;
    const bool nearL = std::fabs(p.x - r.left) <= tol;
    const bool nearR = std::fabs(p.x - r.right) <= tol;
    const bool nearT = std::fabs(p.y - r.top) <= tol;
    const bool nearB = std::fabs(p.y - r.bottom) <= tol;
    const bool inX = p.x >= r.left - tol && p.x <= r.right + tol;
    const bool inY = p.y >= r.top - tol && p.y <= r.bottom + tol;
    if (nearT && nearL) return Handle::NW;
    if (nearT && nearR) return Handle::NE;
    if (nearB && nearL) return Handle::SW;
    if (nearB && nearR) return Handle::SE;
    if (nearT && inX) return Handle::N;
    if (nearB && inX) return Handle::S;
    if (nearL && inY) return Handle::W;
    if (nearR && inY) return Handle::E;
    if (contains(r, p)) return Handle::Move;
    return Handle::None;
}

Rect dragHandle(const Rect& start, Handle h, Vec2 d, const Rect& bounds) {
    if (h == Handle::None) return start;
    if (h == Handle::Move) {
        const float w = std::min(start.width(), bounds.width());
        const float ht = std::min(start.height(), bounds.height());
        const float x = std::clamp(start.left + d.x, bounds.left, bounds.right - w);
        const float y = std::clamp(start.top + d.y, bounds.top, bounds.bottom - ht);
        return {x, y, x + w, y + ht};
    }
    Rect r = start;
    if (h == Handle::W || h == Handle::NW || h == Handle::SW) r.left += d.x;
    if (h == Handle::E || h == Handle::NE || h == Handle::SE) r.right += d.x;
    if (h == Handle::N || h == Handle::NW || h == Handle::NE) r.top += d.y;
    if (h == Handle::S || h == Handle::SW || h == Handle::SE) r.bottom += d.y;
    return intersect(normalizedRect({r.left, r.top}, {r.right, r.bottom}), bounds);
}

Rect placeToolbar(const Rect& sel, float w, float h, const Rect& screen, float gap) {
    const float x = std::clamp(sel.right - w, screen.left, std::max(screen.left, screen.right - w));
    float y;
    if (sel.bottom + gap + h <= screen.bottom) {
        y = sel.bottom + gap;
    } else if (sel.top - gap - h >= screen.top) {
        y = sel.top - gap - h;
    } else {
        y = sel.bottom - gap - h;
    }
    y = std::clamp(y, screen.top, std::max(screen.top, screen.bottom - h));
    return {x, y, x + w, y + h};
}

Rect placePopup(const Rect& a, float w, float h, const Rect& screen, float gap) {
    const float x = std::clamp(a.left, screen.left, std::max(screen.left, screen.right - w));
    float y = a.bottom + gap;
    if (y + h > screen.bottom && a.top - gap - h >= screen.top) y = a.top - gap - h;
    y = std::clamp(y, screen.top, std::max(screen.top, screen.bottom - h));
    return {x, y, x + w, y + h};
}

Rect placeNearCursor(Vec2 c, float w, float h, const Rect& screen, float off) {
    float x = c.x + off;
    float y = c.y + off;
    if (x + w > screen.right) x = c.x - off - w;
    if (y + h > screen.bottom) y = c.y - off - h;
    x = std::max(x, screen.left);
    y = std::max(y, screen.top);
    return {x, y, x + w, y + h};
}

}  // namespace bs
