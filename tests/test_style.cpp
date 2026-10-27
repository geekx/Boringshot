#include <string>
#include <vector>

#include "boringshot/annotation.h"
#include "bstest.h"

using namespace bs;

TEST(style_kind_defaults) {
    CHECK(makeShape(ShapeKind::Arrow).head == EndShape::Triangle);
    CHECK(makeShape(ShapeKind::MLeader).head == EndShape::Triangle);
    CHECK(makeShape(ShapeKind::Line).head == EndShape::None);
    CHECK(makeShape(ShapeKind::MLeader).tail == EndShape::None);
    CHECK(makeShape(ShapeKind::Text).textStyle.bold);
}

TEST(style_fonts_clamped) {
    CHECK(std::wstring(fontFamily(0)) == L"Microsoft YaHei UI");
    CHECK(std::wstring(fontFamily(2)) == L"KaiTi");
    CHECK(std::wstring(fontFamily(-3)) == fontFamily(0));
    CHECK(std::wstring(fontFamily(99)) == fontFamily(kFontCount - 1));
    CHECK(std::wstring(fontLabel(1)) == L"宋体");
}

TEST(list_numbered_and_bullets) {
    CHECK(formatList(L"a\nb\nc", ListStyle::Numbered) == L"1. a\n2. b\n3. c");
    CHECK(formatList(L"a\n\nb", ListStyle::Numbered) == L"1. a\n\n2. b");  // blank lines not counted
    CHECK(formatList(L"a\nb", ListStyle::Bullet) == L"• a\n• b");
    CHECK(formatList(L"plain", ListStyle::None) == L"plain");
    CHECK(formatList(L"", ListStyle::Numbered).empty());
    std::wstring ten;
    for (int i = 0; i < 10; ++i) ten += (i ? L"\nx" : L"x");
    const std::wstring out = formatList(ten, ListStyle::Numbered);
    CHECK(out.rfind(L"10. x") == out.size() - 5);
}

TEST(list_star_markers_become_bullets) {
    CHECK(formatList(L"* one\n* two", ListStyle::None) == L"• one\n• two");
    CHECK(formatList(L"＊全角", ListStyle::None) == L"• 全角");
    CHECK(formatList(L"* x", ListStyle::Bullet) == L"• x");  // no double marker
    CHECK(formatList(L"*bold*", ListStyle::None) == L"*bold*");    // needs the space
    Shape s = makeShape(ShapeKind::Text);
    s.text = L"a\nb";
    s.textStyle.list = ListStyle::Numbered;
    CHECK(displayText(s) == L"1. a\n2. b");
}

TEST(endcap_shapes) {
    const Vec2 from{0, 0}, tip{100, 0};
    const auto tri = makeEndCap(EndShape::Triangle, from, tip, 2);
    CHECK(tri.filled && tri.closed && tri.outline.size() == 3);
    CHECK((tri.outline[0] == tip));
    CHECK(tri.shaftEnd.x < tip.x);  // shaft stops at the base
    const auto open = makeEndCap(EndShape::OpenArrow, from, tip, 2);
    CHECK(!open.filled && open.outline.size() == 3 && (open.outline[1] == tip) && (open.shaftEnd == tip));
    const auto dot = makeEndCap(EndShape::Dot, from, tip, 2);
    CHECK(dot.filled && (dot.center == tip) && dot.radius > 0);
    const auto tick = makeEndCap(EndShape::Tick, from, tip, 2);
    CHECK(tick.outline.size() == 2);
    CHECK_NEAR(inclinationDeg(tick.outline[0], tick.outline[1]), 45, 1e-3);
    const auto sq = makeEndCap(EndShape::Square, from, tip, 2);
    CHECK(sq.filled && sq.outline.size() == 4);
    CHECK(makeEndCap(EndShape::None, from, tip, 2).outline.empty());
    CHECK(makeEndCap(EndShape::Triangle, tip, tip, 2).shape == EndShape::None);  // degenerate
}

TEST(inclination_is_acute) {
    CHECK_NEAR(inclinationDeg({0, 0}, {10, 0}), 0, 1e-4);
    CHECK_NEAR(inclinationDeg({0, 0}, {10, -10}), 45, 1e-3);
    CHECK_NEAR(inclinationDeg({0, 0}, {-10, 10}), 45, 1e-3);
    CHECK_NEAR(inclinationDeg({0, 0}, {0, 7}), 90, 1e-3);
    CHECK_NEAR(inclinationDeg({3, 3}, {3, 3}), 0, 1e-6);
}

TEST(leader_layout_uses_measured_label) {
    Shape s = makeShape(ShapeKind::MLeader);
    s.leader = {{0, 100}, {50, 50}, {60, 50}, 0};
    s.text = L"label";
    s.textSize = {120, 24};
    const auto l = leaderLayout(s);
    CHECK_NEAR(l.textRect.width(), 120, 1e-4);
    CHECK_NEAR(l.textRect.height(), 24, 1e-4);
    s.text.clear();
    CHECK_NEAR(leaderLayout(s).textRect.width(), 0, 1e-4);
}

TEST(document_replace_and_remove_undo) {
    Document d;
    Shape a = makeShape(ShapeKind::Rect);
    a.b = {10, 10};
    Shape b = a;
    b.kind = ShapeKind::Ellipse;
    d.add(a);
    d.add(b);
    Shape moved = a;
    moved.b = {50, 50};
    CHECK(d.replace(0, moved));
    CHECK((d.shapes()[0].b == Vec2{50, 50}));
    CHECK(d.remove(1));
    CHECK(d.shapes().size() == 1);
    CHECK(!d.remove(5));
    CHECK(!d.replace(9, a));
    Shape tiny = a;
    tiny.b = a.a;
    CHECK(!d.replace(0, tiny));  // would make it meaningless

    CHECK(d.undo());  // un-remove
    CHECK(d.shapes().size() == 2 && d.shapes()[1].kind == ShapeKind::Ellipse);
    CHECK(d.undo());  // un-replace
    CHECK((d.shapes()[0].b == Vec2{10, 10}));
    CHECK(d.redo() && d.redo());
    CHECK(d.shapes().size() == 1 && (d.shapes()[0].b == Vec2{50, 50}));
}

TEST(mosaic_is_box_shaped) {
    Shape m = makeShape(ShapeKind::Mosaic);
    m.b = {1, 1};
    CHECK(!isMeaningful(m));
    m.b = {20, 30};
    CHECK(isMeaningful(m));
    CHECK((constrainDrag(ShapeKind::Mosaic, {0, 0}, {20, 5}, true) == Vec2{20, 20}));
}

TEST(style_option_names_unique) {
    std::vector<std::wstring> names;
    for (int i = 0; i < kLineFinishCount; ++i) names.push_back(finishName(static_cast<LineFinish>(i)));
    for (int i = 0; i < kEndShapeCount; ++i) names.push_back(endShapeName(static_cast<EndShape>(i)));
    for (int i = 0; i < kListStyleCount; ++i) names.push_back(listStyleName(static_cast<ListStyle>(i)));
    names.push_back(mosaicModeName(MosaicMode::Pixelate));
    names.push_back(mosaicModeName(MosaicMode::Blur));
    for (std::size_t i = 0; i < names.size(); ++i) {
        CHECK(!names[i].empty() && names[i] != L"?");
        for (std::size_t j = i + 1; j < names.size(); ++j) CHECK(names[i] != names[j]);
    }
}

TEST(style_frosted_dot_is_a_lens) {
    const auto solid = makeEndCap(EndShape::Dot, {0, 0}, {50, 0}, 3);
    const auto glass = makeEndCap(EndShape::Dot, {0, 0}, {50, 0}, 3, Material::Frosted);
    CHECK(glass.radius > solid.radius * 1.8f);
    CHECK((glass.center == Vec2{50, 0}));
    CHECK(makeShape(ShapeKind::MLeader).material == Material::Solid);
}

TEST(text_keys_enter_breaks_line) {
    CHECK(classifyTextChar(L'\r', false) == TextKey::Newline);   // plain Enter
    CHECK(classifyTextChar(L'\n', true) == TextKey::Commit);     // Ctrl+Enter → LF
    CHECK(classifyTextChar(L'\r', true) == TextKey::Commit);
    CHECK(classifyTextChar(L'\b', false) == TextKey::Backspace);
    CHECK(classifyTextChar(26, true) == TextKey::Ignore);        // Ctrl+Z
    CHECK(classifyTextChar(L'中', false) == TextKey::Insert);
    CHECK(classifyTextChar(L' ', false) == TextKey::Insert);
}
