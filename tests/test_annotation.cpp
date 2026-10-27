#include <cmath>

#include "boringshot/annotation.h"
#include "boringshot/color.h"
#include "boringshot/pin.h"
#include "bstest.h"

using namespace bs;

TEST(color_formats) {
    const Rgb8 c = fromBgra(0xFF1A2B3Cu);
    CHECK((c == Rgb8{0x1A, 0x2B, 0x3C}));
    CHECK(hexString(c) == L"#1A2B3C");
    CHECK(rgbString(c) == L"26, 43, 60");
    CHECK(hexString({255, 255, 255}) == L"#FFFFFF");
}

TEST(annotation_shift_constraints) {
    const Vec2 sq = constrainDrag(ShapeKind::Rect, {0, 0}, {50, -20}, true);
    CHECK((sq == Vec2{50, -50}));
    const Vec2 ln = constrainDrag(ShapeKind::Arrow, {0, 0}, {100, 8}, true);
    CHECK_NEAR(ln.y, 0, 1e-3);
    CHECK((constrainDrag(ShapeKind::Rect, {0, 0}, {50, -20}, false) == Vec2{50, -20}));
    CHECK((constrainDrag(ShapeKind::Pen, {0, 0}, {50, -20}, true) == Vec2{50, -20}));
}

TEST(annotation_pen_decimation) {
    std::vector<Vec2> pts;
    appendPenPoint(pts, {0, 0}, 2);
    appendPenPoint(pts, {1, 0}, 2);  // too close
    appendPenPoint(pts, {3, 0}, 2);
    CHECK(pts.size() == 2);
}

TEST(annotation_meaningful_filter) {
    Shape s;
    s.a = {0, 0};
    s.b = {1, 1};
    CHECK(!isMeaningful(s));  // accidental click with rect tool
    s.b = {10, 10};
    CHECK(isMeaningful(s));
    Shape t;
    t.kind = ShapeKind::Text;
    CHECK(!isMeaningful(t));
    t.text = L"标注";
    CHECK(isMeaningful(t));
}

TEST(document_add_undo_redo) {
    Document d;
    Shape s;
    s.b = {10, 10};
    CHECK(d.add(s));
    s.kind = ShapeKind::Ellipse;
    CHECK(d.add(s));
    Shape tiny;
    CHECK(!d.add(tiny));
    CHECK(d.shapes().size() == 2);

    CHECK(d.undo());
    CHECK(d.shapes().size() == 1);
    CHECK(d.shapes()[0].kind == ShapeKind::Rect);
    CHECK(d.redo());
    CHECK(d.shapes().back().kind == ShapeKind::Ellipse);
    CHECK(d.undo() && d.undo());
    CHECK(d.empty());
    CHECK(!d.undo());
    d.redo();
    d.clear();
    CHECK(d.empty() && !d.canRedo() && !d.canUndo());
}

TEST(arrowhead_helper_for_arrow_shape) {
    const auto t = arrowHeadTriangle({0, 0}, {0, 100}, 10, 4);
    CHECK(t.has_value());
    CHECK_NEAR((*t)[1].y, 90, 1e-4);
    CHECK(!arrowHeadTriangle({5, 5}, {5, 5}, 10, 4).has_value());
}

TEST(pin_zoom_scale_and_anchor) {
    CHECK_NEAR(nextPinScale(1.0f, 120), 1.1, 1e-5);
    CHECK_NEAR(nextPinScale(1.0f, -240), 1.0 / 1.21, 1e-4);
    CHECK(nextPinScale(7.9f, 1200) == kPinMaxScale);
    CHECK(nextPinScale(0.0f, -1200) == kPinMinScale);

    const Rect w{100, 100, 300, 200};
    const Rect z = zoomAroundPoint(w, {200, 150}, 1.0f, 2.0f);
    CHECK((z == Rect{0, 50, 400, 250}));
    CHECK((zoomAroundPoint(w, {200, 150}, 0.0f, 2.0f) == w));
}
