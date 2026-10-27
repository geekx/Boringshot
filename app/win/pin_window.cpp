#include "pin_window.h"

#include <algorithm>
#include <cmath>

#include "boringshot/pin.h"

using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kClassName[] = L"Boringshot.Pin";
enum : UINT { kCmdCopy = 1, kCmdSave, kCmdReset, kCmdClose };

}  // namespace

bool PinWindow::open(HINSTANCE instance, gfx::Image image, POINT topLeft, std::function<void(PinWindow*)> closed) {
    if (image.empty()) return false;
    image_ = std::move(image);
    closed_ = std::move(closed);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS | CS_DROPSHADOW;
    wc.lpfnWndProc = &PinWindow::wndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_SIZEALL);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, kClassName, L"Boringshot Pin", WS_POPUP,
                    topLeft.x, topLeft.y, image_.width, image_.height, nullptr, nullptr, instance, this);
    if (!hwnd_) return false;
    setOpacity(255);
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    return true;
}

void PinWindow::close() {
    if (hwnd_) DestroyWindow(hwnd_);
}

LRESULT CALLBACK PinWindow::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* self = static_cast<PinWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<PinWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    if (msg == WM_NCDESTROY) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        self->hwnd_ = nullptr;
        self->rt_.Reset();
        if (auto closed = self->closed_) closed(self);
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    return self->handle(msg, wp, lp);
}

LRESULT PinWindow::handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_NCHITTEST:
            return HTCAPTION;  // whole surface drags the window
        case WM_NCLBUTTONDBLCLK:
            close();  // double-click dismisses
            return 0;
        case WM_NCRBUTTONUP:
            showMenu({static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))});
            return 0;
        case WM_MOUSEWHEEL: {
            const int delta = GET_WHEEL_DELTA_WPARAM(wp);
            if (GET_KEYSTATE_WPARAM(wp) & MK_CONTROL) {
                setOpacity(alpha_ + (delta > 0 ? 20 : -20));
            } else {
                zoom(delta, {static_cast<short>(LOWORD(lp)), static_cast<short>(HIWORD(lp))});
            }
            return 0;
        }
        case WM_KEYDOWN: {
            const bool ctrl = GetKeyState(VK_CONTROL) < 0;
            if (wp == VK_ESCAPE) close();
            if (ctrl && wp == 'C') gfx::copyImageToClipboard(hwnd_, image_);
            if (ctrl && wp == 'S') gfx::saveWithDialog(hwnd_, image_);
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case kCmdCopy: gfx::copyImageToClipboard(hwnd_, image_); break;
                case kCmdSave: gfx::saveWithDialog(hwnd_, image_); break;
                case kCmdReset: {
                    RECT r{};
                    GetWindowRect(hwnd_, &r);
                    scale_ = 1.0f;
                    SetWindowPos(hwnd_, nullptr, r.left, r.top, image_.width, image_.height,
                                 SWP_NOZORDER | SWP_NOACTIVATE);
                    setOpacity(255);
                    break;
                }
                case kCmdClose: close(); break;
                default: break;
            }
            return 0;
        case WM_SIZE:
            if (rt_) rt_->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp)));
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd_, &ps);
            paint();
            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        default:
            return DefWindowProcW(hwnd_, msg, wp, lp);
    }
}

void PinWindow::paint() {
    if (!hwnd_) return;
    if (!rt_) {
        RECT rc{};
        GetClientRect(hwnd_, &rc);
        const auto props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
            96.0f, 96.0f);
        if (FAILED(gfx::factories().d2d->CreateHwndRenderTarget(
                props,
                D2D1::HwndRenderTargetProperties(hwnd_, D2D1::SizeU(static_cast<UINT32>(rc.right),
                                                                    static_cast<UINT32>(rc.bottom))),
                rt_.GetAddressOf())))
            return;
        bitmap_ = gfx::makeBitmap(rt_.Get(), image_);
        rt_->CreateSolidColorBrush(D2D1::ColorF(0.25f, 0.56f, 1.0f, 0.9f), brush_.ReleaseAndGetAddressOf());
    }
    const D2D1_SIZE_F size = rt_->GetSize();
    rt_->BeginDraw();
    rt_->Clear(D2D1::ColorF(D2D1::ColorF::White));
    if (bitmap_)
        rt_->DrawBitmap(bitmap_.Get(), D2D1::RectF(0, 0, size.width, size.height), 1.0f,
                        scale_ >= 1.0f ? D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR
                                       : D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    if (brush_) rt_->DrawRectangle(D2D1::RectF(0.5f, 0.5f, size.width - 0.5f, size.height - 0.5f), brush_.Get(), 1.0f);
    if (rt_->EndDraw() == static_cast<HRESULT>(D2DERR_RECREATE_TARGET)) {
        rt_.Reset();
        bitmap_.Reset();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void PinWindow::zoom(int wheelDelta, POINT anchor) {
    const float next = bs::nextPinScale(scale_, wheelDelta);
    if (next == scale_) return;
    RECT r{};
    GetWindowRect(hwnd_, &r);
    const bs::Rect win{static_cast<float>(r.left), static_cast<float>(r.top), static_cast<float>(r.right),
                       static_cast<float>(r.bottom)};
    const bs::Rect z = bs::zoomAroundPoint(win, {static_cast<float>(anchor.x), static_cast<float>(anchor.y)}, scale_, next);
    scale_ = next;
    const int w = std::max(1, static_cast<int>(std::lround(static_cast<float>(image_.width) * scale_)));
    const int h = std::max(1, static_cast<int>(std::lround(static_cast<float>(image_.height) * scale_)));
    SetWindowPos(hwnd_, nullptr, static_cast<int>(std::lround(z.left)), static_cast<int>(std::lround(z.top)), w, h,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

void PinWindow::setOpacity(int alpha) {
    alpha_ = std::clamp(alpha, 40, 255);
    SetLayeredWindowAttributes(hwnd_, 0, static_cast<BYTE>(alpha_), LWA_ALPHA);
}

void PinWindow::showMenu(POINT at) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kCmdCopy, L"复制\tCtrl+C");
    AppendMenuW(menu, MF_STRING, kCmdSave, L"另存为…\tCtrl+S");
    AppendMenuW(menu, MF_STRING, kCmdReset, L"还原大小与透明度");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdClose, L"关闭\t双击 / Esc");
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, at.x, at.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
}

bool PinWindow::smoke() {
    if (!hwnd_) return false;
    paint();
    RECT before{};
    GetWindowRect(hwnd_, &before);
    zoom(120, {before.left, before.top});
    RECT after{};
    GetWindowRect(hwnd_, &after);
    paint();
    return (after.right - after.left) > (before.right - before.left);
}
