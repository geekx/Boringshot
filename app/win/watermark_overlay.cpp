#include "watermark_overlay.h"

using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kClassName[] = L"Boringshot.WatermarkOverlay";

std::wstring currentUser() {
    wchar_t name[257] = {};
    DWORD len = 257;
    return GetUserNameW(name, &len) ? std::wstring(name) : std::wstring(L"user");
}

bs::DateTimeParts now() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    return {st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute};
}

// RAII for the GDI objects UpdateLayeredWindow needs.
struct DibSurface {
    HDC screen = nullptr;
    HDC mem = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;

    bool init(int w, int h) {
        screen = GetDC(nullptr);
        mem = CreateCompatibleDC(screen);
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h;  // top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        bitmap = CreateDIBSection(mem, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bitmap) return false;
        previous = SelectObject(mem, bitmap);
        return true;
    }

    ~DibSurface() {
        if (previous) SelectObject(mem, previous);
        if (bitmap) DeleteObject(bitmap);
        if (mem) DeleteDC(mem);
        if (screen) ReleaseDC(nullptr, screen);
    }
};

}  // namespace

bool WatermarkOverlay::create(HINSTANCE instance) {
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_.GetAddressOf())))
        return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()))))
        return false;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &WatermarkOverlay::wndProc;
    wc.hInstance = instance;
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);  // re-registration after destroy() fails harmlessly

    hwnd_ = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
        kClassName, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance, nullptr);
    return hwnd_ != nullptr;
}

void WatermarkOverlay::destroy() {
    if (hwnd_) DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    enabled_ = false;
}

void WatermarkOverlay::enable(bool on) {
    if (!hwnd_) return;
    enabled_ = on;
    if (on) {
        refresh(true);
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    } else {
        ShowWindow(hwnd_, SW_HIDE);
    }
}

void WatermarkOverlay::updateProfile(const bs::WatermarkProfile& profile) {
    profile_ = profile;
    if (enabled_) refresh(true);
}

std::wstring WatermarkOverlay::expandedText() const {
    return bs::expandWatermarkText(profile_.text, {currentUser(), now()});
}

bool WatermarkOverlay::refresh(bool force) {
    std::wstring text = expandedText();
    if (!force && text == lastText_) return true;
    lastText_ = std::move(text);
    return render();
}

bool WatermarkOverlay::render() {
    if (!hwnd_) return false;
    const bool all = profile_.enableOnAllMonitors;
    const int x = all ? GetSystemMetrics(SM_XVIRTUALSCREEN) : 0;
    const int y = all ? GetSystemMetrics(SM_YVIRTUALSCREEN) : 0;
    const int w = GetSystemMetrics(all ? SM_CXVIRTUALSCREEN : SM_CXSCREEN);
    const int h = GetSystemMetrics(all ? SM_CYVIRTUALSCREEN : SM_CYSCREEN);
    if (w <= 0 || h <= 0) return false;

    DibSurface dib;
    if (!dib.init(w, h)) return false;

    const auto props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
    ComPtr<ID2D1DCRenderTarget> rt;
    if (FAILED(d2d_->CreateDCRenderTarget(&props, rt.GetAddressOf()))) return false;
    const RECT bounds{0, 0, w, h};
    if (FAILED(rt->BindDC(dib.mem, &bounds))) return false;

    const float scale = static_cast<float>(GetDpiForSystem()) / 96.0f;
    ComPtr<IDWriteTextFormat> format;
    if (FAILED(dwrite_->CreateTextFormat(profile_.fontFamily.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                         DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                         profile_.fontSize * scale, L"", format.GetAddressOf())))
        return false;
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dwrite_->CreateTextLayout(lastText_.c_str(), static_cast<UINT32>(lastText_.size()),
                                         format.Get(), 4096.0f, 1024.0f, layout.GetAddressOf())))
        return false;
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(rt->CreateSolidColorBrush(
            D2D1::ColorF(profile_.colorR, profile_.colorG, profile_.colorB, profile_.opacity),
            brush.GetAddressOf())))
        return false;

    rt->BeginDraw();
    rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);  // ClearType needs opaque bg
    rt->Clear(D2D1::ColorF(0, 0, 0, 0));
    const bs::Rect area{0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)};
    for (const auto& p : bs::watermarkTileOrigins(area, profile_.tileSpacingX * scale,
                                                  profile_.tileSpacingY * scale)) {
        const auto origin = D2D1::Point2F(p.x, p.y);
        rt->SetTransform(D2D1::Matrix3x2F::Rotation(profile_.rotationDeg, origin));
        rt->DrawTextLayout(origin, layout.Get(), brush.Get());
    }
    rt->SetTransform(D2D1::Matrix3x2F::Identity());
    if (FAILED(rt->EndDraw())) return false;

    POINT dst{x, y};
    SIZE size{w, h};
    POINT src{0, 0};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    return UpdateLayeredWindow(hwnd_, dib.screen, &dst, &size, dib.mem, &src, 0, &blend, ULW_ALPHA) != FALSE;
}

LRESULT CALLBACK WatermarkOverlay::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCHITTEST) return HTTRANSPARENT;
    if (msg == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    return DefWindowProcW(hwnd, msg, wp, lp);
}
