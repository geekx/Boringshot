#include "boringshot/mleader.h"

#include <algorithm>
#include <cmath>

namespace bs {

Vec2 snapKink(Vec2 arrowTip, Vec2 kink, float stepDeg) {
    const Vec2 d = kink - arrowTip;
    const float len = length(d);
    if (stepDeg <= 0.0f || len < 1e-6f) return kink;
    const float step = stepDeg * kPi / 180.0f;
    const float snapped = std::round(std::atan2(d.y, d.x) / step) * step;
    return arrowTip + Vec2{std::cos(snapped), std::sin(snapped)} * len;
}

std::optional<std::array<Vec2, 3>> arrowHeadTriangle(Vec2 from, Vec2 tip, float h, float halfWidth) {
    const Vec2 u = normalize(tip - from);
    if (u == Vec2{}) return std::nullopt;
    const float len = std::min(h, length(tip - from));
    const Vec2 v{-u.y, u.x};
    const Vec2 base = tip - u * len;
    return std::array<Vec2, 3>{tip, base + v * halfWidth, base - v * halfWidth};
}

MLeaderLayout layoutMLeader(const MLeaderInput& in, const MLeaderGeometry& g) {
    MLeaderLayout out;
    out.arrowTip = in.arrowTip;
    out.kink = in.kink;
    out.landingStart = in.kink;

    const float gap = std::max(g.landingGap, 0.0f);
    const float textW = std::max(in.textWidth, 0.0f);
    // The landing must carry the text plus a gap on both ends.
    const float minLen = std::max(g.minLandingLength, textW + 2.0f * gap);

    out.align = in.textAnchor.x >= in.kink.x ? TextAlign::Left : TextAlign::Right;
    const float y = in.kink.y;
    out.textRect.bottom = y - gap;
    out.textRect.top = out.textRect.bottom - std::max(g.textHeight, 0.0f);

    if (out.align == TextAlign::Left) {
        const float endX = std::max(in.textAnchor.x, in.kink.x + minLen);
        out.landingEnd = {endX, y};
        out.textRect.left = in.kink.x + gap;
        out.textRect.right = out.textRect.left + textW;
    } else {
        const float endX = std::min(in.textAnchor.x, in.kink.x - minLen);
        out.landingEnd = {endX, y};
        out.textRect.right = in.kink.x - gap;
        out.textRect.left = out.textRect.right - textW;
    }

    out.arrowHead = arrowHeadTriangle(in.kink, in.arrowTip, g.arrowSize, g.arrowHalfWidth);
    return out;
}

Vec2 MLeaderTool::effectiveKink(Vec2 raw) const {
    return snap_ ? snapKink(input_.arrowTip, raw, geometry_.angleStepDeg) : raw;
}

void MLeaderTool::pointerMove(Vec2 p) {
    switch (phase_) {
        case Phase::Kink:
            input_.kink = effectiveKink(p);
            input_.textAnchor = input_.kink;
            break;
        case Phase::TextAnchor:
            input_.textAnchor = p;
            break;
        case Phase::ArrowTip:
        case Phase::Editing:
            break;
    }
}

void MLeaderTool::click(Vec2 p) {
    switch (phase_) {
        case Phase::ArrowTip:
            input_ = {p, p, p, input_.textWidth};
            phase_ = Phase::Kink;
            break;
        case Phase::Kink:
            pointerMove(p);
            phase_ = Phase::TextAnchor;
            break;
        case Phase::TextAnchor:
            pointerMove(p);
            phase_ = Phase::Editing;
            break;
        case Phase::Editing:
            break;
    }
}

void MLeaderTool::cancel() {
    input_ = {};
    phase_ = Phase::ArrowTip;
}

std::optional<MLeaderLayout> MLeaderTool::layout() const {
    if (phase_ == Phase::ArrowTip) return std::nullopt;
    return layoutMLeader(input_, geometry_);
}

}  // namespace bs
