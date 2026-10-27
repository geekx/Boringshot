#include "capture_window.h"

#include <imm.h>

#include <algorithm>
#include <cmath>
#include <cwchar>

#include "boringshot/anim.h"
#include "boringshot/color.h"
#include "boringshot/shape_edit.h"

using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kClassName[] = L"Boringshot.Capture";
constexpr UINT_PTR kAnimTimer = 1;
constexpr UINT_PTR kTipTimer = 2;
constexpr UINT kTipDelayMs = 450;

const bs::Color kPalette[] = {
    {1.00f, 0.30f, 0.31f, 1.0f},  // red
    {1.00f, 0.77f, 0.24f, 1.0f},  // amber
    {0.32f, 0.77f, 0.10f, 1.0f},  // green
    {0.09f, 0.47f, 1.00f, 1.0f},  // blue
    {1.00f, 1.00f, 1.00f, 1.0f},  // white
    {0.10f, 0.10f, 0.10f, 1.0f},  // ink
};
constexpr int kPaletteSize = 6;
constexpr int kFrostedSwatch = kPaletteSize;  // 7th swatch: frosted-glass material
constexpr int kSwatchCount = kPaletteSize + 1;
const wchar_t* const kSwatchNames[kSwatchCount] = {L"红色", L"琥珀", L"绿色", L"蓝色", L"白色", L"墨黑",
                                                    L"毛玻璃材质（模糊背景 + 1–2px 柔边；引线圆点头会变成透镜）"};
constexpr float kWidths[] = {2.0f, 4.0f, 8.0f};
constexpr float kFontSizes[] = {16.0f, 22.0f, 32.0f};
constexpr int kWidthCount = 3;

// MD3-flavoured dark chrome.
const D2D1_COLOR_F kAccent = D2D1::ColorF(0.25f, 0.56f, 1.0f, 1.0f);
const D2D1_COLOR_F kAccentSoft = D2D1::ColorF(0.25f, 0.56f, 1.0f, 0.30f);
const D2D1_COLOR_F kSurface = D2D1::ColorF(0.11f, 0.11f, 0.13f, 0.95f);
const D2D1_COLOR_F kOnSurface = D2D1::ColorF(0.92f, 0.92f, 0.95f, 1.0f);
const D2D1_COLOR_F kMuted = D2D1::ColorF(0.55f, 0.55f, 0.60f, 1.0f);
const D2D1_COLOR_F kHover = D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.10f);
const D2D1_COLOR_F kDim = D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.45f);

bs::Vec2 pointFrom(LPARAM lp) {
    return {static_cast<float>(static_cast<short>(LOWORD(lp))), static_cast<float>(static_cast<short>(HIWORD(lp)))};
}

bool keyDown(int vk) { return GetKeyState(vk) < 0; }

bs::ShapeKind kindFor(int tool) {
    switch (tool) {
        case 2: return bs::ShapeKind::Ellipse;
        case 3: return bs::ShapeKind::Line;
        case 4: return bs::ShapeKind::Arrow;
        case 5: return bs::ShapeKind::Pen;
        case 6: return bs::ShapeKind::Text;
        case 7: return bs::ShapeKind::MLeader;
        case 8: return bs::ShapeKind::Mosaic;
        default: return bs::ShapeKind::Rect;
    }
}

int toolFor(bs::ShapeKind k) {
    switch (k) {
        case bs::ShapeKind::Rect: return 1;
        case bs::ShapeKind::Ellipse: return 2;
        case bs::ShapeKind::Line: return 3;
        case bs::ShapeKind::Arrow: return 4;
        case bs::ShapeKind::Pen: return 5;
        case bs::ShapeKind::Text: return 6;
        case bs::ShapeKind::MLeader: return 7;
        case bs::ShapeKind::Mosaic: return 8;
    }
    return 1;
}

bool hasText(bs::ShapeKind k) { return k == bs::ShapeKind::Text || k == bs::ShapeKind::MLeader; }
bool hasCaps(bs::ShapeKind k) {
    return k == bs::ShapeKind::Line || k == bs::ShapeKind::Arrow || k == bs::ShapeKind::MLeader;
}

int wrap(int v, int n) { return ((v % n) + n) % n; }

ComPtr<IDWriteTextFormat> makeFormat(const wchar_t* family, float size, DWRITE_FONT_WEIGHT weight,
                                     DWRITE_FONT_STYLE style, bool center) {
    ComPtr<IDWriteTextFormat> f;
    gfx::factories().dwrite->CreateTextFormat(family, nullptr, weight, style, DWRITE_FONT_STRETCH_NORMAL, size, L"",
                                              f.GetAddressOf());
    if (f && center) {
        f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    return f;
}

LONGLONG qpcNow() {
    LARGE_INTEGER t{};
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

}  // namespace

// ───────────────────────────── lifecycle ─────────────────────────────

bool CaptureWindow::open(HINSTANCE instance, ScreenGrab grab, Callbacks cb, CaptureOptions opts) {
    if (grab.image.empty()) return false;
    grab_ = std::move(grab);
    cb_ = std::move(cb);
    opts_ = opts;
    ui_ = static_cast<float>(GetDpiForSystem()) / 96.0f;
    for (int t = 0; t < kToolCount; ++t) templates_[static_cast<std::size_t>(t)] = bs::makeShape(kindFor(t));

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = &CaptureWindow::wndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    const RECT& v = grab_.virtualRect;
    CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kClassName, L"Boringshot", WS_POPUP, v.left, v.top,
                    v.right - v.left, v.bottom - v.top, nullptr, nullptr, instance, this);
    if (!hwnd_) return false;

    POINT pt{};
    GetCursorPos(&pt);
    cursor_ = {static_cast<float>(pt.x - v.left), static_cast<float>(pt.y - v.top)};
    hover_ = bs::pickWindowRect(grab_.windows, cursor_);
    // The frame is already frozen, so the animation can never leak into the capture (doc §7.3).
    if (opts_.animate) {
        animating_ = true;
        animStart_ = qpcNow();
        SetTimer(hwnd_, kAnimTimer, 10, nullptr);
    }
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    UpdateWindow(hwnd_);
    return true;
}

void CaptureWindow::close() {
    if (hwnd_) DestroyWindow(hwnd_);
}

LRESULT CALLBACK CaptureWindow::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* self = static_cast<CaptureWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<CaptureWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    if (msg == WM_NCDESTROY) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        self->hwnd_ = nullptr;
        self->rt_.Reset();
        self->shotBitmap_.Reset();
        if (auto closed = self->cb_.closed) closed();  // owner defers deletion
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    return self->handle(msg, wp, lp);
}

LRESULT CaptureWindow::handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd_, &ps);
            paint();
            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_TIMER:
            if (wp == kTipTimer) {
                KillTimer(hwnd_, kTipTimer);
                if (hoverButton_ >= 0 && !menu_.open) tipButton_ = hoverButton_;
                invalidate();
            }
            if (wp == kAnimTimer) {
                if (animElapsedMs() >= bs::rippleTotalMs()) {
                    animating_ = false;
                    KillTimer(hwnd_, kAnimTimer);
                }
                invalidate();
            }
            return 0;
        case WM_SIZE:
            if (rt_) rt_->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp)));
            return 0;
        case WM_MOUSEMOVE:
            onMouseMove(pointFrom(lp), (wp & MK_SHIFT) != 0);
            return 0;
        case WM_LBUTTONDOWN:
            if (animating_) return 0;  // no input until the summon animation settles
            SetCapture(hwnd_);
            onLButtonDown(pointFrom(lp));
            return 0;
        case WM_LBUTTONUP:
            ReleaseCapture();
            if (!animating_) onLButtonUp(pointFrom(lp), (wp & MK_SHIFT) != 0);
            return 0;
        case WM_LBUTTONDBLCLK:
            if (!animating_) onDoubleClick(pointFrom(lp));
            return 0;
        case WM_RBUTTONUP:
            if (!animating_) onRButtonUp(pointFrom(lp));
            return 0;
        case WM_KEYDOWN:
            onKeyDown(wp);
            return 0;
        case WM_CHAR:
            onChar(static_cast<wchar_t>(wp));
            return 0;
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT) {
                updateCursor();
                return TRUE;
            }
            break;
        case WM_IME_STARTCOMPOSITION:
            placeImeWindow();
            break;
        default:
            break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

// ───────────────────────────── state helpers ─────────────────────────────

void CaptureWindow::invalidate() {
    if (!hwnd_) return;  // never InvalidateRect(nullptr): that repaints every window
    if (mode_ == Mode::Selected) layoutToolbar();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

float CaptureWindow::animElapsedMs() const {
    LARGE_INTEGER f{};
    QueryPerformanceFrequency(&f);
    return f.QuadPart > 0 ? static_cast<float>(static_cast<double>(qpcNow() - animStart_) * 1000.0 /
                                               static_cast<double>(f.QuadPart))
                          : 1e9f;
}

bs::Rect CaptureWindow::bounds() const {
    return {0.0f, 0.0f, static_cast<float>(grab_.image.width), static_cast<float>(grab_.image.height)};
}

bs::Rect CaptureWindow::activeRect() const {
    switch (mode_) {
        case Mode::Picking:
            return hover_.value_or(bs::Rect{});
        case Mode::Dragging:
            return bs::isClick(dragStart_, cursor_) ? hover_.value_or(bs::Rect{}) : sel_;
        case Mode::Selected:
            return sel_;
    }
    return {};
}

int CaptureWindow::buttonAt(bs::Vec2 p) const {
    if (mode_ != Mode::Selected) return -1;
    for (std::size_t i = 0; i < buttons_.size(); ++i)
        if (bs::contains(buttons_[i].rect, p)) return static_cast<int>(i);
    return -1;
}

bs::Shape CaptureWindow::styledShape(Tool t) const {
    bs::Shape s = templates_[static_cast<std::size_t>(t)];
    const bool frosted = colorIndex_ == kFrostedSwatch;
    s.material = frosted ? bs::Material::Frosted : bs::Material::Solid;
    s.color = frosted ? bs::Color{1, 1, 1, 1} : kPalette[colorIndex_];
    s.strokeWidth = kWidths[widthIndex_];
    s.fontSize = kFontSizes[widthIndex_];
    return s;
}

std::size_t CaptureWindow::hiddenIndex() const {
    if (pick_.active && picked_) return *picked_;
    if (edit_ != Edit::None && editIndex_) return *editIndex_;
    return static_cast<std::size_t>(-1);
}

std::optional<bs::ShapeKind> CaptureWindow::contextKind() const {
    if (edit_ != Edit::None) return editShape_.kind;
    if (picked_ && *picked_ < doc_.shapes().size()) return doc_.shapes()[*picked_].kind;
    if (tool_ != Tool::None) return kindFor(static_cast<int>(tool_));
    return std::nullopt;
}

const bs::Shape& CaptureWindow::styleSource() const {
    if (edit_ != Edit::None) return editShape_;
    if (picked_ && *picked_ < doc_.shapes().size()) return doc_.shapes()[*picked_];
    return templates_[static_cast<std::size_t>(tool_)];
}

void CaptureWindow::applyStyle(const std::function<void(bs::Shape&)>& fn) {
    const auto kind = contextKind();
    if (!kind) return;
    if (edit_ != Edit::None) {
        fn(editShape_);
        gfx::measureShapeText(editShape_);
    } else if (picked_ && *picked_ < doc_.shapes().size()) {
        bs::Shape s = doc_.shapes()[*picked_];
        fn(s);
        if (hasText(s.kind)) gfx::measureShapeText(s);
        doc_.replace(*picked_, std::move(s));
    }
    fn(templates_[static_cast<std::size_t>(toolFor(*kind))]);  // next shapes follow the last choice
}

// ───────────────────────────── input ─────────────────────────────

void CaptureWindow::onMouseMove(bs::Vec2 p, bool shift) {
    cursor_ = p;
    switch (mode_) {
        case Mode::Picking:
            hover_ = bs::pickWindowRect(grab_.windows, p);
            break;
        case Mode::Dragging:
            sel_ = bs::intersect(bs::normalizedRect(dragStart_, p), bounds());
            break;
        case Mode::Selected: {
            const bs::Vec2 clamped{std::clamp(p.x, sel_.left, sel_.right), std::clamp(p.y, sel_.top, sel_.bottom)};
            if (pick_.active) {
                live_ = pick_.handle >= 0 ? bs::moveControlPoint(pick_.original, pick_.handle, clamped, shift)
                                          : bs::translate(pick_.original, p - pick_.start);
            } else if (dragHandle_ != bs::Handle::None) {
                sel_ = bs::dragHandle(selAtDragStart_, dragHandle_, p - dragStart_, bounds());
            } else if (drawing_) {
                if (draft_.kind == bs::ShapeKind::Pen)
                    bs::appendPenPoint(draft_.points, clamped, 1.5f);
                else
                    draft_.b = bs::constrainDrag(draft_.kind, draft_.a, clamped, shift);
            } else if (tool_ == Tool::MLeader) {
                leader_.setSnap(!shift);  // 45° snapping by default, Shift for free angles
                leader_.pointerMove(clamped);
            }
            if (menu_.open) menu_.hover = menuRowAt(p);
            if (const int hb = buttonAt(p); hb != hoverButton_) {  // restart the tooltip delay
                hoverButton_ = hb;
                tipButton_ = -1;
                KillTimer(hwnd_, kTipTimer);
                if (hb >= 0 && !menu_.open) SetTimer(hwnd_, kTipTimer, kTipDelayMs, nullptr);
            }
            break;
        }
    }
    updateCursor();
    invalidate();
}

void CaptureWindow::onLButtonDown(bs::Vec2 p) {
    cursor_ = p;
    if (mode_ == Mode::Picking) {
        mode_ = Mode::Dragging;
        dragStart_ = p;
        sel_ = {};
        invalidate();
        return;
    }
    if (mode_ != Mode::Selected) return;
    tipButton_ = -1;
    if (menu_.open) {  // a click picks a row or just closes the menu
        if (const int row = menuRowAt(p); row >= 0) setStyleValue(menu_.action, row);
        menu_.open = false;
        invalidate();
        return;
    }

    if (const int b = buttonAt(p); b >= 0) {  // style buttons act on the text being edited
        pressedButton_ = b;
        return;
    }
    if (edit_ != Edit::None) {  // clicking away just finishes the text
        commitEdit();
        invalidate();
        return;
    }
    const bool leaderBusy = tool_ == Tool::MLeader && leader_.phase() != bs::MLeaderTool::Phase::ArrowTip;
    if (!leaderBusy && bs::contains(sel_, p)) {
        if (const auto hit = bs::hitTestShapes(doc_.shapes(), p, 5.0f * ui_, picked_)) {
            picked_ = hit->index;
            pick_ = {true, hit->handle, p, doc_.shapes()[hit->index]};
            live_ = pick_.original;
            invalidate();
            return;
        }
    }
    picked_.reset();

    const bs::Handle h = bs::hitTestHandle(sel_, p, 6.0f * ui_);
    const bool resize = h != bs::Handle::None && h != bs::Handle::Move;
    if (!leaderBusy && (resize || (tool_ == Tool::None && h == bs::Handle::Move))) {
        dragHandle_ = h;
        dragStart_ = p;
        selAtDragStart_ = sel_;
        invalidate();
        return;
    }
    if (tool_ == Tool::None || !bs::contains(sel_, p)) {
        invalidate();
        return;
    }

    switch (tool_) {
        case Tool::Text:
            beginEdit(Edit::Text, p);
            break;
        case Tool::MLeader:
            leader_.click(p);
            if (leader_.phase() == bs::MLeaderTool::Phase::Editing) beginEdit(Edit::LeaderLabel, p);
            break;
        default:
            drawing_ = true;
            draft_ = styledShape(tool_);
            draft_.a = draft_.b = p;
            draft_.points = {p};
            break;
    }
    invalidate();
}

void CaptureWindow::onLButtonUp(bs::Vec2 p, bool shift) {
    if (mode_ == Mode::Dragging) {
        if (bs::isClick(dragStart_, p)) sel_ = hover_.value_or(bs::Rect{});
        if (sel_.width() < 2.0f || sel_.height() < 2.0f) {
            resetToPicking();
            return;
        }
        mode_ = Mode::Selected;
        invalidate();
        return;
    }
    if (pressedButton_ >= 0) {
        const int b = pressedButton_;
        pressedButton_ = -1;
        if (buttonAt(p) == b) {
            const Button btn = buttons_[static_cast<std::size_t>(b)];
            if (isMenuAction(btn.action)) {
                openMenu(btn);
                invalidate();
            } else {
                run(btn.action, btn.arg, +1);  // may close the window
            }
        }
        return;
    }
    if (pick_.active) {
        pick_.active = false;
        if (picked_ && !bs::isClick(pick_.start, p)) doc_.replace(*picked_, live_);  // one undo step per drag
    } else if (dragHandle_ != bs::Handle::None) {
        dragHandle_ = bs::Handle::None;
        if (bs::isEmpty(sel_)) {
            resetToPicking();
            return;
        }
    } else if (drawing_) {
        drawing_ = false;
        if (draft_.kind != bs::ShapeKind::Pen) {
            const bs::Vec2 c{std::clamp(p.x, sel_.left, sel_.right), std::clamp(p.y, sel_.top, sel_.bottom)};
            draft_.b = bs::constrainDrag(draft_.kind, draft_.a, c, shift);
        }
        if (doc_.add(draft_)) picked_.reset();
    }
    invalidate();
}

void CaptureWindow::onDoubleClick(bs::Vec2 p) {
    if (mode_ != Mode::Selected || edit_ != Edit::None) return;
    // Double-click a label to edit it in place; elsewhere (no tool) = copy and done.
    if (const auto hit = bs::hitTestShapes(doc_.shapes(), p, 5.0f * ui_, std::nullopt);
        hit && hasText(doc_.shapes()[hit->index].kind)) {
        beginEditExisting(hit->index);
        invalidate();
        return;
    }
    if (tool_ == Tool::None && bs::contains(sel_, p)) finishCopy();
}

void CaptureWindow::onRButtonUp(bs::Vec2 p) {
    if (menu_.open) {
        menu_.open = false;
        invalidate();
        return;
    }
    if (const int b = buttonAt(p); b >= 0) {  // right-click cycles a style forward without the menu
        const Button btn = buttons_[static_cast<std::size_t>(b)];
        if (isMenuAction(btn.action)) {
            run(btn.action, btn.arg, +1);
            return;
        }
    }
    if (edit_ != Edit::None) {
        commitEdit();  // never throw typed text away
    } else if (drawing_ || leader_.phase() != bs::MLeaderTool::Phase::ArrowTip) {
        drawing_ = false;
        leader_.cancel();
    } else if (picked_) {
        picked_.reset();
    } else if (mode_ == Mode::Selected) {
        resetToPicking();
        return;
    } else {
        close();
        return;
    }
    invalidate();
}

void CaptureWindow::onKeyDown(WPARAM key) {
    const bool ctrl = keyDown(VK_CONTROL);
    const bool shift = keyDown(VK_SHIFT);
    if (menu_.open && key == VK_ESCAPE) {
        menu_.open = false;
        invalidate();
        return;
    }
    if (edit_ != Edit::None) {  // characters arrive through WM_CHAR
        if (key == VK_ESCAPE) commitEdit();  // Esc finishes, keeping the text
        if (ctrl && key == 'B') applyStyle([](bs::Shape& s) { s.textStyle.bold = !s.textStyle.bold; });
        if (ctrl && key == 'I') applyStyle([](bs::Shape& s) { s.textStyle.italic = !s.textStyle.italic; });
        invalidate();
        return;
    }
    const bool selected = mode_ == Mode::Selected;
    switch (key) {
        case VK_ESCAPE:
            if (drawing_ || leader_.phase() != bs::MLeaderTool::Phase::ArrowTip) {
                drawing_ = false;
                leader_.cancel();
            } else if (picked_) {
                picked_.reset();
            } else {
                close();
                return;
            }
            break;
        case VK_DELETE:
        case VK_BACK:
            deletePicked();
            break;
        case VK_RETURN:
            if (selected) finishCopy();
            return;
        case 'Z':
            if (ctrl) {
                shift ? doc_.redo() : doc_.undo();
                picked_.reset();
            }
            break;
        case 'Y':
            if (ctrl) {
                doc_.redo();
                picked_.reset();
            }
            break;
        case 'C':
            if (ctrl && selected) {
                finishCopy();
                return;
            }
            if (!ctrl && !selected) {
                const int x = std::clamp(static_cast<int>(cursor_.x), 0, grab_.image.width - 1);
                const int y = std::clamp(static_cast<int>(cursor_.y), 0, grab_.image.height - 1);
                const bs::Rgb8 c = bs::fromBgra(grab_.image.at(x, y));
                const std::wstring value = shift ? bs::rgbString(c) : bs::hexString(c);
                if (gfx::copyTextToClipboard(hwnd_, value)) copiedNote_ = L"已复制 " + value;
            }
            break;
        case 'S':
            if (ctrl && selected) {
                finishSave();
                return;
            }
            break;
        case 'T':
            if (ctrl && selected) {
                finishPin();
                return;
            }
            break;
        case VK_F3:
            if (selected) {
                finishPin();
                return;
            }
            break;
        case VK_LEFT:
        case VK_RIGHT:
        case VK_UP:
        case VK_DOWN: {
            const float step = shift ? 10.0f : 1.0f;
            const bs::Vec2 d{key == VK_LEFT ? -step : key == VK_RIGHT ? step : 0.0f,
                             key == VK_UP ? -step : key == VK_DOWN ? step : 0.0f};
            if (selected && picked_) {
                doc_.replace(*picked_, bs::translate(doc_.shapes()[*picked_], d));  // nudge the shape
            } else if (selected) {
                sel_ = bs::dragHandle(sel_, bs::Handle::Move, d, bounds());
            } else {  // pixel-precise picking
                POINT pt{};
                GetCursorPos(&pt);
                SetCursorPos(pt.x + static_cast<int>(d.x), pt.y + static_cast<int>(d.y));
            }
            break;
        }
        default:
            if (selected && key >= '1' && key <= '8') selectTool(static_cast<Tool>(key - '0'));
            break;
    }
    invalidate();
}

void CaptureWindow::onChar(wchar_t c) {
    if (edit_ == Edit::None) return;
    std::wstring& text = editShape_.text;
    switch (bs::classifyTextChar(c, keyDown(VK_CONTROL))) {
        case bs::TextKey::Backspace:
            if (!text.empty()) text.pop_back();
            if (!text.empty() && IS_HIGH_SURROGATE(text.back())) text.pop_back();
            break;
        case bs::TextKey::Newline:
            text += L'\n';
            break;
        case bs::TextKey::Commit:
            commitEdit();
            invalidate();
            return;
        case bs::TextKey::Insert:
            text += c;
            break;
        case bs::TextKey::Ignore:
            return;
    }
    gfx::measureShapeText(editShape_);
    placeImeWindow();
    invalidate();
}

void CaptureWindow::updateCursor() {
    LPCWSTR id = IDC_CROSS;
    if (mode_ == Mode::Selected) {
        const bool leaderBusy = tool_ == Tool::MLeader && leader_.phase() != bs::MLeaderTool::Phase::ArrowTip;
        const auto hit = leaderBusy || !bs::contains(sel_, cursor_)
                             ? std::nullopt
                             : bs::hitTestShapes(doc_.shapes(), cursor_, 5.0f * ui_, picked_);
        const bs::Handle h =
            dragHandle_ != bs::Handle::None ? dragHandle_ : bs::hitTestHandle(sel_, cursor_, 6.0f * ui_);
        switch (h) {
            case bs::Handle::NW:
            case bs::Handle::SE: id = IDC_SIZENWSE; break;
            case bs::Handle::NE:
            case bs::Handle::SW: id = IDC_SIZENESW; break;
            case bs::Handle::N:
            case bs::Handle::S: id = IDC_SIZENS; break;
            case bs::Handle::E:
            case bs::Handle::W: id = IDC_SIZEWE; break;
            case bs::Handle::Move:
                id = tool_ == Tool::None ? IDC_SIZEALL : tool_ == Tool::Text ? IDC_IBEAM : IDC_CROSS;
                break;
            case bs::Handle::None: id = IDC_ARROW; break;
        }
        if (pick_.active || hit) id = (pick_.active ? pick_.handle >= 0 : hit->handle >= 0) ? IDC_CROSS : IDC_SIZEALL;
        if (edit_ != Edit::None) id = IDC_IBEAM;
        if (buttonAt(cursor_) >= 0) id = IDC_HAND;
    }
    SetCursor(LoadCursorW(nullptr, id));
}

// ───────────────────────────── actions ─────────────────────────────

void CaptureWindow::selectTool(Tool t) {
    if (edit_ != Edit::None) commitEdit();
    leader_.cancel();
    drawing_ = false;
    picked_.reset();
    tool_ = tool_ == t ? Tool::None : t;
}

void CaptureWindow::run(Action a, int arg, int dir) {
    switch (a) {
        case Action::Tool: selectTool(static_cast<Tool>(arg)); break;
        case Action::Color: {
            colorIndex_ = std::clamp(arg, 0, kSwatchCount - 1);
            const bool frosted = colorIndex_ == kFrostedSwatch;
            const bs::Color c = frosted ? bs::Color{1, 1, 1, 1} : kPalette[colorIndex_];
            if (edit_ != Edit::None || picked_)
                applyStyle([c, frosted](bs::Shape& s) {
                    s.color = c;
                    s.material = frosted ? bs::Material::Frosted : bs::Material::Solid;
                });
            break;
        }
        case Action::Width: {
            widthIndex_ = std::clamp(arg, 0, kWidthCount - 1);
            const float w = kWidths[widthIndex_], f = kFontSizes[widthIndex_];
            if (edit_ != Edit::None || picked_)
                applyStyle([w, f](bs::Shape& s) {
                    s.strokeWidth = w;
                    s.fontSize = f;
                });
            break;
        }
        case Action::Finish:
        case Action::Head:
        case Action::Tail:
        case Action::Font:
        case Action::List:
        case Action::MosaicMode:
            setStyleValue(a, wrap(styleValue(a) + dir, optionCount(a)));
            break;
        case Action::Newline:
            if (edit_ != Edit::None) {
                editShape_.text += L'\n';
                gfx::measureShapeText(editShape_);
            }
            break;
        case Action::Bold:
            applyStyle([](bs::Shape& s) { s.textStyle.bold = !s.textStyle.bold; });
            break;
        case Action::Italic:
            applyStyle([](bs::Shape& s) { s.textStyle.italic = !s.textStyle.italic; });
            break;
        case Action::Plate:
            applyStyle([](bs::Shape& s) { s.textStyle.background = !s.textStyle.background; });
            break;
        case Action::Delete: deletePicked(); break;
        case Action::Undo:
            doc_.undo();
            picked_.reset();
            break;
        case Action::Redo:
            doc_.redo();
            picked_.reset();
            break;
        case Action::Pin: finishPin(); return;
        case Action::Save: finishSave(); return;
        case Action::Copy: finishCopy(); return;
        case Action::Cancel: close(); return;
    }
    if (edit_ != Edit::None) placeImeWindow();
    invalidate();
}

bool CaptureWindow::isMenuAction(Action a) {
    return a == Action::Finish || a == Action::Head || a == Action::Tail || a == Action::Font || a == Action::List ||
           a == Action::MosaicMode;
}

int CaptureWindow::optionCount(Action a) {
    switch (a) {
        case Action::Finish: return bs::kLineFinishCount;
        case Action::Head:
        case Action::Tail: return bs::kEndShapeCount;
        case Action::Font: return bs::kFontCount;
        case Action::List: return bs::kListStyleCount;
        case Action::MosaicMode: return 2;
        default: return 1;
    }
}

int CaptureWindow::styleValue(Action a) const {
    const bs::Shape& s = styleSource();
    switch (a) {
        case Action::Finish: return static_cast<int>(s.finish);
        case Action::Head: return static_cast<int>(s.head);
        case Action::Tail: return static_cast<int>(s.tail);
        case Action::Font: return s.textStyle.font;
        case Action::List: return static_cast<int>(s.textStyle.list);
        case Action::MosaicMode: return static_cast<int>(s.mosaic);
        default: return 0;
    }
}

void CaptureWindow::setStyleValue(Action a, int value) {
    const int v = std::clamp(value, 0, optionCount(a) - 1);
    applyStyle([a, v](bs::Shape& s) {
        switch (a) {
            case Action::Finish: s.finish = static_cast<bs::LineFinish>(v); break;
            case Action::Head: s.head = static_cast<bs::EndShape>(v); break;
            case Action::Tail: s.tail = static_cast<bs::EndShape>(v); break;
            case Action::Font: s.textStyle.font = v; break;
            case Action::List: s.textStyle.list = static_cast<bs::ListStyle>(v); break;
            case Action::MosaicMode: s.mosaic = static_cast<bs::MosaicMode>(v); break;
            default: break;
        }
    });
}

std::wstring CaptureWindow::optionLabel(Action a, int v) const {
    switch (a) {
        case Action::Finish: return bs::finishName(static_cast<bs::LineFinish>(v));
        case Action::Head:
        case Action::Tail: return bs::endShapeName(static_cast<bs::EndShape>(v));
        case Action::Font: return std::wstring(bs::fontLabel(v)) + L"  " + bs::fontFamily(v);
        case Action::List: return bs::listStyleName(static_cast<bs::ListStyle>(v));
        case Action::MosaicMode: return bs::mosaicModeName(static_cast<bs::MosaicMode>(v));
        default: return {};
    }
}

std::wstring CaptureWindow::tooltipFor(const Button& b) const {
    static const wchar_t* const tools[kToolCount] = {
        L"", L"矩形  1", L"椭圆  2", L"直线  3", L"箭头  4", L"画笔  5",
        L"文本  6 · Enter 换行 · Ctrl+Enter / Esc / 点击别处 完成", L"多重引线  7 · 依次点：箭头尖 → 折点 → 横肩 · Shift 取消 45° 吸附",
        L"马赛克 / 毛玻璃  8"};
    static const wchar_t* const widths[kWidthCount] = {L"细", L"中", L"粗"};
    const std::wstring more = L"  ·  点击展开，右键切换";
    switch (b.action) {
        case Action::Tool: return tools[std::clamp(b.arg, 0, kToolCount - 1)];
        case Action::Color: return kSwatchNames[std::clamp(b.arg, 0, kSwatchCount - 1)];
        case Action::Width: return std::wstring(L"线宽 / 字号：") + widths[std::clamp(b.arg, 0, kWidthCount - 1)];
        case Action::Undo: return L"撤回  Ctrl+Z";
        case Action::Redo: return L"重做  Ctrl+Y";
        case Action::Cancel: return L"退出  Esc";
        case Action::Pin: return L"贴到桌面  F3 · 双击贴图即关闭";
        case Action::Save: return L"保存 PNG  Ctrl+S";
        case Action::Copy: return L"复制并完成  Enter / Ctrl+C / 双击选区";
        case Action::Finish: return L"线型：" + optionLabel(b.action, styleValue(b.action)) + more;
        case Action::Head: return L"头部（箭头端）：" + optionLabel(b.action, styleValue(b.action)) + more;
        case Action::Tail: return L"尾部：" + optionLabel(b.action, styleValue(b.action)) + more;
        case Action::Font: return L"字体：" + optionLabel(b.action, styleValue(b.action)) + more;
        case Action::List: return L"列表：" + optionLabel(b.action, styleValue(b.action)) + more + L"（行首输入 * 也会变圆点）";
        case Action::MosaicMode: return L"马赛克样式：" + optionLabel(b.action, styleValue(b.action)) + more;
        case Action::Bold: return L"粗体  Ctrl+B";
        case Action::Italic: return L"斜体  Ctrl+I";
        case Action::Plate: return L"文字底衬";
        case Action::Delete: return L"删除选中标注  Delete";
        case Action::Newline: return L"换行  Enter";
    }
    return {};
}

void CaptureWindow::openMenu(const Button& b) {
    const float row = 30.0f * ui_, pad = 6.0f * ui_, width = 230.0f * ui_;
    const float height = static_cast<float>(optionCount(b.action)) * row + 2.0f * pad;
    menu_ = {true, b.action, bs::placePopup(b.rect, width, height, monitorFor(sel_), 6.0f * ui_), -1};
    tipButton_ = -1;
}

int CaptureWindow::menuRowAt(bs::Vec2 p) const {
    if (!menu_.open || !bs::contains(menu_.rect, p)) return -1;
    const float row = 30.0f * ui_, pad = 6.0f * ui_;
    const int i = static_cast<int>(std::floor((p.y - menu_.rect.top - pad) / row));
    return i >= 0 && i < optionCount(menu_.action) ? i : -1;
}

void CaptureWindow::deletePicked() {
    if (picked_ && doc_.remove(*picked_)) picked_.reset();
}

gfx::Image CaptureWindow::composite() {
    if (edit_ != Edit::None) commitEdit();
    const bs::Rect r{std::round(sel_.left), std::round(sel_.top), std::round(sel_.right), std::round(sel_.bottom)};
    return gfx::renderComposite(grab_.image, r, doc_.shapes());
}

void CaptureWindow::finishCopy() {
    gfx::copyImageToClipboard(hwnd_, composite());
    close();
}

void CaptureWindow::finishSave() {
    if (gfx::saveWithDialog(hwnd_, composite())) close();
}

void CaptureWindow::finishPin() {
    gfx::Image img = composite();
    const POINT at{grab_.virtualRect.left + static_cast<LONG>(std::round(sel_.left)),
                   grab_.virtualRect.top + static_cast<LONG>(std::round(sel_.top))};
    auto pin = cb_.pin;
    close();
    if (pin && !img.empty()) pin(std::move(img), at);
}

void CaptureWindow::resetToPicking() {
    mode_ = Mode::Picking;
    sel_ = {};
    doc_.clear();
    drawing_ = false;
    edit_ = Edit::None;
    editIndex_.reset();
    picked_.reset();
    pick_.active = false;
    leader_.cancel();
    tool_ = Tool::None;
    dragHandle_ = bs::Handle::None;
    buttons_.clear();
    menu_.open = false;
    tipButton_ = -1;
    hover_ = bs::pickWindowRect(grab_.windows, cursor_);
    invalidate();
}

// ───────────────────────────── text editing ─────────────────────────────

void CaptureWindow::beginEdit(Edit kind, bs::Vec2 at) {
    edit_ = kind;
    editIndex_.reset();
    picked_.reset();
    if (kind == Edit::Text) {
        editShape_ = styledShape(Tool::Text);
        gfx::measureShapeText(editShape_);
        editShape_.a = at - bs::Vec2{0.0f, editShape_.textSize.y * 0.5f};
    } else {
        editShape_ = styledShape(Tool::MLeader);
        editShape_.leader = leader_.input();
    }
    editShape_.text.clear();
    gfx::measureShapeText(editShape_);
    placeImeWindow();
}

void CaptureWindow::beginEditExisting(std::size_t index) {
    const bs::Shape& s = doc_.shapes()[index];
    edit_ = s.kind == bs::ShapeKind::Text ? Edit::Text : Edit::LeaderLabel;
    editIndex_ = index;
    editShape_ = s;
    picked_ = index;
    pick_.active = false;
    placeImeWindow();
}

void CaptureWindow::commitEdit() {
    if (edit_ == Edit::None) return;
    gfx::measureShapeText(editShape_);
    if (editIndex_) {
        if (editShape_.kind == bs::ShapeKind::Text && editShape_.text.empty())
            doc_.remove(*editIndex_);  // emptied label = delete
        else
            doc_.replace(*editIndex_, editShape_);
    } else {
        doc_.add(editShape_);  // empty Text is dropped; a bare leader is kept
    }
    if (edit_ == Edit::LeaderLabel) leader_.cancel();
    edit_ = Edit::None;
    editIndex_.reset();
    picked_.reset();
}

void CaptureWindow::cancelEdit() {
    if (edit_ == Edit::LeaderLabel) leader_.cancel();
    edit_ = Edit::None;
    editIndex_.reset();
}

void CaptureWindow::placeImeWindow() {
    if (edit_ == Edit::None || !hwnd_) return;
    const bs::Vec2 o = gfx::textOrigin(editShape_);
    const D2D1_RECT_F caret = gfx::caretRect(editShape_);
    if (HIMC imc = ImmGetContext(hwnd_)) {
        COMPOSITIONFORM cf{};
        cf.dwStyle = CFS_POINT;
        cf.ptCurrentPos = {static_cast<LONG>(o.x + caret.left), static_cast<LONG>(o.y + caret.top)};
        ImmSetCompositionWindow(imc, &cf);
        ImmReleaseContext(hwnd_, imc);
    }
}

std::optional<bs::Shape> CaptureWindow::previewShape() const {
    if (drawing_) return draft_;
    if (edit_ != Edit::None) return editShape_;
    if (pick_.active) return live_;
    if (tool_ == Tool::MLeader && leader_.phase() != bs::MLeaderTool::Phase::ArrowTip) {
        bs::Shape s = styledShape(Tool::MLeader);
        s.leader = leader_.input();
        return s;
    }
    return std::nullopt;
}

// ───────────────────────────── rendering ─────────────────────────────

bool CaptureWindow::ensureTarget() {
    if (rt_) return true;
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    const auto props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
        96.0f, 96.0f);
    const auto hwndProps = D2D1::HwndRenderTargetProperties(
        hwnd_, D2D1::SizeU(static_cast<UINT32>(rc.right), static_cast<UINT32>(rc.bottom)));
    auto& f = gfx::factories();
    if (FAILED(f.d2d->CreateHwndRenderTarget(props, hwndProps, rt_.GetAddressOf()))) return false;
    shotBitmap_ = gfx::makeBitmap(rt_.Get(), grab_.image);
    rt_->CreateSolidColorBrush(kOnSurface, brush_.ReleaseAndGetAddressOf());
    if (!dashStroke_)
        f.d2d->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT,
                                                             D2D1_CAP_STYLE_FLAT, D2D1_LINE_JOIN_MITER, 10.0f,
                                                             D2D1_DASH_STYLE_DASH),
                                 nullptr, 0, dashStroke_.GetAddressOf());
    const auto N = DWRITE_FONT_WEIGHT_NORMAL;
    const auto upright = DWRITE_FONT_STYLE_NORMAL;
    if (!labelFont_) labelFont_ = makeFormat(L"Segoe UI", 12.0f * ui_, N, upright, false);
    if (!iconFont_) iconFont_ = makeFormat(L"Segoe MDL2 Assets", 16.0f * ui_, N, upright, true);
    if (!glyphFont_) glyphFont_ = makeFormat(L"Segoe UI", 17.0f * ui_, DWRITE_FONT_WEIGHT_BOLD, upright, true);
    if (!italicFont_) italicFont_ = makeFormat(L"Segoe UI", 17.0f * ui_, N, DWRITE_FONT_STYLE_ITALIC, true);
    for (int i = 0; i < bs::kFontCount; ++i) {
        auto& ff = fontFormats_[static_cast<std::size_t>(i)];
        if (!ff) ff = makeFormat(bs::fontFamily(i), 15.0f * ui_, N, upright, true);
    }
    return shotBitmap_ && brush_ && dashStroke_ && labelFont_ && iconFont_ && glyphFont_ && italicFont_;
}

bs::Rect CaptureWindow::monitorFor(const bs::Rect& r) const {
    const bs::Vec2 c{(r.left + r.right) * 0.5f, (r.top + r.bottom) * 0.5f};
    for (const auto& m : grab_.monitors)
        if (bs::contains(m, c)) return m;
    return bounds();
}

void CaptureWindow::layoutToolbar() {
    struct Item {
        Action a;
        int arg;
        bool gapBefore;
    };
    const float size = 34.0f * ui_, pad = 6.0f * ui_, gap = 12.0f * ui_;
    const auto place = [&](const std::vector<Item>& items, float& width) {
        std::vector<Button> out;
        float x = pad;
        for (const auto& it : items) {
            if (it.gapBefore) x += gap;
            out.push_back({{x, pad, x + size, pad + size}, it.a, it.arg});
            x += size;
        }
        width = x + pad;
        return out;
    };
    const auto offset = [](std::vector<Button>& bs_, const bs::Rect& r) {
        for (auto& b : bs_) {
            b.rect.left += r.left;
            b.rect.right += r.left;
            b.rect.top += r.top;
            b.rect.bottom += r.top;
        }
    };

    std::vector<Item> main;
    for (int t = 1; t < kToolCount; ++t) main.push_back({Action::Tool, t, false});
    for (int c = 0; c < kSwatchCount; ++c) main.push_back({Action::Color, c, c == 0});
    for (int w = 0; w < kWidthCount; ++w) main.push_back({Action::Width, w, false});
    main.push_back({Action::Undo, 0, true});
    main.push_back({Action::Redo, 0, false});
    main.push_back({Action::Cancel, 0, true});
    main.push_back({Action::Pin, 0, false});
    main.push_back({Action::Save, 0, false});
    main.push_back({Action::Copy, 0, false});

    float mainW = 0;
    buttons_ = place(main, mainW);
    const float h = size + 2.0f * pad;
    const bs::Rect monitor = monitorFor(sel_);
    toolbarRect_ = bs::placeToolbar(sel_, mainW, h, monitor, 8.0f * ui_);
    offset(buttons_, toolbarRect_);

    // Contextual CMF bar for the active tool / picked shape / label being edited.
    std::vector<Item> sub;
    if (const auto kind = contextKind()) {
        const bool box = *kind == bs::ShapeKind::Rect || *kind == bs::ShapeKind::Ellipse || *kind == bs::ShapeKind::Pen;
        if (box || hasCaps(*kind)) sub.push_back({Action::Finish, 0, false});
        if (hasCaps(*kind)) {
            sub.push_back({Action::Head, 0, false});
            sub.push_back({Action::Tail, 0, false});
        }
        if (hasText(*kind)) {
            const bool g = !sub.empty();
            sub.push_back({Action::Font, 0, g});
            sub.push_back({Action::Bold, 0, false});
            sub.push_back({Action::Italic, 0, false});
            sub.push_back({Action::List, 0, false});
            sub.push_back({Action::Plate, 0, false});
            if (edit_ != Edit::None) sub.push_back({Action::Newline, 0, false});
        }
        if (*kind == bs::ShapeKind::Mosaic) sub.push_back({Action::MosaicMode, 0, false});
    }
    if (picked_ && edit_ == Edit::None) sub.push_back({Action::Delete, 0, !sub.empty()});
    subRect_ = {};
    if (sub.empty()) return;
    float subW = 0;
    std::vector<Button> subButtons = place(sub, subW);
    const float g = 6.0f * ui_;
    const bool below = toolbarRect_.top >= sel_.bottom && toolbarRect_.bottom + g + h <= monitor.bottom;
    const float top = below ? toolbarRect_.bottom + g : toolbarRect_.top - g - h;
    const float left = std::max(monitor.left, toolbarRect_.right - subW);
    subRect_ = {left, std::clamp(top, monitor.top, monitor.bottom - h), left + subW, 0};
    subRect_.bottom = subRect_.top + h;
    offset(subButtons, subRect_);
    buttons_.insert(buttons_.end(), subButtons.begin(), subButtons.end());
}

void CaptureWindow::paint() {
    if (!hwnd_ || !ensureTarget()) return;
    const float elapsed = animating_ ? animElapsedMs() : 1e9f;
    rt_->BeginDraw();
    rt_->Clear(D2D1::ColorF(0, 0, 0, 1));
    // Summon jolt: the whole frozen frame kicks and settles.
    rt_->SetTransform(animating_ ? D2D1::Matrix3x2F::Translation(bs::shakeOffset(elapsed),
                                                                 bs::shakeOffset(elapsed + 12.0f, 1.5f))
                                 : D2D1::Matrix3x2F::Identity());
    rt_->DrawBitmap(shotBitmap_.Get(), gfx::toD2D(bounds()), 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);

    const bs::Rect active = activeRect();
    paintDim(active);
    if (!bs::isEmpty(active)) {
        if (mode_ == Mode::Selected) {
            rt_->PushAxisAlignedClip(gfx::toD2D(active), D2D1_ANTIALIAS_MODE_ALIASED);
            gfx::drawShapes(rt_.Get(), doc_.shapes(), &grab_.image, hiddenIndex());
            if (auto p = previewShape()) gfx::drawShape(rt_.Get(), *p, &grab_.image);
            if (edit_ != Edit::None) {
                const bs::Vec2 o = gfx::textOrigin(editShape_);
                const D2D1_RECT_F c = gfx::caretRect(editShape_);
                brush_->SetColor(kAccent);
                rt_->DrawRectangle(D2D1::RectF(o.x - 4, o.y - 2, o.x + std::max(editShape_.textSize.x, 8.0f) + 4,
                                               o.y + editShape_.textSize.y + 2),
                                   brush_.Get(), 1.0f, dashStroke_.Get());
                brush_->SetColor(gfx::toD2D(editShape_.color));
                rt_->FillRectangle(D2D1::RectF(o.x + c.left, o.y + c.top, o.x + c.right, o.y + c.bottom), brush_.Get());
            }
            rt_->PopAxisAlignedClip();
            paintPicked();
        }
        paintSelectionChrome(active);
    }
    rt_->SetTransform(D2D1::Matrix3x2F::Identity());
    if (mode_ == Mode::Selected && dragHandle_ == bs::Handle::None && !drawing_ && !pick_.active) {
        paintToolbar();
        paintMenu();
        paintTooltip();
    }
    if (mode_ != Mode::Selected) paintMagnifier();
    if (animating_) paintAnimation(elapsed);

    if (rt_->EndDraw() == static_cast<HRESULT>(D2DERR_RECREATE_TARGET)) {
        rt_.Reset();
        shotBitmap_.Reset();
        brush_.Reset();
        invalidate();
    }
}

void CaptureWindow::paintAnimation(float elapsedMs) {
    const float maxR = 220.0f * ui_;
    for (const auto& ring : bs::rippleFrame(elapsedMs, maxR)) {
        if (ring.opacity <= 0.0f) continue;
        const auto e = D2D1::Ellipse(gfx::toD2D(cursor_), ring.radius, ring.radius);
        brush_->SetColor(D2D1::ColorF(kAccent.r, kAccent.g, kAccent.b, ring.opacity * 0.18f));
        rt_->FillEllipse(e, brush_.Get());
        brush_->SetColor(D2D1::ColorF(kAccent.r, kAccent.g, kAccent.b, ring.opacity));
        rt_->DrawEllipse(e, brush_.Get(), 2.5f * ui_);
    }
}

void CaptureWindow::paintDim(const bs::Rect& s) {
    brush_->SetColor(kDim);
    const bs::Rect b = bounds();
    if (bs::isEmpty(s)) {
        rt_->FillRectangle(gfx::toD2D(b), brush_.Get());
        return;
    }
    rt_->FillRectangle(D2D1::RectF(b.left, b.top, b.right, s.top), brush_.Get());
    rt_->FillRectangle(D2D1::RectF(b.left, s.bottom, b.right, b.bottom), brush_.Get());
    rt_->FillRectangle(D2D1::RectF(b.left, s.top, s.left, s.bottom), brush_.Get());
    rt_->FillRectangle(D2D1::RectF(s.right, s.top, b.right, s.bottom), brush_.Get());
}

void CaptureWindow::paintAngle(bs::Vec2 vertex, bs::Vec2 target) {
    const bs::Vec2 d = target - vertex;
    const float len = bs::length(d);
    if (len < 12.0f * ui_) return;
    const float deg = bs::inclinationDeg(vertex, target);
    const float sx = d.x >= 0 ? 1.0f : -1.0f;
    const float r = std::min(26.0f * ui_, len * 0.6f);
    const bs::Vec2 u = bs::normalize(d);
    const bs::Vec2 h{sx, 0.0f};

    // Horizontal reference + arc from it to the segment.
    brush_->SetColor(D2D1::ColorF(1, 1, 1, 0.75f));
    rt_->DrawLine(gfx::toD2D(vertex), gfx::toD2D(vertex + h * (r + 10.0f * ui_)), brush_.Get(), 1.0f, dashStroke_.Get());
    if (deg > 0.5f) {
        ComPtr<ID2D1PathGeometry> path;
        ComPtr<ID2D1GeometrySink> sink;
        if (SUCCEEDED(gfx::factories().d2d->CreatePathGeometry(path.GetAddressOf())) &&
            SUCCEEDED(path->Open(sink.GetAddressOf()))) {
            sink->BeginFigure(gfx::toD2D(vertex + h * r), D2D1_FIGURE_BEGIN_HOLLOW);
            // In y-down space a positive cross product means a clockwise turn.
            const bool cw = sx * u.y > 0.0f;
            sink->AddArc(D2D1::ArcSegment(gfx::toD2D(vertex + u * r), D2D1::SizeF(r, r), 0.0f,
                                          cw ? D2D1_SWEEP_DIRECTION_CLOCKWISE : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
                                          D2D1_ARC_SIZE_SMALL));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            if (SUCCEEDED(sink->Close())) {
                brush_->SetColor(kAccent);
                rt_->DrawGeometry(path.Get(), brush_.Get(), 1.5f * ui_);
            }
        }
    }

    wchar_t label[16];
    if (std::fabs(deg - std::round(deg)) < 0.05f)
        std::swprintf(label, 16, L"%.0f°", static_cast<double>(deg));
    else
        std::swprintf(label, 16, L"%.1f°", static_cast<double>(deg));
    const bs::Vec2 bis = bs::normalize(h + u);
    const bs::Vec2 at = vertex + (bis == bs::Vec2{} ? bs::Vec2{0, -1} : bis) * (r + 16.0f * ui_);
    const float w = 44.0f * ui_, hh = 20.0f * ui_;
    const auto pill = D2D1::RoundedRect(D2D1::RectF(at.x - w / 2, at.y - hh / 2, at.x + w / 2, at.y + hh / 2), hh / 2, hh / 2);
    brush_->SetColor(kSurface);
    rt_->FillRoundedRectangle(pill, brush_.Get());
    brush_->SetColor(kOnSurface);
    labelFont_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    labelFont_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    rt_->DrawText(label, static_cast<UINT32>(std::wcslen(label)), labelFont_.Get(), pill.rect, brush_.Get());
    labelFont_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    labelFont_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
}

void CaptureWindow::paintPicked() {
    // Angle readouts for whatever leader / segment is being shaped right now.
    std::optional<bs::Shape> s;
    if (pick_.active) s = live_;
    else if (drawing_) s = draft_;
    else if (edit_ != Edit::None) s = editShape_;
    else if (tool_ == Tool::MLeader && leader_.phase() != bs::MLeaderTool::Phase::ArrowTip) s = previewShape();
    else if (picked_ && *picked_ < doc_.shapes().size()) s = doc_.shapes()[*picked_];
    if (!s) return;
    if (s->kind == bs::ShapeKind::MLeader) paintAngle(s->leader.kink, s->leader.arrowTip);
    if (s->kind == bs::ShapeKind::Line || s->kind == bs::ShapeKind::Arrow) paintAngle(s->a, s->b);

    if (!picked_ || drawing_) return;
    // Joints of the picked shape (or its outline when it only moves as a whole).
    const auto pts = bs::controlPoints(*s);
    if (pts.empty() || s->kind == bs::ShapeKind::Mosaic) {
        const bs::Rect b = bs::shapeBounds(*s);
        brush_->SetColor(kAccent);
        rt_->DrawRectangle(D2D1::RectF(b.left - 4, b.top - 4, b.right + 4, b.bottom + 4), brush_.Get(), 1.0f,
                           dashStroke_.Get());
    }
    const float r = 4.5f * ui_;
    for (const auto& p : pts) {
        const auto e = D2D1::Ellipse(gfx::toD2D(p), r, r);
        brush_->SetColor(D2D1::ColorF(D2D1::ColorF::White));
        rt_->FillEllipse(e, brush_.Get());
        brush_->SetColor(kAccent);
        rt_->DrawEllipse(e, brush_.Get(), 1.8f * ui_);
    }
}

void CaptureWindow::paintSelectionChrome(const bs::Rect& s) {
    const bool snapping = mode_ == Mode::Picking || (mode_ == Mode::Dragging && bs::isClick(dragStart_, cursor_));
    brush_->SetColor(kAccent);
    const float w = (snapping ? 3.0f : 1.5f) * ui_;
    rt_->DrawRectangle(D2D1::RectF(s.left + w / 2, s.top + w / 2, s.right - w / 2, s.bottom - w / 2), brush_.Get(), w);

    if (mode_ == Mode::Selected) {
        const float r = 4.0f * ui_;
        const float xs[] = {s.left, (s.left + s.right) / 2, s.right};
        const float ys[] = {s.top, (s.top + s.bottom) / 2, s.bottom};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) {
                if (i == 1 && j == 1) continue;
                const auto e = D2D1::Ellipse(D2D1::Point2F(xs[i], ys[j]), r, r);
                brush_->SetColor(D2D1::ColorF(D2D1::ColorF::White));
                rt_->FillEllipse(e, brush_.Get());
                brush_->SetColor(kAccent);
                rt_->DrawEllipse(e, brush_.Get(), 1.5f * ui_);
            }
    }

    wchar_t label[48];
    std::swprintf(label, 48, L"%d × %d", static_cast<int>(std::round(s.width())), static_cast<int>(std::round(s.height())));
    const float lw = 92.0f * ui_, lh = 22.0f * ui_;
    const bool above = s.top - lh - 6.0f * ui_ >= 0;
    const float ly = above ? s.top - lh - 6.0f * ui_ : s.top + 6.0f * ui_;
    const float lx = s.left + (above ? 0.0f : 6.0f * ui_);
    const auto pill = D2D1::RoundedRect(D2D1::RectF(lx, ly, lx + lw, ly + lh), lh / 2, lh / 2);
    brush_->SetColor(kSurface);
    rt_->FillRoundedRectangle(pill, brush_.Get());
    brush_->SetColor(kOnSurface);
    labelFont_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    labelFont_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    rt_->DrawText(label, static_cast<UINT32>(std::wcslen(label)), labelFont_.Get(), pill.rect, brush_.Get());
    labelFont_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    labelFont_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
}

bool CaptureWindow::isToggled(const Button& b) const {
    const bs::Shape& s = styleSource();
    switch (b.action) {
        case Action::Tool: return static_cast<int>(tool_) == b.arg;
        case Action::Bold: return s.textStyle.bold;
        case Action::Italic: return s.textStyle.italic;
        case Action::Plate: return s.textStyle.background;
        case Action::List: return s.textStyle.list != bs::ListStyle::None;
        default: return false;
    }
}

void CaptureWindow::paintToolbar() {
    if (buttons_.empty()) return;
    const float radius = 12.0f * ui_;
    for (const bs::Rect* bar : {&toolbarRect_, &subRect_}) {
        if (bs::isEmpty(*bar)) continue;
        // Soft elevation: a few widening translucent layers under the surface.
        for (int i = 3; i >= 1; --i) {
            const float g = static_cast<float>(i) * 2.0f * ui_;
            brush_->SetColor(D2D1::ColorF(0, 0, 0, 0.10f));
            rt_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(bar->left - g, bar->top - g + 2 * ui_, bar->right + g,
                                                                    bar->bottom + g + 2 * ui_),
                                                        radius + g, radius + g),
                                      brush_.Get());
        }
        brush_->SetColor(kSurface);
        rt_->FillRoundedRectangle(D2D1::RoundedRect(gfx::toD2D(*bar), radius, radius), brush_.Get());
    }

    for (std::size_t i = 0; i < buttons_.size(); ++i) {
        const Button& b = buttons_[i];
        const auto bg = D2D1::RoundedRect(D2D1::RectF(b.rect.left + 2, b.rect.top + 2, b.rect.right - 2, b.rect.bottom - 2),
                                          8.0f * ui_, 8.0f * ui_);
        D2D1_COLOR_F fg = kOnSurface;
        if (b.action == Action::Copy) {
            brush_->SetColor(kAccent);
            rt_->FillRoundedRectangle(bg, brush_.Get());
            fg = D2D1::ColorF(D2D1::ColorF::White);
        } else if (isToggled(b)) {
            brush_->SetColor(kAccentSoft);
            rt_->FillRoundedRectangle(bg, brush_.Get());
        }
        if (static_cast<int>(i) == hoverButton_ && b.action != Action::Copy) {
            brush_->SetColor(kHover);
            rt_->FillRoundedRectangle(bg, brush_.Get());
        }
        if ((b.action == Action::Undo && !doc_.canUndo()) || (b.action == Action::Redo && !doc_.canRedo())) fg = kMuted;
        paintButtonIcon(b, fg);
    }
}

void CaptureWindow::paintCapSample(D2D1_POINT_2F c, bs::EndShape shape, bool atRight) {
    const float u = ui_;
    const bs::Vec2 l{c.x - 10 * u, c.y}, r{c.x + 10 * u, c.y};
    const bs::Vec2 tip = atRight ? r : l, from = atRight ? l : r;
    const auto cap = bs::makeEndCap(shape, from, tip, 0.4f * u);
    rt_->DrawLine(gfx::toD2D(from), gfx::toD2D(cap.shaftEnd), brush_.Get(), 1.5f * u);
    if (cap.shape == bs::EndShape::Dot) {
        rt_->FillEllipse(D2D1::Ellipse(gfx::toD2D(cap.center), cap.radius, cap.radius), brush_.Get());
    } else if (cap.outline.size() >= 2) {
        ComPtr<ID2D1PathGeometry> path;
        ComPtr<ID2D1GeometrySink> sink;
        if (SUCCEEDED(gfx::factories().d2d->CreatePathGeometry(path.GetAddressOf())) &&
            SUCCEEDED(path->Open(sink.GetAddressOf()))) {
            sink->BeginFigure(gfx::toD2D(cap.outline[0]), cap.filled ? D2D1_FIGURE_BEGIN_FILLED : D2D1_FIGURE_BEGIN_HOLLOW);
            for (std::size_t i = 1; i < cap.outline.size(); ++i) sink->AddLine(gfx::toD2D(cap.outline[i]));
            sink->EndFigure(cap.closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
            if (SUCCEEDED(sink->Close())) {
                if (cap.filled) rt_->FillGeometry(path.Get(), brush_.Get());
                else rt_->DrawGeometry(path.Get(), brush_.Get(), 1.5f * u);
            }
        }
    }
    // Small marker on the far end tells head (●→) from tail (←●) apart.
    rt_->FillEllipse(D2D1::Ellipse(gfx::toD2D(from), 1.6f * u, 1.6f * u), brush_.Get());
}

void CaptureWindow::paintButtonIcon(const Button& b, D2D1_COLOR_F fg) {
    const float u = ui_;
    const D2D1_POINT_2F c = D2D1::Point2F((b.rect.left + b.rect.right) / 2, (b.rect.top + b.rect.bottom) / 2);
    const float sw = 1.6f * u;
    brush_->SetColor(fg);
    auto glyph = [&](const wchar_t* g, IDWriteTextFormat* f) {
        rt_->DrawText(g, static_cast<UINT32>(std::wcslen(g)), f, gfx::toD2D(b.rect), brush_.Get());
    };
    auto line = [&](float x0, float y0, float x1, float y1) {
        rt_->DrawLine(D2D1::Point2F(c.x + x0 * u, c.y + y0 * u), D2D1::Point2F(c.x + x1 * u, c.y + y1 * u), brush_.Get(), sw);
    };
    switch (b.action) {
        case Action::Tool:
            switch (static_cast<Tool>(b.arg)) {
                case Tool::Rect:
                    rt_->DrawRoundedRectangle(
                        D2D1::RoundedRect(D2D1::RectF(c.x - 8 * u, c.y - 6 * u, c.x + 8 * u, c.y + 6 * u), 2 * u, 2 * u),
                        brush_.Get(), sw);
                    break;
                case Tool::Ellipse:
                    rt_->DrawEllipse(D2D1::Ellipse(c, 8 * u, 6.5f * u), brush_.Get(), sw);
                    break;
                case Tool::Line:
                    line(-7, 7, 7, -7);
                    break;
                case Tool::Arrow:
                    line(-7, 7, 7, -7);
                    line(7, -7, 1, -7);
                    line(7, -7, 7, -1);
                    break;
                case Tool::Pen: {
                    ComPtr<ID2D1PathGeometry> path;
                    ComPtr<ID2D1GeometrySink> sink;
                    if (SUCCEEDED(gfx::factories().d2d->CreatePathGeometry(path.GetAddressOf())) &&
                        SUCCEEDED(path->Open(sink.GetAddressOf()))) {
                        sink->BeginFigure(D2D1::Point2F(c.x - 8 * u, c.y + 3 * u), D2D1_FIGURE_BEGIN_HOLLOW);
                        sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(c.x - 4 * u, c.y - 9 * u),
                                                            D2D1::Point2F(c.x + 1 * u, c.y + 9 * u),
                                                            D2D1::Point2F(c.x + 8 * u, c.y - 4 * u)));
                        sink->EndFigure(D2D1_FIGURE_END_OPEN);
                        if (SUCCEEDED(sink->Close())) rt_->DrawGeometry(path.Get(), brush_.Get(), sw);
                    }
                    break;
                }
                case Tool::Text:
                    glyph(L"T", glyphFont_.Get());
                    break;
                case Tool::MLeader:
                    line(-8, 7, -2, -3);
                    line(-2, -3, 9, -3);
                    rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(c.x - 8 * u, c.y + 7 * u), 2.2f * u, 2.2f * u), brush_.Get());
                    break;
                case Tool::Mosaic:
                    for (int i = 0; i < 3; ++i)
                        for (int j = 0; j < 3; ++j)
                            if ((i + j) % 2 == 0)
                                rt_->FillRectangle(D2D1::RectF(c.x + (static_cast<float>(i) * 5 - 7.5f) * u,
                                                               c.y + (static_cast<float>(j) * 5 - 7.5f) * u,
                                                               c.x + (static_cast<float>(i) * 5 - 2.5f) * u,
                                                               c.y + (static_cast<float>(j) * 5 - 2.5f) * u),
                                                   brush_.Get());
                    break;
                case Tool::None:
                    break;
            }
            break;
        case Action::Color: {
            if (b.arg == kFrostedSwatch) {  // glass bead: translucent layers + highlight
                for (int i = 3; i >= 1; --i) {
                    brush_->SetColor(D2D1::ColorF(0.85f, 0.90f, 1.0f, 0.22f));
                    rt_->FillEllipse(D2D1::Ellipse(c, (4.0f + static_cast<float>(i)) * u, (4.0f + static_cast<float>(i)) * u),
                                     brush_.Get());
                }
                brush_->SetColor(D2D1::ColorF(1, 1, 1, 0.85f));
                rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(c.x - 2.5f * u, c.y - 2.5f * u), 1.8f * u, 1.8f * u),
                                 brush_.Get());
                rt_->DrawEllipse(D2D1::Ellipse(c, 7 * u, 7 * u), brush_.Get(), 1.0f);
                if (b.arg == colorIndex_) {
                    brush_->SetColor(kOnSurface);
                    rt_->DrawEllipse(D2D1::Ellipse(c, 10.5f * u, 10.5f * u), brush_.Get(), 1.5f * u);
                }
                break;
            }
            brush_->SetColor(gfx::toD2D(kPalette[b.arg]));
            rt_->FillEllipse(D2D1::Ellipse(c, 7 * u, 7 * u), brush_.Get());
            brush_->SetColor(kMuted);
            rt_->DrawEllipse(D2D1::Ellipse(c, 7 * u, 7 * u), brush_.Get(), 1.0f);
            if (b.arg == colorIndex_) {
                brush_->SetColor(kOnSurface);
                rt_->DrawEllipse(D2D1::Ellipse(c, 10.5f * u, 10.5f * u), brush_.Get(), 1.5f * u);
            }
            break;
        }
        case Action::Width: {
            const float radii[] = {2.5f, 4.0f, 6.0f};
            rt_->FillEllipse(D2D1::Ellipse(c, radii[b.arg] * u, radii[b.arg] * u), brush_.Get());
            if (b.arg == widthIndex_) {
                brush_->SetColor(kAccent);
                rt_->DrawEllipse(D2D1::Ellipse(c, 10.5f * u, 10.5f * u), brush_.Get(), 1.5f * u);
            }
            break;
        }
        case Action::Finish:
        case Action::Head:
        case Action::Tail:
        case Action::Font:
        case Action::List:
        case Action::MosaicMode:
            paintOptionIcon(b.action, styleValue(b.action), c, fg);
            // Tiny tick marks the buttons that open a dropdown.
            rt_->FillRectangle(D2D1::RectF(b.rect.right - 8 * u, b.rect.bottom - 6 * u, b.rect.right - 4 * u,
                                           b.rect.bottom - 5 * u),
                               brush_.Get());
            break;
        case Action::Newline:  // ↵
            line(6, -6, 6, 2);
            line(6, 2, -6, 2);
            line(-6, 2, -2, -2);
            line(-6, 2, -2, 6);
            break;
        case Action::Bold: glyph(L"B", glyphFont_.Get()); break;
        case Action::Italic: glyph(L"I", italicFont_.Get()); break;
        case Action::Plate:
            brush_->SetColor(D2D1::ColorF(fg.r, fg.g, fg.b, 0.35f));
            rt_->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(c.x - 10 * u, c.y - 8 * u, c.x + 10 * u, c.y + 8 * u),
                                                        3 * u, 3 * u),
                                      brush_.Get());
            brush_->SetColor(fg);
            glyph(L"A", glyphFont_.Get());
            break;
        case Action::Delete: glyph(L"\uE74D", iconFont_.Get()); break;
        case Action::Undo: glyph(L"\uE7A7", iconFont_.Get()); break;
        case Action::Redo: glyph(L"\uE7A6", iconFont_.Get()); break;
        case Action::Pin: glyph(L"\uE718", iconFont_.Get()); break;
        case Action::Save: glyph(L"\uE74E", iconFont_.Get()); break;
        case Action::Copy: glyph(L"\uE8C8", iconFont_.Get()); break;
        case Action::Cancel: glyph(L"\uE711", iconFont_.Get()); break;
    }
}

void CaptureWindow::paintOptionIcon(Action a, int v, D2D1_POINT_2F c, D2D1_COLOR_F fg) {
    const float u = ui_;
    brush_->SetColor(fg);
    switch (a) {
        case Action::Finish: {
            ComPtr<ID2D1StrokeStyle> st;
            const D2D1_DASH_STYLE dash[] = {D2D1_DASH_STYLE_SOLID, D2D1_DASH_STYLE_DASH, D2D1_DASH_STYLE_DOT,
                                            D2D1_DASH_STYLE_DASH_DOT};
            gfx::factories().d2d->CreateStrokeStyle(
                D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_FLAT, D2D1_CAP_STYLE_ROUND,
                                            D2D1_LINE_JOIN_ROUND, 10.0f, dash[std::clamp(v, 0, 3)]),
                nullptr, 0, st.GetAddressOf());
            rt_->DrawLine(D2D1::Point2F(c.x - 11 * u, c.y), D2D1::Point2F(c.x + 11 * u, c.y), brush_.Get(), 2.0f * u, st.Get());
            break;
        }
        case Action::Head: paintCapSample(c, static_cast<bs::EndShape>(v), true); break;
        case Action::Tail: paintCapSample(c, static_cast<bs::EndShape>(v), false); break;
        case Action::Font: {
            const auto& f = fontFormats_[static_cast<std::size_t>(std::clamp(v, 0, bs::kFontCount - 1))];
            const std::wstring label = bs::fontLabel(v);
            rt_->DrawText(label.c_str(), static_cast<UINT32>(label.size()), f.Get(),
                          D2D1::RectF(c.x - 23 * u, c.y - 17 * u, c.x + 23 * u, c.y + 17 * u), brush_.Get());
            break;
        }
        case Action::List: {
            for (int i = 0; i < 3; ++i) {
                const float y = c.y + (static_cast<float>(i) - 1.0f) * 5.5f * u;
                rt_->DrawLine(D2D1::Point2F(c.x - 2 * u, y), D2D1::Point2F(c.x + 9 * u, y), brush_.Get(), 1.6f * u);
                if (v == static_cast<int>(bs::ListStyle::Bullet))
                    rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(c.x - 6 * u, y), 1.6f * u, 1.6f * u), brush_.Get());
            }
            if (v == static_cast<int>(bs::ListStyle::Numbered))
                rt_->DrawText(L"1\n2\n3", 5, labelFont_.Get(), D2D1::RectF(c.x - 10 * u, c.y - 9 * u, c.x - 2 * u, c.y + 10 * u),
                              brush_.Get());
            break;
        }
        case Action::MosaicMode:
            if (v == static_cast<int>(bs::MosaicMode::Pixelate)) {
                for (int i = 0; i < 4; ++i)
                    for (int j = 0; j < 4; ++j) {
                        brush_->SetColor(D2D1::ColorF(fg.r, fg.g, fg.b, (i + j) % 2 ? 0.9f : 0.3f));
                        rt_->FillRectangle(D2D1::RectF(c.x + (static_cast<float>(i) * 4 - 8) * u,
                                                       c.y + (static_cast<float>(j) * 4 - 8) * u,
                                                       c.x + (static_cast<float>(i) * 4 - 4) * u,
                                                       c.y + (static_cast<float>(j) * 4 - 4) * u),
                                           brush_.Get());
                    }
            } else {
                for (int i = 3; i >= 1; --i) {
                    brush_->SetColor(D2D1::ColorF(fg.r, fg.g, fg.b, 0.25f));
                    rt_->FillEllipse(D2D1::Ellipse(c, static_cast<float>(i) * 3.2f * u, static_cast<float>(i) * 3.2f * u),
                                     brush_.Get());
                }
            }
            break;
        default:
            break;
    }
}

void CaptureWindow::paintMenu() {
    if (!menu_.open) return;
    const float radius = 10.0f * ui_, row = 30.0f * ui_, pad = 6.0f * ui_;
    const bs::Rect& r = menu_.rect;
    for (int i = 3; i >= 1; --i) {
        const float g = static_cast<float>(i) * 2.0f * ui_;
        brush_->SetColor(D2D1::ColorF(0, 0, 0, 0.12f));
        rt_->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(r.left - g, r.top - g + 2 * ui_, r.right + g, r.bottom + g + 2 * ui_), radius + g,
                              radius + g),
            brush_.Get());
    }
    brush_->SetColor(kSurface);
    rt_->FillRoundedRectangle(D2D1::RoundedRect(gfx::toD2D(r), radius, radius), brush_.Get());
    const int current = styleValue(menu_.action);
    for (int i = 0; i < optionCount(menu_.action); ++i) {
        const float top = r.top + pad + static_cast<float>(i) * row;
        const auto bg = D2D1::RoundedRect(D2D1::RectF(r.left + 4 * ui_, top + 1, r.right - 4 * ui_, top + row - 1), 6 * ui_, 6 * ui_);
        if (i == current) {
            brush_->SetColor(kAccentSoft);
            rt_->FillRoundedRectangle(bg, brush_.Get());
        }
        if (i == menu_.hover) {
            brush_->SetColor(kHover);
            rt_->FillRoundedRectangle(bg, brush_.Get());
        }
        paintOptionIcon(menu_.action, i, D2D1::Point2F(r.left + 28 * ui_, top + row / 2), kOnSurface);
        const std::wstring label = optionLabel(menu_.action, i);
        brush_->SetColor(kOnSurface);
        labelFont_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        rt_->DrawText(label.c_str(), static_cast<UINT32>(label.size()), labelFont_.Get(),
                      D2D1::RectF(r.left + 58 * ui_, top, r.right - 8 * ui_, top + row), brush_.Get());
        labelFont_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    }
}

void CaptureWindow::paintTooltip() {
    if (menu_.open || tipButton_ < 0 || static_cast<std::size_t>(tipButton_) >= buttons_.size()) return;
    const Button& b = buttons_[static_cast<std::size_t>(tipButton_)];
    const std::wstring text = tooltipFor(b);
    if (text.empty()) return;
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(gfx::factories().dwrite->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), labelFont_.Get(),
                                                         520.0f * ui_, 200.0f * ui_, layout.GetAddressOf())))
        return;
    DWRITE_TEXT_METRICS m{};
    layout->GetMetrics(&m);
    const float padX = 10.0f * ui_, padY = 5.0f * ui_;
    const bs::Rect r = bs::placePopup(b.rect, m.widthIncludingTrailingWhitespace + 2 * padX, m.height + 2 * padY,
                                      monitorFor(sel_), 6.0f * ui_);
    brush_->SetColor(D2D1::ColorF(0.18f, 0.18f, 0.21f, 0.97f));
    rt_->FillRoundedRectangle(D2D1::RoundedRect(gfx::toD2D(r), 6 * ui_, 6 * ui_), brush_.Get());
    brush_->SetColor(kOnSurface);
    rt_->DrawTextLayout(D2D1::Point2F(r.left + padX, r.top + padY), layout.Get(), brush_.Get());
}

void CaptureWindow::paintMagnifier() {
    constexpr int kCells = 15;  // odd, so the cursor pixel is centred
    const float cell = 8.0f * ui_;
    const float zoom = kCells * cell;
    const float infoH = 62.0f * ui_;
    const bs::Rect box = bs::placeNearCursor(cursor_, zoom, zoom + infoH, bounds(), 20.0f * ui_);
    const int W = grab_.image.width, H = grab_.image.height;
    const int cx = std::clamp(static_cast<int>(cursor_.x), 0, W - 1);
    const int cy = std::clamp(static_cast<int>(cursor_.y), 0, H - 1);

    brush_->SetColor(D2D1::ColorF(0, 0, 0, 1));
    rt_->FillRectangle(D2D1::RectF(box.left, box.top, box.left + zoom, box.top + zoom), brush_.Get());
    // Clamp the source window to the bitmap and shift the destination to match.
    const int sx0 = cx - kCells / 2, sy0 = cy - kCells / 2;
    const int vx0 = std::max(sx0, 0), vy0 = std::max(sy0, 0);
    const int vx1 = std::min(sx0 + kCells, W), vy1 = std::min(sy0 + kCells, H);
    if (vx1 > vx0 && vy1 > vy0) {
        const auto dst = D2D1::RectF(box.left + static_cast<float>(vx0 - sx0) * cell, box.top + static_cast<float>(vy0 - sy0) * cell,
                                     box.left + static_cast<float>(vx1 - sx0) * cell, box.top + static_cast<float>(vy1 - sy0) * cell);
        const auto src = D2D1::RectF(static_cast<float>(vx0), static_cast<float>(vy0), static_cast<float>(vx1),
                                     static_cast<float>(vy1));
        rt_->DrawBitmap(shotBitmap_.Get(), dst, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR, src);
    }
    const float mid = box.left + (kCells / 2) * cell, midY = box.top + (kCells / 2) * cell;
    brush_->SetColor(kAccentSoft);
    rt_->FillRectangle(D2D1::RectF(box.left, midY, box.left + zoom, midY + cell), brush_.Get());
    rt_->FillRectangle(D2D1::RectF(mid, box.top, mid + cell, box.top + zoom), brush_.Get());
    brush_->SetColor(D2D1::ColorF(D2D1::ColorF::White));
    rt_->DrawRectangle(D2D1::RectF(mid, midY, mid + cell, midY + cell), brush_.Get(), 1.0f);
    rt_->DrawRectangle(D2D1::RectF(box.left, box.top, box.left + zoom, box.top + zoom), brush_.Get(), 1.0f);

    const bs::Rgb8 px = bs::fromBgra(grab_.image.at(cx, cy));
    const float iy = box.top + zoom;
    brush_->SetColor(kSurface);
    rt_->FillRectangle(D2D1::RectF(box.left, iy, box.right, box.bottom), brush_.Get());
    brush_->SetColor(D2D1::ColorF(px.r / 255.0f, px.g / 255.0f, px.b / 255.0f, 1.0f));
    const float sq = 12.0f * ui_, m = 8.0f * ui_;
    rt_->FillRectangle(D2D1::RectF(box.left + m, iy + m + 2 * ui_, box.left + m + sq, iy + m + 2 * ui_ + sq), brush_.Get());

    wchar_t line1[64];
    const int dx = cx + grab_.virtualRect.left, dy = cy + grab_.virtualRect.top;
    if (mode_ == Mode::Dragging && !bs::isEmpty(sel_))
        std::swprintf(line1, 64, L"%ls\n(%d, %d)  %d × %d", bs::hexString(px).c_str(), dx, dy,
                      static_cast<int>(sel_.width()), static_cast<int>(sel_.height()));
    else
        std::swprintf(line1, 64, L"%ls  %ls\n(%d, %d)", bs::hexString(px).c_str(), bs::rgbString(px).c_str(), dx, dy);
    brush_->SetColor(kOnSurface);
    rt_->DrawText(line1, static_cast<UINT32>(std::wcslen(line1)), labelFont_.Get(),
                  D2D1::RectF(box.left + m + sq + 6 * ui_, iy + m, box.right - m, box.bottom), brush_.Get());
    const std::wstring hint = copiedNote_.empty() ? L"C 复制 HEX · ⇧C 复制 RGB" : copiedNote_;
    brush_->SetColor(copiedNote_.empty() ? kMuted : kAccent);
    rt_->DrawText(hint.c_str(), static_cast<UINT32>(hint.size()), labelFont_.Get(),
                  D2D1::RectF(box.left + m, box.bottom - 20 * ui_, box.right - m, box.bottom), brush_.Get());
}

// ───────────────────────────── smoke ─────────────────────────────

const wchar_t* CaptureWindow::smoke(const bs::Rect& r) {
    if (!hwnd_) return L"no window";
    sel_ = bs::intersect(r, bounds());
    if (bs::isEmpty(sel_)) return L"empty selection";
    if (opts_.animate) {
        paint();  // mid-animation frame: ripple + shake
        animStart_ -= 1000000000LL;
        animating_ = false;
    }
    paint();  // picking frame: dim + magnifier
    mode_ = Mode::Selected;
    layoutToolbar();

    const bs::Vec2 o{sel_.left, sel_.top};
    // Every drag tool in its own column (so no stroke lands on an earlier shape),
    // each with a different CMF finish.
    float column = 10.0f;
    for (int t = 1; t <= 8; ++t) {
        if (t == 6 || t == 7) continue;  // text tools below
        tool_ = static_cast<Tool>(t);
        if (t == 8) run(Action::MosaicMode, 0, +1);  // blur
        run(Action::Finish, 0, +1);
        const bs::Vec2 a = o + bs::Vec2{column, 10.0f}, b = a + bs::Vec2{25.0f, 35.0f};
        onLButtonDown(a);
        onMouseMove(b, t == 1);  // Shift squares the rect to 35 px, still inside its column
        onLButtonUp(b, t == 1);
        column += 50.0f;
        tool_ = Tool::None;
    }
    if (doc_.shapes().size() != 6) return L"drag tools did not create 6 shapes";

    // Text with font / bold / italic / numbered list / backing plate.
    tool_ = Tool::Text;
    beginEdit(Edit::Text, o + bs::Vec2{20, 100});
    run(Action::Font, 0, +1);
    run(Action::Italic, 0, +1);
    run(Action::List, 0, -1);  // None → Numbered
    run(Action::Plate, 0, +1);
    for (wchar_t ch : std::wstring(L"第一\r* 要点")) onChar(ch);  // plain Enter breaks the line
    if (editShape_.text != L"第一\n* 要点") return L"Enter did not insert a line break";
    run(Action::Newline, 0, +1);
    onChar(L'\b');
    if (editShape_.text != L"第一\n* 要点") return L"newline button / backspace mismatch";
    const std::size_t before = doc_.shapes().size();
    onChar(L'\n');  // Ctrl+Enter arrives as LF and finishes
    if (edit_ != Edit::None || doc_.shapes().size() != before + 1) return L"Ctrl+Enter did not finish the text";
    const bs::Shape& text = doc_.shapes().back();
    if (text.textStyle.list != bs::ListStyle::Numbered || !text.textStyle.italic || text.textSize.x <= 0)
        return L"text style not applied";

    // MLEADER with caps on both ends, then pick it and drag its kink.
    tool_ = Tool::MLeader;
    run(Action::Tail, 0, +1);
    leader_.click(o + bs::Vec2{5, 120});
    leader_.click(o + bs::Vec2{60, 90});
    leader_.click(o + bs::Vec2{150, 90});
    beginEdit(Edit::LeaderLabel, o);
    onChar(L'A');
    commitEdit();
    tool_ = Tool::None;
    const std::size_t li = doc_.shapes().size() - 1;
    const bs::Vec2 kink = doc_.shapes()[li].leader.kink;
    onLButtonDown(kink);  // picks the leader body
    onLButtonUp(kink, false);
    if (picked_ != li) return L"click did not pick the leader";
    onLButtonDown(kink);  // now the kink joint
    onMouseMove(kink + bs::Vec2{10, -10}, false);
    paint();  // angle readout while dragging
    onLButtonUp(kink + bs::Vec2{10, -10}, false);
    if (!(doc_.shapes()[li].leader.kink == kink + bs::Vec2{10, -10})) return L"kink drag not applied";
    run(Action::Head, 0, +1);  // restyle the picked shape (right-click style cycle)
    paint();

    // Dropdown: open the Head menu of the picked leader and choose "Dot".
    const auto headBtn = std::find_if(buttons_.begin(), buttons_.end(), [](const Button& b) { return b.action == Action::Head; });
    if (headBtn == buttons_.end()) return L"no Head button for the picked leader";
    openMenu(*headBtn);
    paint();
    const float row = 30.0f * ui_, pad = 6.0f * ui_;
    const bs::Vec2 dotRow{menu_.rect.left + 20.0f, menu_.rect.top + pad + row * 3.5f};
    onMouseMove(dotRow, false);
    onLButtonDown(dotRow);
    onLButtonUp(dotRow, false);
    if (menu_.open || doc_.shapes()[li].head != bs::EndShape::Dot) return L"dropdown pick not applied";
    // Frosted material turns the dot head into a glass lens.
    run(Action::Color, kFrostedSwatch, +1);
    if (doc_.shapes()[li].material != bs::Material::Frosted) return L"frosted material not applied";
    tipButton_ = 0;  // tooltip frame
    paint();
    tipButton_ = -1;

    // Double-click edits the label in place; Delete removes; undo restores.
    const std::size_t count = doc_.shapes().size();
    beginEditExisting(li);
    onChar(L'B');
    commitEdit();
    if (doc_.shapes()[li].text != L"AB") return L"label re-edit failed";
    picked_ = li;
    deletePicked();
    if (doc_.shapes().size() != count - 1) return L"delete failed";
    doc_.undo();
    if (doc_.shapes().size() != count) return L"undo of delete failed";

    paint();
    const gfx::Image img = composite();
    const bool sized = img.width == static_cast<int>(std::round(sel_.width())) &&
                       img.height == static_cast<int>(std::round(sel_.height()));
    return sized ? nullptr : L"composite size mismatch";
}
