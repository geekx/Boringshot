#pragma once

#include <cstdint>
#include <vector>

namespace bs {

// Read-only view of tightly packed 32-bit BGRA pixels.
struct PixelView {
    const std::uint32_t* px = nullptr;
    int width = 0;
    int height = 0;
};

// A processed rectangle of pixels placed at (x, y) in source coordinates.
struct PixelBlock {
    int x = 0, y = 0, width = 0, height = 0;
    std::vector<std::uint32_t> px;
    bool empty() const { return width <= 0 || height <= 0; }
};

// Region is clipped to the source; output alpha is always opaque.
// Mosaic: every block x block cell (aligned to the region origin) becomes its average.
PixelBlock pixelate(PixelView src, int x, int y, int w, int h, int block);
// Approximate gaussian: `passes` separable box blurs of `radius`. Samples
// outside the region (but inside the source) so edges blend naturally.
PixelBlock boxBlur(PixelView src, int x, int y, int w, int h, int radius, int passes = 3);

}  // namespace bs
