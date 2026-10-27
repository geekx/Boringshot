#include "boringshot/image_ops.h"

#include <algorithm>
#include <array>

namespace bs {
namespace {

constexpr std::uint32_t kOpaque = 0xFF000000u;

struct Clip {
    int x0, y0, x1, y1;
    bool empty() const { return x1 <= x0 || y1 <= y0; }
};

Clip clip(PixelView s, int x, int y, int w, int h) {
    return {std::max(x, 0), std::max(y, 0), std::min(x + std::max(w, 0), s.width), std::min(y + std::max(h, 0), s.height)};
}

std::size_t at(int x, int y, int width) {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
}

std::uint32_t channel(std::uint32_t p, int c) { return (p >> (8 * c)) & 0xFFu; }

// One horizontal (dx=1) or vertical box pass over buf (w x h), edge-clamped.
void boxPass(std::vector<std::uint32_t>& buf, int w, int h, int r, bool horizontal) {
    std::vector<std::uint32_t> out(buf.size());
    const int len = horizontal ? w : h;
    const int lines = horizontal ? h : w;
    const auto idx = [&](int line, int i) { return horizontal ? at(i, line, w) : at(line, i, w); };
    const std::uint32_t win = static_cast<std::uint32_t>(2 * r + 1);
    for (int line = 0; line < lines; ++line) {
        std::array<std::uint32_t, 3> sum{};
        for (int i = -r; i <= r; ++i) {
            const std::uint32_t p = buf[idx(line, std::clamp(i, 0, len - 1))];
            for (int c = 0; c < 3; ++c) sum[static_cast<std::size_t>(c)] += channel(p, c);
        }
        for (int i = 0; i < len; ++i) {
            out[idx(line, i)] = kOpaque | (sum[2] / win) << 16 | (sum[1] / win) << 8 | (sum[0] / win);
            const std::uint32_t add = buf[idx(line, std::min(i + r + 1, len - 1))];
            const std::uint32_t sub = buf[idx(line, std::max(i - r, 0))];
            for (int c = 0; c < 3; ++c) {
                const auto ci = static_cast<std::size_t>(c);
                sum[ci] = sum[ci] + channel(add, c) - channel(sub, c);
            }
        }
    }
    buf.swap(out);
}

}  // namespace

PixelBlock pixelate(PixelView src, int x, int y, int w, int h, int block) {
    PixelBlock out;
    const Clip c = clip(src, x, y, w, h);
    if (!src.px || c.empty()) return out;
    out = {c.x0, c.y0, c.x1 - c.x0, c.y1 - c.y0, {}};
    out.px.resize(at(0, out.height, out.width));
    const int b = std::max(block, 1);
    for (int by = c.y0; by < c.y1; by += b) {
        for (int bx = c.x0; bx < c.x1; bx += b) {
            const int ex = std::min(bx + b, c.x1), ey = std::min(by + b, c.y1);
            std::array<std::uint64_t, 3> sum{};
            for (int yy = by; yy < ey; ++yy)
                for (int xx = bx; xx < ex; ++xx) {
                    const std::uint32_t p = src.px[at(xx, yy, src.width)];
                    for (int ch = 0; ch < 3; ++ch) sum[static_cast<std::size_t>(ch)] += channel(p, ch);
                }
            const auto n = static_cast<std::uint64_t>((ex - bx) * (ey - by));
            const auto avg = kOpaque | static_cast<std::uint32_t>(sum[2] / n) << 16 |
                             static_cast<std::uint32_t>(sum[1] / n) << 8 | static_cast<std::uint32_t>(sum[0] / n);
            for (int yy = by; yy < ey; ++yy)
                for (int xx = bx; xx < ex; ++xx) out.px[at(xx - c.x0, yy - c.y0, out.width)] = avg;
        }
    }
    return out;
}

PixelBlock boxBlur(PixelView src, int x, int y, int w, int h, int radius, int passes) {
    PixelBlock out;
    const Clip c = clip(src, x, y, w, h);
    if (!src.px || c.empty()) return out;
    const int r = std::clamp(radius, 0, 64);
    const int n = std::clamp(passes, 1, 4);
    // Work on a margin-padded copy so the region edge sees real neighbours.
    const int m = r * n;
    const Clip pad{std::max(c.x0 - m, 0), std::max(c.y0 - m, 0), std::min(c.x1 + m, src.width),
                   std::min(c.y1 + m, src.height)};
    const int pw = pad.x1 - pad.x0, ph = pad.y1 - pad.y0;
    std::vector<std::uint32_t> buf(at(0, ph, pw));
    for (int yy = 0; yy < ph; ++yy)
        for (int xx = 0; xx < pw; ++xx) buf[at(xx, yy, pw)] = src.px[at(pad.x0 + xx, pad.y0 + yy, src.width)] | kOpaque;
    if (r > 0)
        for (int i = 0; i < n; ++i) {
            boxPass(buf, pw, ph, r, true);
            boxPass(buf, pw, ph, r, false);
        }
    out = {c.x0, c.y0, c.x1 - c.x0, c.y1 - c.y0, {}};
    out.px.resize(at(0, out.height, out.width));
    for (int yy = 0; yy < out.height; ++yy)
        for (int xx = 0; xx < out.width; ++xx)
            out.px[at(xx, yy, out.width)] = buf[at(c.x0 - pad.x0 + xx, c.y0 - pad.y0 + yy, pw)];
    return out;
}

}  // namespace bs
