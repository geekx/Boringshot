#include "boringshot/mleader.h"
#include "bstest.h"

using namespace bs;

TEST(snap_rounds_to_45_and_keeps_length) {
    const Vec2 tip{0, 0};
    const Vec2 k = snapKink(tip, {100, 10}, 45.0f);  // ~5.7deg -> 0deg
    CHECK_NEAR(k.x, length({100, 10}), 1e-3);
    CHECK_NEAR(k.y, 0, 1e-3);

    const Vec2 d = snapKink(tip, {60, -50}, 45.0f);  // ~-40deg -> -45deg
    CHECK_NEAR(d.x, -d.y, 1e-3);
    CHECK_NEAR(length(d), length({60, -50}), 1e-3);
}

TEST(snap_disabled_or_degenerate_is_identity) {
    CHECK((snapKink({1, 1}, {7, 3}, 0.0f) == Vec2{7, 3}));
    CHECK((snapKink({5, 5}, {5, 5}, 45.0f) == Vec2{5, 5}));
}

TEST(layout_text_right_of_kink_is_left_aligned) {
    MLeaderGeometry g;
    const auto l = layoutMLeader({{0, 100}, {50, 50}, {300, 60}, 80.0f}, g);
    CHECK(l.align == TextAlign::Left);
    CHECK((l.landingStart == Vec2{50, 50}));
    CHECK_NEAR(l.landingEnd.x, 300, 1e-4);
    CHECK_NEAR(l.landingEnd.y, 50, 1e-4);  // horizontal, at kink.y
    CHECK_NEAR(l.textRect.left, 50 + g.landingGap, 1e-4);
    CHECK_NEAR(l.textRect.width(), 80, 1e-4);
    CHECK_NEAR(l.textRect.bottom, 50 - g.landingGap, 1e-4);  // above landing
    CHECK_NEAR(l.textRect.height(), g.textHeight, 1e-4);
}

TEST(layout_text_left_of_kink_is_right_aligned) {
    MLeaderGeometry g;
    const auto l = layoutMLeader({{200, 200}, {150, 150}, {0, 140}, 60.0f}, g);
    CHECK(l.align == TextAlign::Right);
    CHECK_NEAR(l.landingEnd.x, 0, 1e-4);
    CHECK_NEAR(l.textRect.right, 150 - g.landingGap, 1e-4);
    CHECK_NEAR(l.textRect.left, 150 - g.landingGap - 60, 1e-4);
}

TEST(landing_extends_to_carry_long_text) {
    MLeaderGeometry g;
    // Anchor only 10px right of kink, text 200px wide.
    const auto r = layoutMLeader({{0, 0}, {100, 100}, {110, 100}, 200.0f}, g);
    CHECK(r.landingEnd.x >= r.textRect.right + g.landingGap - 1e-4f);
    const auto l = layoutMLeader({{0, 0}, {500, 100}, {490, 100}, 200.0f}, g);
    CHECK(l.align == TextAlign::Right);
    CHECK(l.landingEnd.x <= l.textRect.left - g.landingGap + 1e-4f);
}

TEST(empty_text_uses_min_landing) {
    MLeaderGeometry g;
    const auto l = layoutMLeader({{0, 0}, {10, 10}, {10, 10}, 0.0f}, g);
    CHECK_NEAR(l.landingEnd.x - l.landingStart.x, g.minLandingLength, 1e-4);
    CHECK_NEAR(l.textRect.width(), 0, 1e-4);
    const auto neg = layoutMLeader({{0, 0}, {10, 10}, {10, 10}, -5.0f}, g);
    CHECK_NEAR(neg.textRect.width(), 0, 1e-4);
}

TEST(arrowhead_points_at_tip) {
    MLeaderGeometry g;
    const auto l = layoutMLeader({{0, 0}, {100, 0}, {200, 0}, 10.0f}, g);
    CHECK(l.arrowHead.has_value());
    const auto& t = *l.arrowHead;
    CHECK((t[0] == Vec2{0, 0}));
    CHECK_NEAR(t[1].x, g.arrowSize, 1e-4);
    CHECK_NEAR(t[2].x, g.arrowSize, 1e-4);
    CHECK_NEAR(std::fabs(t[1].y - t[2].y), 2 * g.arrowHalfWidth, 1e-4);
}

TEST(arrowhead_clamped_to_short_leader_and_absent_when_degenerate) {
    MLeaderGeometry g;
    const auto s = layoutMLeader({{0, 0}, {5, 0}, {50, 0}, 0.0f}, g);
    CHECK(s.arrowHead.has_value());
    CHECK_NEAR((*s.arrowHead)[1].x, 5, 1e-4);  // h clamped to leader length
    const auto d = layoutMLeader({{3, 3}, {3, 3}, {50, 3}, 0.0f}, g);
    CHECK(!d.arrowHead.has_value());
}

TEST(tool_walks_three_phases) {
    MLeaderTool tool;
    CHECK(!tool.layout().has_value());
    tool.pointerMove({1, 1});
    CHECK(tool.phase() == MLeaderTool::Phase::ArrowTip);

    tool.click({0, 0});
    CHECK(tool.phase() == MLeaderTool::Phase::Kink);
    tool.pointerMove({100, -95});  // snaps to -45deg
    auto l = tool.layout();
    CHECK(l.has_value());
    CHECK_NEAR(l->kink.x, -l->kink.y, 1e-3);

    tool.click({100, -95});
    CHECK(tool.phase() == MLeaderTool::Phase::TextAnchor);
    tool.pointerMove({-200, 0});
    CHECK(tool.layout()->align == TextAlign::Right);
    tool.click({300, 0});
    CHECK(tool.phase() == MLeaderTool::Phase::Editing);
    CHECK(tool.layout()->align == TextAlign::Left);

    tool.click({0, 0});  // ignored while editing
    CHECK(tool.phase() == MLeaderTool::Phase::Editing);
    tool.cancel();
    CHECK(tool.phase() == MLeaderTool::Phase::ArrowTip);
    CHECK(!tool.layout().has_value());
}

TEST(tool_without_snap_keeps_raw_kink) {
    MLeaderTool tool;
    tool.setSnap(false);
    tool.click({0, 0});
    tool.pointerMove({100, -37});
    CHECK((tool.layout()->kink == Vec2{100, -37}));
}
