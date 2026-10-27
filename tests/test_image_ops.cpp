#include <vector>

#include "boringshot/image_ops.h"
#include "bstest.h"

using namespace bs;

namespace {
struct Img {
    int w, h;
    std::vector<std::uint32_t> px;
    PixelView view() const { return {px.data(), w, h}; }
};
Img checker(int w, int h) {
    Img i{w, h, {}};
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) i.px.push_back(((x + y) % 2) ? 0xFFFFFFFFu : 0xFF000000u);
    return i;
}
}  // namespace

TEST(pixelate_averages_blocks) {
    const Img img = checker(8, 8);
    const PixelBlock b = pixelate(img.view(), 0, 0, 8, 8, 2);
    CHECK(b.width == 8 && b.height == 8);
    for (auto p : b.px) CHECK(p == 0xFF7F7F7Fu);  // (255+0)/2 = 127 per channel
}

TEST(pixelate_clips_and_guards) {
    const Img img = checker(4, 4);
    const PixelBlock b = pixelate(img.view(), -2, -2, 4, 4, 3);
    CHECK(b.x == 0 && b.y == 0 && b.width == 2 && b.height == 2);
    CHECK(pixelate(img.view(), 10, 10, 5, 5, 2).empty());
    CHECK(pixelate({}, 0, 0, 5, 5, 2).empty());
    const PixelBlock same = pixelate(img.view(), 0, 0, 4, 4, 0);  // block clamped to 1 → copy
    CHECK(same.px == img.px);
}

TEST(blur_keeps_flat_colour_and_mean) {
    Img flat{6, 6, std::vector<std::uint32_t>(36, 0xFF336699u)};
    const PixelBlock b = boxBlur(flat.view(), 1, 1, 4, 4, 2);
    CHECK(b.width == 4 && b.px.size() == 16);
    for (auto p : b.px) CHECK(p == 0xFF336699u);
    const Img c = checker(16, 16);
    const PixelBlock cb = boxBlur(c.view(), 4, 4, 8, 8, 2, 3);
    for (auto p : cb.px) {
        const auto v = p & 0xFF;
        CHECK(v > 90 && v < 165);  // checkerboard smoothed toward grey
    }
}

TEST(blur_zero_radius_is_copy_and_alpha_opaque) {
    Img img{2, 1, {0x00112233u, 0x00445566u}};
    const PixelBlock b = boxBlur(img.view(), 0, 0, 2, 1, 0);
    CHECK(b.px[0] == 0xFF112233u && b.px[1] == 0xFF445566u);
}
