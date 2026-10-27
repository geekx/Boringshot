#pragma once

#include <array>
#include <optional>

#include "boringshot/geometry.h"

namespace bs {

enum class TextAlign { Left, Right };

// Geometry part of MLeaderStyle (CMF colors/finish live with the renderer).
struct MLeaderGeometry {
    float arrowSize = 12.0f;       // arrowhead length h
    float arrowHalfWidth = 4.5f;   // arrowhead half width w
    float angleStepDeg = 45.0f;    // dogleg snap increment; <= 0 disables snapping
    float landingGap = 4.0f;       // gap between landing and text
    float textHeight = 16.0f;
    float minLandingLength = 24.0f;
};

struct MLeaderInput {
    Vec2 arrowTip;
    Vec2 kink;
    Vec2 textAnchor;
    float textWidth = 0.0f;  // measured by DirectWrite; 0 for empty text
};

struct MLeaderLayout {
    Vec2 arrowTip;
    Vec2 kink;
    Vec2 landingStart;  // always == kink
    Vec2 landingEnd;
    Rect textRect;      // sits above the landing
    TextAlign align = TextAlign::Left;
    // Triangle {tip, base1, base2}; absent when arrowTip == kink.
    std::optional<std::array<Vec2, 3>> arrowHead;
};

// Triangle {tip, base1, base2} = B - hU ± wV with U from `from` toward `tip`;
// h is clamped to the segment length. Empty for a zero-length segment.
std::optional<std::array<Vec2, 3>> arrowHeadTriangle(Vec2 from, Vec2 tip, float h, float halfWidth);

// Rotate (kink - tip) to the nearest multiple of stepDeg, preserving length.
Vec2 snapKink(Vec2 arrowTip, Vec2 kink, float stepDeg);

// Full CAD-style layout: landing is horizontal at kink.y, extends toward the
// anchor side, and is never shorter than the text it carries.
MLeaderLayout layoutMLeader(const MLeaderInput& in, const MLeaderGeometry& g);

// Interactive three-click state machine (doc §5.2).
class MLeaderTool {
public:
    enum class Phase { ArrowTip, Kink, TextAnchor, Editing };

    explicit MLeaderTool(MLeaderGeometry g = {}) : geometry_(g) {}

    Phase phase() const { return phase_; }
    const MLeaderInput& input() const { return input_; }
    void setSnap(bool on) { snap_ = on; }

    void pointerMove(Vec2 p);
    void click(Vec2 p);
    void setTextWidth(float w) { input_.textWidth = w; }
    void cancel();

    // Preview / committed layout; empty while waiting for the first click.
    std::optional<MLeaderLayout> layout() const;

private:
    Vec2 effectiveKink(Vec2 raw) const;

    MLeaderGeometry geometry_;
    MLeaderInput input_;
    Phase phase_ = Phase::ArrowTip;
    bool snap_ = true;
};

}  // namespace bs
