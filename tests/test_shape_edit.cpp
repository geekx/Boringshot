#include "boringshot/shape_edit.h"
#include "bstest.h"

using namespace bs;

namespace {
Shape rect(Vec2 a, Vec2 b) {
    Shape s = makeShape(ShapeKind::Rect);
    s.a = a;
    s.b = b;
    return s;
}
Shape leader() {
    Shape s = makeShape(ShapeKind::MLeader);
    s.leader = {{0, 100}, {50, 50}, {200, 50}, 0};
    s.text = L"x";
    s.textSize = {40, 20};
    return s;
}
}  // namespace

TEST(edit_segment_distance) {
    CHECK_NEAR(distanceToSegment({5, 5}, {0, 0}, {10, 0}), 5, 1e-5);
    CHECK_NEAR(distanceToSegment({-3, 4}, {0, 0}, {10, 0}), 5, 1e-5);  // clamps to endpoint
    CHECK_NEAR(distanceToSegment({3, 4}, {0, 0}, {0, 0}), 5, 1e-5);
}

TEST(edit_control_points_per_kind) {
    CHECK(controlPoints(rect({0, 0}, {10, 20})).size() == 4);
    Shape l = makeShape(ShapeKind::Line);
    l.a = {1, 2};
    l.b = {3, 4};
    CHECK(controlPoints(l).size() == 2);
    const auto lp = controlPoints(leader());
    CHECK(lp.size() == 3);
    CHECK((lp[1] == Vec2{50, 50}));
    CHECK_NEAR(lp[2].y, 50, 1e-4);
    CHECK(controlPoints(makeShape(ShapeKind::Pen)).empty());
    CHECK(controlPoints(makeShape(ShapeKind::Text)).empty());
}

TEST(edit_rect_corner_drag_and_square) {
    const Shape r = rect({0, 0}, {10, 20});
    const Shape m = moveControlPoint(r, 1, {30, -5}, false);  // top-right
    CHECK((m.a == Vec2{0, -5}) && (m.b == Vec2{30, 20}));
    const Shape sq = moveControlPoint(r, 2, {40, 10}, true);  // square against corner 0
    CHECK_NEAR(sq.b.x - sq.a.x, sq.b.y - sq.a.y, 1e-4);
}

TEST(edit_line_endpoint_snaps_45) {
    Shape l = makeShape(ShapeKind::Arrow);
    l.a = {0, 0};
    l.b = {100, 0};
    const Shape m = moveControlPoint(l, 1, {100, -90}, true);
    CHECK_NEAR(inclinationDeg(m.a, m.b), 45, 1e-3);
    CHECK((m.a == Vec2{0, 0}));
}

TEST(edit_leader_joints) {
    const Shape s = leader();
    const Shape tip = moveControlPoint(s, 0, {10, 120}, false);
    CHECK((tip.leader.arrowTip == Vec2{10, 120}) && (tip.leader.kink == s.leader.kink));
    // Kink drag carries the landing so its length/side are kept.
    const Shape k = moveControlPoint(s, 1, {60, 40}, false);
    CHECK((k.leader.kink == Vec2{60, 40}));
    CHECK((k.leader.textAnchor == Vec2{210, 40}));
    // Shift snaps the leader against the tip.
    const Shape ks = moveControlPoint(s, 1, {52, 60}, true);
    CHECK_NEAR(inclinationDeg(ks.leader.arrowTip, ks.leader.kink), 45, 1e-3);
    // Landing end only moves horizontally and can flip the side.
    const Shape e = moveControlPoint(s, 2, {-100, 999}, false);
    CHECK_NEAR(e.leader.textAnchor.y, 50, 1e-4);
    CHECK(leaderLayout(e).align == TextAlign::Right);
    CHECK((moveControlPoint(s, 7, {0, 0}, false).leader.kink == s.leader.kink));  // bad index: no-op
}

TEST(edit_translate_moves_everything) {
    Shape p = makeShape(ShapeKind::Pen);
    p.points = {{0, 0}, {5, 5}};
    const Shape t = translate(p, {10, -1});
    CHECK((t.points[1] == Vec2{15, 4}));
    const Shape l = translate(leader(), {1, 1});
    CHECK((l.leader.arrowTip == Vec2{1, 101}) && (l.leader.textAnchor == Vec2{201, 51}));
}

TEST(edit_hit_body_per_kind) {
    const Shape r = rect({0, 0}, {100, 50});
    CHECK(hitShapeBody(r, {0, 25}, 3));
    CHECK(!hitShapeBody(r, {50, 25}, 3));  // inside a rect outline is free for drawing
    Shape e = makeShape(ShapeKind::Ellipse);
    e.a = {0, 0};
    e.b = {100, 100};
    CHECK(hitShapeBody(e, {100, 50}, 3));
    CHECK(!hitShapeBody(e, {50, 50}, 3));
    Shape m = makeShape(ShapeKind::Mosaic);
    m.b = {40, 40};
    CHECK(hitShapeBody(m, {20, 20}, 0));
    Shape t = makeShape(ShapeKind::Text);
    t.a = {10, 10};
    t.textSize = {50, 20};
    CHECK(hitShapeBody(t, {30, 20}, 2));
    CHECK(!hitShapeBody(t, {90, 20}, 2));
    const Shape l = leader();
    CHECK(hitShapeBody(l, {25, 75}, 2));   // on the leader
    CHECK(hitShapeBody(l, {150, 50}, 2));  // on the landing
    CHECK(!hitShapeBody(l, {150, 150}, 2));
}

TEST(edit_hit_priority_handles_then_topmost) {
    std::vector<Shape> shapes{rect({0, 0}, {100, 100}), rect({0, 0}, {100, 100})};
    const auto top = hitTestShapes(shapes, {0, 50}, 3, std::nullopt);
    CHECK(top && top->index == 1 && top->handle == -1);
    const auto h = hitTestShapes(shapes, {99, 1}, 3, std::size_t{0});
    CHECK(h && h->index == 0 && h->handle == 1);  // selected shape's joint wins
    CHECK(!hitTestShapes(shapes, {50, 50}, 3, std::nullopt));
}

TEST(edit_bounds_cover_label) {
    const Rect b = shapeBounds(leader());
    CHECK(b.left <= 0 && b.bottom >= 100);
    CHECK(b.top <= 50 - 20);  // label sits above the landing
}
