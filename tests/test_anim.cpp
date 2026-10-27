#include <cmath>

#include "boringshot/anim.h"
#include "bstest.h"

using namespace bs;

TEST(anim_cubic_bezier_endpoints_and_shape) {
    CHECK_NEAR(cubicBezier(0.16f, 1, 0.3f, 1, 0), 0, 1e-4);
    CHECK_NEAR(cubicBezier(0.16f, 1, 0.3f, 1, 1), 1, 1e-4);
    CHECK(cubicBezier(0.16f, 1, 0.3f, 1, 0.3f) > 0.8f);  // strong ease-out
    CHECK_NEAR(cubicBezier(0, 0, 1, 1, 0.37f), 0.37, 1e-3);  // linear
    float prev = -1;
    for (int i = 0; i <= 20; ++i) {
        const float v = cubicBezier(0.16f, 1, 0.3f, 1, static_cast<float>(i) / 20.0f);
        CHECK(v >= prev - 1e-5f);
        prev = v;
    }
}

TEST(anim_ripple_staggered_and_fading) {
    const auto f0 = rippleFrame(0, 200);
    CHECK(f0.size() == 3);
    for (const auto& r : f0) CHECK(r.opacity == 0 && r.radius == 0);
    const auto f = rippleFrame(60, 200);
    CHECK(f[0].radius > f[1].radius);    // ring 1 started 40ms later
    CHECK(f[2].radius == 0);             // ring 2 not yet (starts at 80ms)
    CHECK(f[0].opacity < 0.6f && f[0].opacity > 0);
    const auto end = rippleFrame(rippleTotalMs(), 200);
    for (const auto& r : end) CHECK(r.opacity < 1e-4f);
    CHECK_NEAR(end[0].radius, 200, 1e-2);
    CHECK_NEAR(rippleTotalMs(), 400, 1e-4);
}

TEST(anim_shake_kicks_and_settles) {
    CHECK(shakeOffset(0) == 0);
    float peak = 0;
    for (int t = 1; t < 60; ++t) peak = std::max(peak, std::fabs(shakeOffset(static_cast<float>(t))));
    CHECK(peak > 1.0f && peak <= 3.0f);
    CHECK(std::fabs(shakeOffset(200)) < 0.05f);
    CHECK(shakeOffset(10, 3, 0.55f, 0) == 0);
}
