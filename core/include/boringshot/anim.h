#pragma once

#include <vector>

namespace bs {

// CSS-style cubic-bezier(x1, y1, x2, y2) easing: progress for time fraction t.
float cubicBezier(float x1, float y1, float x2, float y2, float t);

// Summon ripple (doc §7.2): concentric rings, each delayed, eased outwards
// while fading from startOpacity to 0.
struct RippleParams {
    float durationMs = 320.0f;
    float delayMs = 40.0f;
    float startOpacity = 0.6f;
    int rings = 3;
};

struct RippleRing {
    float radius = 0.0f;
    float opacity = 0.0f;
};

std::vector<RippleRing> rippleFrame(float elapsedMs, float maxRadius, const RippleParams& p = {});
float rippleTotalMs(const RippleParams& p = {});

// Under-damped spring kick (Win10 AI-key jolt): starts at 0, overshoots, settles.
float shakeOffset(float elapsedMs, float amplitude = 3.0f, float damping = 0.55f, float periodMs = 50.0f);

}  // namespace bs
