#include "boringshot/watermark.h"

#include <algorithm>
#include <cmath>
#include <cwchar>

namespace bs {
namespace {

std::wstring pad2(int v) {
    wchar_t buf[16];
    std::swprintf(buf, 16, L"%02d", v);
    return buf;
}

std::wstring formatDate(const DateTimeParts& t) {
    wchar_t buf[32];
    std::swprintf(buf, 32, L"%04d-%02d-%02d", t.year, t.month, t.day);
    return buf;
}

}  // namespace

std::wstring expandWatermarkText(const std::wstring& tpl, const WatermarkContext& ctx) {
    std::wstring out;
    out.reserve(tpl.size() + 16);
    for (std::size_t i = 0; i < tpl.size(); ++i) {
        const wchar_t c = tpl[i];
        if ((c == L'{' || c == L'}') && i + 1 < tpl.size() && tpl[i + 1] == c) {
            out += c;
            ++i;
            continue;
        }
        if (c == L'{') {
            const std::size_t close = tpl.find(L'}', i + 1);
            if (close != std::wstring::npos) {
                const std::wstring key = tpl.substr(i + 1, close - i - 1);
                if (key == L"user") {
                    out += ctx.user;
                    i = close;
                    continue;
                }
                if (key == L"date") {
                    out += formatDate(ctx.now);
                    i = close;
                    continue;
                }
                if (key == L"time") {
                    out += pad2(ctx.now.hour) + L":" + pad2(ctx.now.minute);
                    i = close;
                    continue;
                }
            }
        }
        out += c;
    }
    return out;
}

bool watermarkNeedsClock(const std::wstring& tpl) {
    // Compare two expansions a minute and a day apart instead of re-parsing.
    WatermarkContext a, b;
    b.now = {1971, 2, 2, 1, 1};
    return expandWatermarkText(tpl, a) != expandWatermarkText(tpl, b);
}

std::vector<Vec2> watermarkTileOrigins(const Rect& area, float spacingX, float spacingY,
                                       std::size_t maxTiles) {
    std::vector<Vec2> out;
    const float sx = std::max(spacingX, 1.0f);
    const float sy = std::max(spacingY, 1.0f);
    if (area.width() <= 0.0f || area.height() <= 0.0f || maxTiles == 0) return out;

    const auto cols = static_cast<std::size_t>(std::ceil(area.width() / sx)) + 2;
    const auto rows = static_cast<std::size_t>(std::ceil(area.height() / sy)) + 2;
    out.reserve(std::min(cols * rows, maxTiles));
    for (std::size_t r = 0; r < rows; ++r) {
        const float stagger = (r % 2 == 1) ? sx * 0.5f : 0.0f;
        const float y = area.top - sy + static_cast<float>(r) * sy;
        for (std::size_t c = 0; c < cols; ++c) {
            if (out.size() >= maxTiles) return out;
            out.push_back({area.left - sx + stagger + static_cast<float>(c) * sx, y});
        }
    }
    return out;
}

}  // namespace bs
