#include "boringshot/anim.h"

#include <algorithm>
#include <cmath>

namespace bs {
namespace {

float bezier1d(float p1, float p2, float s) {
    const float u = 1.0f - s;
    return 3.0f * u * u * s * p1 + 3.0f * u * s * s * p2 + s * s * s;
}

}  // namespace

float cubicBezier(float x1, float y1, float x2, float y2, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    // Bisection on the monotone x(s); 24 rounds is well under 1e-6.
    float lo = 0.0f, hi = 1.0f, s = t;
    for (int i = 0; i < 24; ++i) {
        s = 0.5f * (lo + hi);
        if (bezier1d(x1, x2, s) < t) lo = s; else hi = s;
    }
    return bezier1d(y1, y2, s);
}

std::vector<RippleRing> rippleFrame(float elapsedMs, float maxRadius, const RippleParams& p) {
    std::vector<RippleRing> rings;
    const int n = std::max(p.rings, 0);
    for (int i = 0; i < n; ++i) {
        const float local = elapsedMs - static_cast<float>(i) * p.delayMs;
        const float t = p.durationMs > 0.0f ? std::clamp(local / p.durationMs, 0.0f, 1.0f) : 1.0f;
        const float e = cubicBezier(0.16f, 1.0f, 0.3f, 1.0f, t);
        RippleRing r;
        r.radius = local <= 0.0f ? 0.0f : e * maxRadius;
        r.opacity = local <= 0.0f ? 0.0f : p.startOpacity * (1.0f - t);
        rings.push_back(r);
    }
    return rings;
}

float rippleTotalMs(const RippleParams& p) {
    return p.durationMs + p.delayMs * static_cast<float>(std::max(p.rings - 1, 0));
}

float shakeOffset(float elapsedMs, float amplitude, float damping, float periodMs) {
    if (elapsedMs <= 0.0f || periodMs <= 0.0f) return 0.0f;
    const float zeta = std::clamp(damping, 0.01f, 0.99f);
    const float w = 2.0f * 3.14159265f / periodMs;  // rad per ms
    const float wd = w * std::sqrt(1.0f - zeta * zeta);
    return amplitude * std::exp(-zeta * w * elapsedMs) * std::sin(wd * elapsedMs);
}

}  // namespace bs
