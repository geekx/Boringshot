#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "boringshot/geometry.h"

namespace bs {

struct DateTimeParts {
    int year = 1970, month = 1, day = 1;
    int hour = 0, minute = 0;
};

struct WatermarkContext {
    std::wstring user;
    DateTimeParts now;
};

// Expands {user} {date} {time}; "{{" and "}}" escape braces; unknown
// placeholders are kept verbatim so typos stay visible.
std::wstring expandWatermarkText(const std::wstring& tpl, const WatermarkContext& ctx);

// True if the expansion changes at minute granularity (drives the redraw timer).
bool watermarkNeedsClock(const std::wstring& tpl);

struct WatermarkProfile {
    std::wstring text = L"{user}  {date} {time}";
    std::wstring fontFamily = L"Segoe UI";
    float fontSize = 18.0f;
    float colorR = 0.5f, colorG = 0.5f, colorB = 0.5f;
    float opacity = 0.18f;
    float rotationDeg = -30.0f;
    float tileSpacingX = 320.0f;
    float tileSpacingY = 180.0f;
    bool enableOnAllMonitors = true;
};

// Tile anchor points covering `area` (with one tile of overscan so rotated
// text reaches the edges). Odd rows are staggered by half a column. Spacing is
// clamped to >= 1 and the count is capped at `maxTiles`.
std::vector<Vec2> watermarkTileOrigins(const Rect& area, float spacingX, float spacingY,
                                       std::size_t maxTiles = 20000);

}  // namespace bs
