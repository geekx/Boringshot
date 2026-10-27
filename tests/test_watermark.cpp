#include <algorithm>

#include "boringshot/watermark.h"
#include "bstest.h"

using namespace bs;

namespace {
WatermarkContext ctx() {
    WatermarkContext c;
    c.user = L"alice";
    c.now = {2026, 3, 7, 9, 5};
    return c;
}
}  // namespace

TEST(watermark_expands_variables) {
    CHECK(expandWatermarkText(L"{user} {date} {time}", ctx()) == L"alice 2026-03-07 09:05");
    CHECK(expandWatermarkText(L"评审稿·{user}", ctx()) == L"评审稿·alice");
}

TEST(watermark_escapes_and_unknowns) {
    CHECK(expandWatermarkText(L"{{user}}", ctx()) == L"{user}");
    CHECK(expandWatermarkText(L"{nope} {user", ctx()) == L"{nope} {user");
    CHECK(expandWatermarkText(L"", ctx()).empty());
    CHECK(expandWatermarkText(L"}", ctx()) == L"}");
}

TEST(watermark_clock_detection) {
    CHECK(watermarkNeedsClock(L"{time}"));
    CHECK(watermarkNeedsClock(L"x {date}"));
    CHECK(!watermarkNeedsClock(L"{user} CONFIDENTIAL"));
    CHECK(!watermarkNeedsClock(L"{{time}}"));
}

TEST(watermark_tiles_cover_area_with_overscan) {
    const Rect area{0, 0, 1000, 500};
    const auto t = watermarkTileOrigins(area, 300, 200);
    CHECK(!t.empty());
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    for (const auto& p : t) {
        minX = std::min(minX, p.x);
        minY = std::min(minY, p.y);
        maxX = std::max(maxX, p.x);
        maxY = std::max(maxY, p.y);
    }
    CHECK(minX <= area.left);
    CHECK(minY <= area.top);
    CHECK(maxX >= area.right);
    CHECK(maxY >= area.bottom);
    CHECK_NEAR(t[0].x, -300, 1e-4);  // row 0 unstaggered
}

TEST(watermark_tiles_stagger_and_guards) {
    const Rect area{0, 0, 600, 400};
    const auto t = watermarkTileOrigins(area, 200, 100);
    const std::size_t cols = 3 + 2;
    CHECK_NEAR(t[cols].x - t[0].x, 100, 1e-4);  // row 1 shifted half a column
    CHECK(watermarkTileOrigins({0, 0, 0, 100}, 10, 10).empty());
    CHECK(watermarkTileOrigins(area, 0, -5, 50).size() == 50);  // clamped + capped
}
