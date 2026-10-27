#pragma once

#include <cmath>

namespace bs {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    constexpr Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(float s) const { return {x * s, y * s}; }
    constexpr bool operator==(const Vec2&) const = default;
};

inline float length(Vec2 v) { return std::hypot(v.x, v.y); }

// Unit vector, or {0,0} for a (near) zero-length input.
inline Vec2 normalize(Vec2 v) {
    const float len = length(v);
    return len > 1e-6f ? Vec2{v.x / len, v.y / len} : Vec2{};
}

// Screen space: y grows downward, so top < bottom.
struct Rect {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;

    constexpr float width() const { return right - left; }
    constexpr float height() const { return bottom - top; }
    constexpr bool operator==(const Rect&) const = default;
};

inline constexpr float kPi = 3.14159265358979323846f;

}  // namespace bs
