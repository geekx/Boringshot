#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "boringshot/command_stack.h"
#include "boringshot/geometry.h"
#include "boringshot/mleader.h"

namespace bs {

enum class ShapeKind { Rect, Ellipse, Line, Arrow, Pen, Text, MLeader, Mosaic };

// ── CMF: colour / material (line finish) / end-cap form ──
enum class LineFinish { Solid, Dashed, Dotted, DashDot };
enum class EndShape { None, Triangle, OpenArrow, Dot, Tick, Square };
inline constexpr int kLineFinishCount = 4;
inline constexpr int kEndShapeCount = 6;

enum class ListStyle { None, Bullet, Numbered };
enum class MosaicMode { Pixelate, Blur };
// Material: plain ink, or frosted glass (blurred backdrop with a soft rim).
enum class Material { Solid, Frosted };
inline constexpr int kListStyleCount = 3;

// Option names for menus and tooltips.
const wchar_t* finishName(LineFinish f);
const wchar_t* endShapeName(EndShape e);
const wchar_t* listStyleName(ListStyle l);
const wchar_t* mosaicModeName(MosaicMode m);

inline constexpr int kFontCount = 5;
const wchar_t* fontFamily(int index);  // DirectWrite family name, index clamped
const wchar_t* fontLabel(int index);   // short UI label

struct TextStyle {
    int font = 0;
    bool bold = true;
    bool italic = false;
    ListStyle list = ListStyle::None;
    bool background = false;  // translucent backing plate behind the label
    constexpr bool operator==(const TextStyle&) const = default;
};

struct Color {
    float r = 1.0f, g = 0.25f, b = 0.25f, a = 1.0f;
    constexpr bool operator==(const Color&) const = default;
};

struct Shape {
    ShapeKind kind = ShapeKind::Rect;
    Color color;
    float strokeWidth = 3.0f;
    Vec2 a, b;                  // drag start/end; Text: a = top-left; Line/Arrow: a → b
    std::vector<Vec2> points;   // Pen
    std::wstring text;          // Text / MLeader label (raw, before list formatting)
    float fontSize = 20.0f;
    MLeaderInput leader;        // MLeader (leader.textWidth mirrors textSize.x)
    LineFinish finish = LineFinish::Solid;
    EndShape head = EndShape::None;  // Line/Arrow: at b; MLeader: at the arrow tip
    EndShape tail = EndShape::None;  // Line/Arrow: at a; MLeader: at the landing end
    TextStyle textStyle;
    Vec2 textSize;              // measured label extent, filled in by the renderer
    MosaicMode mosaic = MosaicMode::Pixelate;
    Material material = Material::Solid;
};

// Defaults per kind (Arrow/MLeader get a triangle head, everything else none).
Shape makeShape(ShapeKind kind);

// Shift: square Rect/Ellipse/Mosaic, 45°-snapped Line/Arrow. Other kinds pass through.
Vec2 constrainDrag(ShapeKind kind, Vec2 start, Vec2 current, bool shift);

// Appends p unless it is closer than minDist to the last point (stroke decimation).
void appendPenPoint(std::vector<Vec2>& points, Vec2 p, float minDist);

// A finished drag shape is kept only if it has visible extent.
bool isMeaningful(const Shape& s);

// "* x" / "＊x" lines always become bullets; ListStyle prefixes every non-empty
// line with "• " or "1. ", "2. " … (empty lines are kept and not counted).
std::wstring formatList(const std::wstring& text, ListStyle style);
// Text actually drawn for a shape.
std::wstring displayText(const Shape& s);

// MLEADER geometry derived from stroke width, font size and measured label.
MLeaderGeometry leaderGeometryFor(const Shape& s);
MLeaderLayout leaderLayout(const Shape& s);

// End cap at `tip` for a segment arriving from `from`. `shaftEnd` is where the
// line should stop so a thick stroke never pokes through a filled head.
struct EndCap {
    EndShape shape = EndShape::None;
    std::vector<Vec2> outline;  // polygon (filled/closed) or polyline (open)
    bool filled = false;
    bool closed = false;
    Vec2 center;                // Dot
    float radius = 0.0f;        // Dot
    Vec2 shaftEnd;
};
// Frosted dots grow into a lens so the blurred disc reads as a highlight.
EndCap makeEndCap(EndShape shape, Vec2 from, Vec2 tip, float strokeWidth, Material material = Material::Solid);

// Text editing keys (WM_CHAR): Enter breaks the line, Ctrl+Enter (LF) finishes.
enum class TextKey { Ignore, Insert, Newline, Backspace, Commit };
TextKey classifyTextChar(wchar_t c, bool ctrl);

// Acute inclination of a segment against the horizontal, in degrees [0, 90].
float inclinationDeg(Vec2 a, Vec2 b);

// Annotation list with undo/redo; every mutation goes through the history.
class Document {
public:
    const std::vector<Shape>& shapes() const { return shapes_; }
    bool empty() const { return shapes_.empty(); }

    bool add(Shape s);  // false (and no history entry) if not meaningful
    bool replace(std::size_t index, Shape s);  // false if out of range or unchanged-meaningless
    bool remove(std::size_t index);
    bool undo() { return history_.undo(); }
    bool redo() { return history_.redo(); }
    bool canUndo() const { return history_.canUndo(); }
    bool canRedo() const { return history_.canRedo(); }
    void clear();

private:
    std::vector<Shape> shapes_;
    CommandStack history_{512};
};

}  // namespace bs
