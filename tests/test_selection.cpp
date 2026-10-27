#include "boringshot/selection.h"
#include "bstest.h"

using namespace bs;

TEST(selection_normalize_and_intersect) {
    CHECK((normalizedRect({10, 20}, {0, 5}) == Rect{0, 5, 10, 20}));
    CHECK((intersect({0, 0, 10, 10}, {5, 5, 20, 20}) == Rect{5, 5, 10, 10}));
    CHECK(isEmpty(intersect({0, 0, 10, 10}, {20, 20, 30, 30})));
    CHECK(contains({0, 0, 10, 10}, {0, 0}));
    CHECK(!contains({0, 0, 10, 10}, {10, 5}));  // right edge exclusive
}

TEST(selection_click_vs_drag) {
    CHECK(isClick({100, 100}, {102, 99}));
    CHECK(!isClick({100, 100}, {110, 100}));
}

TEST(selection_pick_topmost_window_then_monitor) {
    const std::vector<Rect> z{{100, 100, 300, 300}, {0, 0, 500, 500}, {0, 0, 1920, 1080}};
    CHECK((*pickWindowRect(z, {150, 150}) == z[0]));
    CHECK((*pickWindowRect(z, {400, 50}) == z[1]));
    CHECK((*pickWindowRect(z, {1000, 900}) == z[2]));
    CHECK(!pickWindowRect(z, {-5, -5}).has_value());
    CHECK(!pickWindowRect({{0, 0, 0, 0}}, {0, 0}).has_value());  // empty rects skipped
}

TEST(selection_handle_hit_test) {
    const Rect r{100, 100, 300, 200};
    CHECK(hitTestHandle(r, {101, 99}, 6) == Handle::NW);
    CHECK(hitTestHandle(r, {299, 203}, 6) == Handle::SE);
    CHECK(hitTestHandle(r, {200, 100}, 6) == Handle::N);
    CHECK(hitTestHandle(r, {304, 150}, 6) == Handle::E);
    CHECK(hitTestHandle(r, {200, 150}, 6) == Handle::Move);
    CHECK(hitTestHandle(r, {50, 50}, 6) == Handle::None);
    CHECK(hitTestHandle({0, 0, 0, 0}, {0, 0}, 6) == Handle::None);
}

TEST(selection_drag_move_clamps_to_bounds) {
    const Rect bounds{0, 0, 1000, 800};
    const Rect r{100, 100, 300, 200};
    CHECK((dragHandle(r, Handle::Move, {50, 20}, bounds) == Rect{150, 120, 350, 220}));
    CHECK((dragHandle(r, Handle::Move, {-500, 900}, bounds) == Rect{0, 700, 200, 800}));
    CHECK((dragHandle(r, Handle::None, {5, 5}, bounds) == r));
}

TEST(selection_drag_resize_crosses_over_and_clips) {
    const Rect bounds{0, 0, 1000, 800};
    const Rect r{100, 100, 300, 200};
    CHECK((dragHandle(r, Handle::SE, {10, 20}, bounds) == Rect{100, 100, 310, 220}));
    CHECK((dragHandle(r, Handle::E, {-250, 0}, bounds) == Rect{50, 100, 100, 200}));  // flipped
    CHECK((dragHandle(r, Handle::NW, {-500, -500}, bounds) == Rect{0, 0, 300, 200}));
    CHECK((dragHandle(r, Handle::N, {999, 999}, bounds).top == 200));  // stays inside bounds
}

TEST(selection_toolbar_placement) {
    const Rect screen{0, 0, 1920, 1080};
    // Below, right-aligned.
    CHECK((placeToolbar({100, 100, 900, 500}, 400, 40, screen, 8) == Rect{500, 508, 900, 548}));
    // No room below -> above.
    CHECK((placeToolbar({100, 500, 900, 1070}, 400, 40, screen, 8).top == 452));
    // Full-screen selection -> inside bottom edge.
    const Rect full = placeToolbar(screen, 400, 40, screen, 8);
    CHECK(full.bottom == 1072);
    // Narrow selection near left edge -> clamped to screen.
    CHECK(placeToolbar({0, 0, 50, 50}, 400, 40, screen, 8).left == 0);
}

TEST(selection_magnifier_flips_at_edges) {
    const Rect screen{0, 0, 1920, 1080};
    CHECK((placeNearCursor({100, 100}, 120, 150, screen, 20) == Rect{120, 120, 240, 270}));
    const Rect br = placeNearCursor({1900, 1060}, 120, 150, screen, 20);
    CHECK(br.right <= 1900 - 20 + 0.01f);
    CHECK(br.bottom <= 1060 - 20 + 0.01f);
}
