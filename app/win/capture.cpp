#include "capture.h"

#include <dwmapi.h>

#include <cstring>
#include <cwchar>

#include "boringshot/selection.h"
#ifdef BORINGSHOT_HAS_WGC
#include "capture_wgc.h"
#endif

namespace {

struct EnumCtx {
    RECT origin;
    bs::Rect bounds;
    std::vector<bs::Rect>* out;
};

bs::Rect toLocal(const RECT& r, const RECT& origin) {
    return {static_cast<float>(r.left - origin.left), static_cast<float>(r.top - origin.top),
            static_cast<float>(r.right - origin.left), static_cast<float>(r.bottom - origin.top)};
}

bool isSnapTarget(HWND hwnd) {
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId()) return false;  // our overlays / pins
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked)
        return false;  // other virtual desktops, suspended UWP frames
    if (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TRANSPARENT) return false;
    wchar_t cls[32] = {};
    GetClassNameW(hwnd, cls, 32);
    // The desktop itself is covered by the monitor entries.
    return std::wcscmp(cls, L"Progman") != 0 && std::wcscmp(cls, L"WorkerW") != 0;
}

BOOL CALLBACK onWindow(HWND hwnd, LPARAM lp) {
    auto* ctx = reinterpret_cast<EnumCtx*>(lp);
    if (!isSnapTarget(hwnd)) return TRUE;
    RECT r{};
    // Extended frame bounds exclude the invisible resize borders of Win10+ windows.
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r))))
        GetWindowRect(hwnd, &r);
    const bs::Rect local = bs::intersect(toLocal(r, ctx->origin), ctx->bounds);
    if (!bs::isEmpty(local)) ctx->out->push_back(local);
    return TRUE;
}

BOOL CALLBACK onMonitor(HMONITOR, HDC, LPRECT r, LPARAM lp) {
    auto* ctx = reinterpret_cast<EnumCtx*>(lp);
    ctx->out->push_back(toLocal(*r, ctx->origin));
    return TRUE;
}

}  // namespace

namespace {

bool grabGdi(const RECT& v, gfx::Image& out) {
    const int w = v.right - v.left;
    const int h = v.bottom - v.top;
    bool ok = false;
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(mem, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (dib && bits) {
        HGDIOBJ old = SelectObject(mem, dib);
        if (BitBlt(mem, 0, 0, w, h, screen, v.left, v.top, SRCCOPY | CAPTUREBLT)) {
            GdiFlush();
            out.width = w;
            out.height = h;
            out.px.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
            std::memcpy(out.px.data(), bits, out.px.size() * 4u);
            for (auto& p : out.px) p |= 0xFF000000u;  // GDI leaves alpha undefined
            ok = true;
        }
        SelectObject(mem, old);
    }
    if (dib) DeleteObject(dib);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return ok;
}

}  // namespace

ScreenGrab grabScreen(bs::CaptureBackend backend) {
    ScreenGrab g;
    RECT& v = g.virtualRect;
    v.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    v.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    v.right = v.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    v.bottom = v.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    const int w = v.right - v.left;
    const int h = v.bottom - v.top;
    if (w <= 0 || h <= 0) return g;

#ifdef BORINGSHOT_HAS_WGC
    const bool tryWgc = backend == bs::CaptureBackend::Wgc || (backend == bs::CaptureBackend::Auto && wgcBorderless());
    if (tryWgc && grabWithWgc(v, g.image)) g.backend = L"wgc";
#else
    (void)backend;
#endif
    if (g.image.empty() && grabGdi(v, g.image)) g.backend = L"gdi";

    const bs::Rect bounds{0, 0, static_cast<float>(w), static_cast<float>(h)};
    EnumCtx wctx{v, bounds, &g.windows};
    EnumWindows(onWindow, reinterpret_cast<LPARAM>(&wctx));
    EnumCtx mctx{v, bounds, &g.monitors};
    EnumDisplayMonitors(nullptr, nullptr, onMonitor, reinterpret_cast<LPARAM>(&mctx));
    g.windows.insert(g.windows.end(), g.monitors.begin(), g.monitors.end());
    return g;
}
