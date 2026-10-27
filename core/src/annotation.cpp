#include "boringshot/annotation.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace bs {
namespace {

constexpr const wchar_t* kFontFamilies[kFontCount] = {L"Microsoft YaHei UI", L"SimSun", L"KaiTi", L"Segoe UI",
                                                      L"Consolas"};
constexpr const wchar_t* kFontLabels[kFontCount] = {L"雅黑", L"宋体", L"楷体", L"Segoe", L"等宽"};

class AddShape : public Command {
public:
    AddShape(std::vector<Shape>& list, Shape s) : list_(list), shape_(std::move(s)) {}
    void apply() override { list_.push_back(shape_); }
    void revert() override { list_.pop_back(); }  // history is LIFO, so ours is last

private:
    std::vector<Shape>& list_;
    Shape shape_;
};

class ReplaceShape : public Command {
public:
    ReplaceShape(std::vector<Shape>& list, std::size_t i, Shape after)
        : list_(list), index_(i), before_(list[i]), after_(std::move(after)) {}
    void apply() override { list_[index_] = after_; }
    void revert() override { list_[index_] = before_; }

private:
    std::vector<Shape>& list_;
    std::size_t index_;
    Shape before_, after_;
};

class RemoveShape : public Command {
public:
    RemoveShape(std::vector<Shape>& list, std::size_t i) : list_(list), index_(i), shape_(list[i]) {}
    void apply() override { list_.erase(list_.begin() + static_cast<std::ptrdiff_t>(index_)); }
    void revert() override { list_.insert(list_.begin() + static_cast<std::ptrdiff_t>(index_), shape_); }

private:
    std::vector<Shape>& list_;
    std::size_t index_;
    Shape shape_;
};

int clampFont(int i) { return std::clamp(i, 0, kFontCount - 1); }

// Strips a "* " / "＊" bullet marker; returns true if one was present.
bool stripBullet(std::wstring& line) {
    if (line.size() >= 2 && line[0] == L'*' && line[1] == L' ') {
        line.erase(0, 2);
        return true;
    }
    if (!line.empty() && line[0] == L'＊') {
        line.erase(0, line.size() >= 2 && line[1] == L' ' ? 2 : 1);
        return true;
    }
    return false;
}

}  // namespace

const wchar_t* finishName(LineFinish f) {
    switch (f) {
        case LineFinish::Solid: return L"实线";
        case LineFinish::Dashed: return L"虚线";
        case LineFinish::Dotted: return L"点线";
        case LineFinish::DashDot: return L"点划线";
    }
    return L"?";
}

const wchar_t* endShapeName(EndShape e) {
    switch (e) {
        case EndShape::None: return L"无";
        case EndShape::Triangle: return L"实心箭头";
        case EndShape::OpenArrow: return L"开放箭头";
        case EndShape::Dot: return L"圆点";
        case EndShape::Tick: return L"建筑斜刻度";
        case EndShape::Square: return L"方块";
    }
    return L"?";
}

const wchar_t* listStyleName(ListStyle l) {
    switch (l) {
        case ListStyle::None: return L"无列表";
        case ListStyle::Bullet: return L"圆点列表 •";
        case ListStyle::Numbered: return L"编号列表 1. 2. 3.";
    }
    return L"?";
}

const wchar_t* mosaicModeName(MosaicMode m) {
    return m == MosaicMode::Pixelate ? L"像素马赛克" : L"毛玻璃模糊";
}

TextKey classifyTextChar(wchar_t c, bool ctrl) {
    if (c == L'\b') return TextKey::Backspace;
    if (c == L'\n') return TextKey::Commit;  // Ctrl+Enter arrives as LF
    if (c == L'\r') return ctrl ? TextKey::Commit : TextKey::Newline;
    if (c < 32) return TextKey::Ignore;       // other Ctrl+letter chords
    return TextKey::Insert;
}

const wchar_t* fontFamily(int index) { return kFontFamilies[clampFont(index)]; }
const wchar_t* fontLabel(int index) { return kFontLabels[clampFont(index)]; }

Shape makeShape(ShapeKind kind) {
    Shape s;
    s.kind = kind;
    if (kind == ShapeKind::Arrow || kind == ShapeKind::MLeader) s.head = EndShape::Triangle;
    return s;
}

Vec2 constrainDrag(ShapeKind kind, Vec2 start, Vec2 cur, bool shift) {
    if (!shift) return cur;
    switch (kind) {
        case ShapeKind::Rect:
        case ShapeKind::Ellipse:
        case ShapeKind::Mosaic: {
            const Vec2 d = cur - start;
            const float side = std::max(std::fabs(d.x), std::fabs(d.y));
            return start + Vec2{d.x < 0 ? -side : side, d.y < 0 ? -side : side};
        }
        case ShapeKind::Line:
        case ShapeKind::Arrow:
            return snapKink(start, cur, 45.0f);
        default:
            return cur;
    }
}

void appendPenPoint(std::vector<Vec2>& points, Vec2 p, float minDist) {
    if (points.empty() || length(p - points.back()) >= minDist) points.push_back(p);
}

bool isMeaningful(const Shape& s) {
    switch (s.kind) {
        case ShapeKind::Rect:
        case ShapeKind::Ellipse:
        case ShapeKind::Mosaic:
            return std::fabs(s.b.x - s.a.x) >= 2.0f && std::fabs(s.b.y - s.a.y) >= 2.0f;
        case ShapeKind::Line:
        case ShapeKind::Arrow:
            return length(s.b - s.a) >= 3.0f;
        case ShapeKind::Pen:
            return s.points.size() >= 2;
        case ShapeKind::Text:
            return !s.text.empty();
        case ShapeKind::MLeader:
            return true;  // a bare leader is still a valid pointer
    }
    return false;
}

std::wstring formatList(const std::wstring& text, ListStyle style) {
    std::wstring out;
    int number = 0;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t nl = text.find(L'\n', start);
        std::wstring line = text.substr(start, nl == std::wstring::npos ? std::wstring::npos : nl - start);
        const bool bullet = stripBullet(line);
        if (!line.empty() || bullet) {
            if (bullet || style == ListStyle::Bullet) {
                out += L"• ";
            } else if (style == ListStyle::Numbered) {
                out += std::to_wstring(++number) + L". ";
            }
        }
        out += line;
        if (nl == std::wstring::npos) break;
        out += L'\n';
        start = nl + 1;
    }
    return out;
}

std::wstring displayText(const Shape& s) { return formatList(s.text, s.textStyle.list); }

MLeaderGeometry leaderGeometryFor(const Shape& s) {
    MLeaderGeometry g;
    g.arrowSize = 3.0f * s.strokeWidth + 8.0f;
    g.arrowHalfWidth = 1.5f * s.strokeWidth + 3.5f;
    g.textHeight = s.textSize.y > 0.0f ? s.textSize.y : s.fontSize * 1.35f;
    g.landingGap = 2.0f + s.strokeWidth;
    return g;
}

MLeaderLayout leaderLayout(const Shape& s) {
    MLeaderInput in = s.leader;
    in.textWidth = s.text.empty() ? 0.0f : s.textSize.x;
    return layoutMLeader(in, leaderGeometryFor(s));
}

EndCap makeEndCap(EndShape shape, Vec2 from, Vec2 tip, float w, Material material) {
    EndCap cap;
    cap.shape = shape;
    cap.shaftEnd = tip;
    const Vec2 u = normalize(tip - from);
    if (u == Vec2{} || shape == EndShape::None) {
        cap.shape = u == Vec2{} ? EndShape::None : shape;
        return cap;
    }
    const Vec2 v{-u.y, u.x};
    const float size = 3.0f * w + 10.0f;
    const float half = 1.5f * w + 5.0f;
    switch (shape) {
        case EndShape::Triangle: {
            const auto t = arrowHeadTriangle(from, tip, size, half);
            cap.outline = {(*t)[0], (*t)[1], (*t)[2]};
            cap.filled = cap.closed = true;
            cap.shaftEnd = ((*t)[1] + (*t)[2]) * 0.5f;
            break;
        }
        case EndShape::OpenArrow: {
            const auto t = arrowHeadTriangle(from, tip, size, half);
            cap.outline = {(*t)[1], (*t)[0], (*t)[2]};
            break;
        }
        case EndShape::Dot:
            cap.center = tip;
            cap.radius = material == Material::Frosted ? 2.6f * w + 8.0f : 1.2f * w + 3.0f;
            cap.filled = true;
            break;
        case EndShape::Tick: {  // CAD architectural tick: oblique stroke across the end
            const Vec2 d = normalize(u + v) * (size * 0.5f);
            cap.outline = {tip - d, tip + d};
            break;
        }
        case EndShape::Square: {
            const float r = 1.2f * w + 3.0f;
            cap.outline = {tip + (u + v) * r, tip + (v - u) * r, tip - (u + v) * r, tip + (u - v) * r};
            cap.filled = cap.closed = true;
            break;
        }
        case EndShape::None:
            break;
    }
    return cap;
}

float inclinationDeg(Vec2 a, Vec2 b) {
    const Vec2 d = b - a;
    if (length(d) < 1e-6f) return 0.0f;
    return std::atan2(std::fabs(d.y), std::fabs(d.x)) * 180.0f / kPi;
}

bool Document::add(Shape s) {
    if (!isMeaningful(s)) return false;
    history_.execute(std::make_unique<AddShape>(shapes_, std::move(s)));
    return true;
}

bool Document::replace(std::size_t index, Shape s) {
    if (index >= shapes_.size() || !isMeaningful(s)) return false;
    history_.execute(std::make_unique<ReplaceShape>(shapes_, index, std::move(s)));
    return true;
}

bool Document::remove(std::size_t index) {
    if (index >= shapes_.size()) return false;
    history_.execute(std::make_unique<RemoveShape>(shapes_, index));
    return true;
}

void Document::clear() {
    shapes_.clear();
    history_.clear();
}

}  // namespace bs
