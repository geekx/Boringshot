#include "capture_window.h"

#include <imm.h>

#include <algorithm>
#include <cmath>
#include <cwchar>

#include "boringshot/color.h"

using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kClassName[] = L"Boringshot.Capture";

const bs::Color kPalette[] = {
    {1.00f, 0.30f, 0.31f, 1.0f},  // red
    {1.00f, 0.77f, 0.24f, 1.0f},  // amber
    {0.32f, 0.77f, 0.10f, 1.0f},  // green
    {0.09f, 0.47f, 1.00f, 1.0f},  // blue
    {1.00f, 1.00f, 1.00f, 1.0f},  // white
    {0.10f, 0.10f, 0.10f, 1.0f},  // ink
};
constexpr int kPaletteSize = 6;
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
        default: return bs::ShapeKind::Rect;
    }
}

ComPtr<IDWriteTextFormat> makeFormat(const wchar_t* family, float size, DWRITE_FONT_WEIGHT weight, bool center) {
    ComPtr<IDWriteTextFormat> f;
    gfx::factories().dwrite->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                                              DWRITE_FONT_STRETCH_NORMAL, size, L"", f.GetAddressOf());
    if (f && center) {
        f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    return f;
}

}  // namespace

// ───────────────────────────── lifecycle ─────────────────────────────

bool CaptureWindow::open(HINSTANCE instance, ScreenGrab grab, Callbacks cb) {
    if (grab.image.empty()) return false;
    grab_ = std::move(grab);
    cb_ = std::move(cb);
    ui_ = static_cast<float>(GetDpiForSystem()) / 96.0f;

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
        case WM_SIZE:
            if (rt_) rt_->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp)));
            return 0;
        case WM_MOUSEMOVE:
            onMouseMove(pointFrom(lp), (wp & MK_SHIFT) != 0);
            return 0;
        case WM_LBUTTONDOWN:
            SetCapture(hwnd_);
            onLButtonDown(pointFrom(lp));
            return 0;
        case WM_LBUTTONUP:
            ReleaseCapture();
            onLButtonUp(pointFrom(lp), (wp & MK_SHIFT) != 0);
            return 0;
        case WM_LBUTTONDBLCLK:
            onDoubleClick(pointFrom(lp));
            return 0;
        case WM_RBUTTONUP:
            onRButtonUp();
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

// ───────────────────────────── input ─────────────────────────────

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

bs::Shape CaptureWindow::styledShape(bs::ShapeKind kind) const {
    bs::Shape s;
    s.kind = kind;
    s.color = kPalette[colorIndex_];
    s.strokeWidth = kWidths[widthIndex_];
    s.fontSize = kFontSizes[widthIndex_];
    return s;
}

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
            if (dragHandle_ != bs::Handle::None) {
                sel_ = bs::dragHandle(selAtDragStart_, dragHandle_, p - dragStart_, bounds());
            } else if (drawing_) {
                if (draft_.kind == bs::ShapeKind::Pen)
                    bs::appendPenPoint(draft_.points, clamped, 1.5f);
                else
                    draft_.b = bs::constrainDrag(draft_.kind, draft_.a, clamped, shift);
            } else if (tool_ == Tool::MLeader) {
                leader_.pointerMove(clamped);
            }
            hoverButton_ = buttonAt(p);
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

    if (edit_ != Edit::None) {
        commitEdit();
        if (buttonAt(p) < 0) {  // clicking away just finishes the text
            invalidate();
            return;
        }
    }
    if (const int b = buttonAt(p); b >= 0) {
        pressedButton_ = b;
        return;
    }

    const bs::Handle h = bs::hitTestHandle(sel_, p, 6.0f * ui_);
    const bool resize = h != bs::Handle::None && h != bs::Handle::Move;
    if (resize || (tool_ == Tool::None && h == bs::Handle::Move)) {
        dragHandle_ = h;
        dragStart_ = p;
        selAtDragStart_ = sel_;
        return;
    }
    if (tool_ == Tool::None || !bs::contains(sel_, p)) return;

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
            draft_ = styledShape(kindFor(static_cast<int>(tool_)));
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
        layoutToolbar();
        invalidate();
        return;
    }
    if (pressedButton_ >= 0) {
        const int b = pressedButton_;
        pressedButton_ = -1;
        if (buttonAt(p) == b) {
            const Button btn = buttons_[static_cast<std::size_t>(b)];
            run(btn.action, btn.arg);  // may close the window
        }
        return;
    }
    if (dragHandle_ != bs::Handle::None) {
        dragHandle_ = bs::Handle::None;
        if (bs::isEmpty(sel_)) {
            resetToPicking();
            return;
        }
        layoutToolbar();
    } else if (drawing_) {
        drawing_ = false;
        if (draft_.kind != bs::ShapeKind::Pen) {
            const bs::Vec2 c{std::clamp(p.x, sel_.left, sel_.right), std::clamp(p.y, sel_.top, sel_.bottom)};
            draft_.b = bs::constrainDrag(draft_.kind, draft_.a, c, shift);
        }
        doc_.add(draft_);
    }
    invalidate();
}

void CaptureWindow::onDoubleClick(bs::Vec2 p) {
    // Double-click inside the selection = copy and done (works right after snapping a window).
    if (mode_ == Mode::Selected && tool_ == Tool::None && edit_ == Edit::None && bs::contains(sel_, p))
        finishCopy();
}

void CaptureWindow::onRButtonUp() {
    if (edit_ != Edit::None) {
        cancelEdit();
    } else if (drawing_ || leader_.phase() != bs::MLeaderTool::Phase::ArrowTip) {
        drawing_ = false;
        leader_.cancel();
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
    if (edit_ != Edit::None) {  // characters arrive through WM_CHAR
        if (key == VK_ESCAPE) cancelEdit();
        invalidate();
        return;
    }
    const bool selected = mode_ == Mode::Selected;
    switch (key) {
        case VK_ESCAPE:
            if (drawing_ || leader_.phase() != bs::MLeaderTool::Phase::ArrowTip) {
                drawing_ = false;
                leader_.cancel();
            } else {
                close();
                return;
            }
            break;
        case VK_RETURN:
            if (selected) finishCopy();
            return;
        case 'Z':
            if (ctrl) shift ? doc_.redo() : doc_.undo();
            break;
        case 'Y':
            if (ctrl) doc_.redo();
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
            if (selected) {
                sel_ = bs::dragHandle(sel_, bs::Handle::Move, d, bounds());
                layoutToolbar();
            } else {  // pixel-precise picking
                POINT pt{};
                GetCursorPos(&pt);
                SetCursorPos(pt.x + static_cast<int>(d.x), pt.y + static_cast<int>(d.y));
            }
            break;
        }
        default:
            if (selected && key >= '1' && key <= '7') selectTool(static_cast<Tool>(key - '0'));
            break;
    }
    invalidate();
}

void CaptureWindow::onChar(wchar_t c) {
    if (edit_ == Edit::None) return;
    std::wstring& text = editShape_.text;
    if (c == L'\b') {
        if (!text.empty()) text.pop_back();
        if (!text.empty() && IS_HIGH_SURROGATE(text.back())) text.pop_back();
    } else if (c == L'\r') {
        if (keyDown(VK_SHIFT)) {
            text += L'\n';
        } else {
            commitEdit();
        }
    } else if (c >= 32) {
        text += c;
    }
    placeImeWindow();
    invalidate();
}

void CaptureWindow::updateCursor() {
    LPCWSTR id = IDC_CROSS;
    if (mode_ == Mode::Selected) {
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
        if (buttonAt(cursor_) >= 0) id = IDC_HAND;
    }
    SetCursor(LoadCursorW(nullptr, id));
}

// ───────────────────────────── actions ─────────────────────────────

void CaptureWindow::selectTool(Tool t) {
    if (edit_ != Edit::None) commitEdit();
    leader_.cancel();
    drawing_ = false;
    tool_ = tool_ == t ? Tool::None : t;
}

void CaptureWindow::run(Action a, int arg) {
    switch (a) {
        case Action::Tool: selectTool(static_cast<Tool>(arg)); break;
        case Action::Color: colorIndex_ = std::clamp(arg, 0, kPaletteSize - 1); break;
        case Action::Width: widthIndex_ = std::clamp(arg, 0, kWidthCount - 1); break;
        case Action::Undo: doc_.undo(); break;
        case Action::Redo: doc_.redo(); break;
        case Action::Pin: finishPin(); return;
        case Action::Save: finishSave(); return;
        case Action::Copy: finishCopy(); return;
        case Action::Cancel: close(); return;
    }
    invalidate();
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
    leader_.cancel();
    tool_ = Tool::None;
    dragHandle_ = bs::Handle::None;
    buttons_.clear();
    hover_ = bs::pickWindowRect(grab_.windows, cursor_);
    invalidate();
}

// ───────────────────────────── text editing ─────────────────────────────

void CaptureWindow::beginEdit(Edit kind, bs::Vec2 at) {
    edit_ = kind;
    if (kind == Edit::Text) {
        editShape_ = styledShape(bs::ShapeKind::Text);
        editShape_.a = at - bs::Vec2{0.0f, gfx::measureText(L"Ag", editShape_.fontSize).height * 0.5f};
    } else {
        editShape_ = styledShape(bs::ShapeKind::MLeader);
        editShape_.leader = leader_.input();
    }
    editShape_.text.clear();
    placeImeWindow();
}

void CaptureWindow::commitEdit() {
    if (edit_ == Edit::None) return;
    doc_.add(editShape_);  // empty Text is dropped; a bare leader is kept
    if (edit_ == Edit::LeaderLabel) leader_.cancel();
    edit_ = Edit::None;
}

void CaptureWindow::cancelEdit() {
    if (edit_ == Edit::LeaderLabel) leader_.cancel();
    edit_ = Edit::None;
}

void CaptureWindow::placeImeWindow() {
    if (edit_ == Edit::None || !hwnd_) return;
    const bs::Vec2 o = gfx::textOrigin(editShape_);
    const D2D1_RECT_F caret = gfx::caretRect(editShape_.text, editShape_.fontSize);
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
    if (tool_ == Tool::MLeader && leader_.phase() != bs::MLeaderTool::Phase::ArrowTip) {
        bs::Shape s = styledShape(bs::ShapeKind::MLeader);
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
    if (FAILED(gfx::factories().d2d->CreateHwndRenderTarget(props, hwndProps, rt_.GetAddressOf()))) return false;
    shotBitmap_ = gfx::makeBitmap(rt_.Get(), grab_.image);
    rt_->CreateSolidColorBrush(kOnSurface, brush_.ReleaseAndGetAddressOf());
    if (!labelFont_) labelFont_ = makeFormat(L"Segoe UI", 12.0f * ui_, DWRITE_FONT_WEIGHT_NORMAL, false);
    if (!iconFont_) iconFont_ = makeFormat(L"Segoe MDL2 Assets", 16.0f * ui_, DWRITE_FONT_WEIGHT_NORMAL, true);
    if (!glyphFont_) glyphFont_ = makeFormat(L"Segoe UI", 18.0f * ui_, DWRITE_FONT_WEIGHT_SEMI_BOLD, true);
    return shotBitmap_ && brush_ && labelFont_ && iconFont_ && glyphFont_;
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
    std::vector<Item> items;
    for (int t = 1; t <= 7; ++t) items.push_back({Action::Tool, t, false});
    for (int c = 0; c < kPaletteSize; ++c) items.push_back({Action::Color, c, c == 0});
    for (int w = 0; w < kWidthCount; ++w) items.push_back({Action::Width, w, false});
    items.push_back({Action::Undo, 0, true});
    items.push_back({Action::Redo, 0, false});
    items.push_back({Action::Cancel, 0, true});
    items.push_back({Action::Pin, 0, false});
    items.push_back({Action::Save, 0, false});
    items.push_back({Action::Copy, 0, false});

    const float size = 34.0f * ui_, pad = 6.0f * ui_, gap = 12.0f * ui_;
    buttons_.clear();
    float x = pad;
    for (const auto& it : items) {
        if (it.gapBefore) x += gap;
        buttons_.push_back({{x, pad, x + size, pad + size}, it.a, it.arg});
        x += size;
    }
    toolbarRect_ = bs::placeToolbar(sel_, x + pad, size + 2.0f * pad, monitorFor(sel_), 8.0f * ui_);
    for (auto& b : buttons_) {
        b.rect.left += toolbarRect_.left;
        b.rect.right += toolbarRect_.left;
        b.rect.top += toolbarRect_.top;
        b.rect.bottom += toolbarRect_.top;
    }
}

void CaptureWindow::paint() {
    if (!hwnd_ || !ensureTarget()) return;
    rt_->BeginDraw();
    rt_->SetTransform(D2D1::Matrix3x2F::Identity());
    rt_->DrawBitmap(shotBitmap_.Get(), gfx::toD2D(bounds()), 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);

    const bs::Rect active = activeRect();
    paintDim(active);
    if (!bs::isEmpty(active)) {
        if (mode_ == Mode::Selected) {
            rt_->PushAxisAlignedClip(gfx::toD2D(active), D2D1_ANTIALIAS_MODE_ALIASED);
            gfx::drawShapes(rt_.Get(), doc_.shapes());
            if (auto p = previewShape()) gfx::drawShape(rt_.Get(), *p);
            if (edit_ != Edit::None) {
                const bs::Vec2 o = gfx::textOrigin(editShape_);
                const D2D1_RECT_F c = gfx::caretRect(editShape_.text, editShape_.fontSize);
                const D2D1_SIZE_F sz = gfx::measureText(editShape_.text, editShape_.fontSize);
                brush_->SetColor(kAccent);
                rt_->DrawRectangle(D2D1::RectF(o.x - 4, o.y - 2, o.x + std::max(sz.width, 8.0f) + 4, o.y + sz.height + 2),
                                   brush_.Get(), 1.0f);
                brush_->SetColor(gfx::toD2D(editShape_.color));
                rt_->FillRectangle(D2D1::RectF(o.x + c.left, o.y + c.top, o.x + c.right, o.y + c.bottom), brush_.Get());
            }
            rt_->PopAxisAlignedClip();
        }
        paintSelectionChrome(active);
    }
    if (mode_ == Mode::Selected && dragHandle_ == bs::Handle::None && !drawing_) paintToolbar();
    if (mode_ != Mode::Selected) paintMagnifier();

    if (rt_->EndDraw() == static_cast<HRESULT>(D2DERR_RECREATE_TARGET)) {
        rt_.Reset();
        shotBitmap_.Reset();
        brush_.Reset();
        invalidate();
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

void CaptureWindow::paintSelectionChrome(const bs::Rect& s) {
    const bool snapping = mode_ == Mode::Picking || (mode_ == Mode::Dragging && bs::isClick(dragStart_, cursor_));
    brush_->SetColor(kAccent);
    const float w = (snapping ? 3.0f : 1.5f) * ui_;
    rt_->DrawRectangle(D2D1::RectF(s.left + w / 2, s.top + w / 2, s.right - w / 2, s.bottom - w / 2), brush_.Get(), w);

    if (mode_ == Mode::Selected) {
        const float r = 4.0f * ui_;
        const float xs[] = {s.left, (s.left + s.right) / 2, s.right};
        const float ys[] = {s.top, (s.top + s.bottom) / 2, s.bottom};
        for (float x : xs)
            for (float y : ys) {
                if (x == xs[1] && y == ys[1]) continue;
                const auto e = D2D1::Ellipse(D2D1::Point2F(x, y), r, r);
                brush_->SetColor(D2D1::ColorF(D2D1::ColorF::White));
                rt_->FillEllipse(e, brush_.Get());
                brush_->SetColor(kAccent);
                rt_->DrawEllipse(e, brush_.Get(), 1.5f * ui_);
            }
    }

    wchar_t label[48];
    std::swprintf(label, 48, L"%d × %d", static_cast<int>(std::round(s.width())), static_cast<int>(std::round(s.height())));
    const float lw = 92.0f * ui_, lh = 22.0f * ui_;
    const float ly = s.top - lh - 6.0f * ui_ >= 0 ? s.top - lh - 6.0f * ui_ : s.top + 6.0f * ui_;
    const float lx = s.left + (s.top - lh - 6.0f * ui_ >= 0 ? 0.0f : 6.0f * ui_);
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

void CaptureWindow::paintToolbar() {
    if (buttons_.empty()) return;
    const float radius = 12.0f * ui_;
    // Soft elevation: a few widening translucent layers under the surface.
    for (int i = 3; i >= 1; --i) {
        const float g = static_cast<float>(i) * 2.0f * ui_;
        brush_->SetColor(D2D1::ColorF(0, 0, 0, 0.10f));
        rt_->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(toolbarRect_.left - g, toolbarRect_.top - g + 2 * ui_,
                                          toolbarRect_.right + g, toolbarRect_.bottom + g + 2 * ui_),
                              radius + g, radius + g),
            brush_.Get());
    }
    brush_->SetColor(kSurface);
    rt_->FillRoundedRectangle(D2D1::RoundedRect(gfx::toD2D(toolbarRect_), radius, radius), brush_.Get());

    for (std::size_t i = 0; i < buttons_.size(); ++i) {
        const Button& b = buttons_[i];
        const auto bg = D2D1::RoundedRect(D2D1::RectF(b.rect.left + 2, b.rect.top + 2, b.rect.right - 2, b.rect.bottom - 2),
                                          8.0f * ui_, 8.0f * ui_);
        const bool activeTool = b.action == Action::Tool && static_cast<int>(tool_) == b.arg;
        D2D1_COLOR_F fg = kOnSurface;
        if (b.action == Action::Copy) {
            brush_->SetColor(kAccent);
            rt_->FillRoundedRectangle(bg, brush_.Get());
            fg = D2D1::ColorF(D2D1::ColorF::White);
        } else if (activeTool) {
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
                case Tool::None:
                    break;
            }
            break;
        case Action::Color: {
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
        case Action::Undo: glyph(L"", iconFont_.Get()); break;
        case Action::Redo: glyph(L"", iconFont_.Get()); break;
        case Action::Pin: glyph(L"", iconFont_.Get()); break;
        case Action::Save: glyph(L"", iconFont_.Get()); break;
        case Action::Copy: glyph(L"", iconFont_.Get()); break;
        case Action::Cancel: glyph(L"", iconFont_.Get()); break;
    }
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

bool CaptureWindow::smoke(const bs::Rect& r) {
    if (!hwnd_) return false;
    sel_ = bs::intersect(r, bounds());
    if (bs::isEmpty(sel_)) return false;
    paint();  // picking frame: dim + magnifier
    mode_ = Mode::Selected;
    layoutToolbar();

    const bs::Vec2 o{sel_.left, sel_.top};
    for (int t = 1; t <= 5; ++t) {
        bs::Shape s = styledShape(kindFor(t));
        s.a = o + bs::Vec2{10.0f, 10.0f};
        s.b = o + bs::Vec2{60.0f, 50.0f};
        s.points = {s.a, o + bs::Vec2{30, 40}, s.b};
        doc_.add(s);
    }
    tool_ = Tool::Text;
    beginEdit(Edit::Text, o + bs::Vec2{20, 80});
    for (wchar_t ch : std::wstring(L"Boringshot 标注")) onChar(ch);
    commitEdit();
    tool_ = Tool::MLeader;
    leader_.click(o + bs::Vec2{5, 5});
    leader_.click(o + bs::Vec2{40, 30});
    leader_.click(o + bs::Vec2{90, 30});
    beginEdit(Edit::LeaderLabel, o);
    onChar(L'A');
    commitEdit();

    doc_.undo();
    doc_.redo();
    paint();
    const gfx::Image img = composite();
    return doc_.shapes().size() == 7 && img.width == static_cast<int>(std::round(sel_.width())) &&
           img.height == static_cast<int>(std::round(sel_.height()));
}
