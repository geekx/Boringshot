// Boringshot resident host: single instance, tray icon, global hotkeys, the
// capture overlay, pinned images and the click-through watermark (doc §12).

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include <algorithm>
#include <cwchar>
#include <memory>
#include <vector>

#include "capture_window.h"
#include "pin_window.h"
#include "watermark_overlay.h"

namespace {

constexpr wchar_t kHostClass[] = L"Boringshot.Host";
constexpr wchar_t kMutexName[] = L"Local\\Boringshot.SingleInstance";
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kReapMessage = WM_APP + 2;  // delete closed capture/pin objects outside their wndProc
constexpr UINT kTrayId = 1;
constexpr int kHotkeyCapture = 1;       // F1
constexpr int kHotkeyCaptureAlt = 2;    // Ctrl+Alt+A
constexpr int kHotkeyWatermark = 3;     // Ctrl+Alt+W
constexpr UINT_PTR kClockTimer = 1;
constexpr UINT kClockIntervalMs = 15000;  // {time} has minute granularity

enum : UINT { kCmdCapture = 100, kCmdToggleWatermark, kCmdClosePins, kCmdExit };

HINSTANCE g_instance = nullptr;
HWND g_host = nullptr;
UINT g_taskbarCreated = 0;
WatermarkOverlay g_overlay;
std::unique_ptr<CaptureWindow> g_capture;
bool g_captureClosed = false;
std::vector<std::unique_ptr<PinWindow>> g_pins;

void addTrayIcon(HWND hwnd) {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = kTrayId;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = kTrayMessage;
    nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(nid.szTip, L"Boringshot " BORINGSHOT_VERSION L" · F1 截图");
    Shell_NotifyIconW(NIM_ADD, &nid);
}

void removeTrayIcon(HWND hwnd) {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = kTrayId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

void openPin(gfx::Image img, POINT at) {
    auto pin = std::make_unique<PinWindow>();
    if (pin->open(g_instance, std::move(img), at, [](PinWindow*) { PostMessageW(g_host, kReapMessage, 0, 0); }))
        g_pins.push_back(std::move(pin));
}

void startCapture() {
    if (g_capture) return;  // one session at a time
    auto capture = std::make_unique<CaptureWindow>();
    CaptureWindow::Callbacks cb;
    cb.pin = openPin;
    cb.closed = [] {
        g_captureClosed = true;
        PostMessageW(g_host, kReapMessage, 0, 0);
    };
    g_captureClosed = false;
    if (capture->open(g_instance, grabScreen(), std::move(cb))) g_capture = std::move(capture);
}

void reap() {
    if (g_captureClosed) {
        g_capture.reset();
        g_captureClosed = false;
    }
    g_pins.erase(std::remove_if(g_pins.begin(), g_pins.end(), [](const auto& p) { return p->hwnd() == nullptr; }),
                 g_pins.end());
}

void toggleWatermark() { g_overlay.enable(!g_overlay.enabled()); }

void showTrayMenu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kCmdCapture, L"截图\tF1 / Ctrl+Alt+A");
    AppendMenuW(menu, MF_STRING | (g_overlay.enabled() ? MF_CHECKED : MF_UNCHECKED), kCmdToggleWatermark,
                L"水印\tCtrl+Alt+W");
    AppendMenuW(menu, MF_STRING | (g_pins.empty() ? MF_GRAYED : 0u), kCmdClosePins, L"关闭全部贴图");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdExit, L"退出");
    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);  // required so the menu dismisses on outside click
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

LRESULT CALLBACK hostProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == g_taskbarCreated && g_taskbarCreated != 0) {  // Explorer restarted
        addTrayIcon(hwnd);
        return 0;
    }
    switch (msg) {
        case kTrayMessage:
            if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_CONTEXTMENU) showTrayMenu(hwnd);
            if (LOWORD(lp) == WM_LBUTTONUP) startCapture();
            return 0;
        case kReapMessage:
            reap();
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case kCmdCapture: startCapture(); break;
                case kCmdToggleWatermark: toggleWatermark(); break;
                case kCmdClosePins:
                    for (auto& p : g_pins) p->close();
                    break;
                case kCmdExit: DestroyWindow(hwnd); break;
                default: break;
            }
            return 0;
        case WM_HOTKEY: {
            const int id = static_cast<int>(wp);
            if (id == kHotkeyCapture || id == kHotkeyCaptureAlt) startCapture();
            if (id == kHotkeyWatermark) toggleWatermark();
            return 0;
        }
        case WM_TIMER:
            if (wp == kClockTimer && g_overlay.enabled()) g_overlay.refresh(false);
            return 0;
        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED:
            if (g_overlay.enabled()) g_overlay.refresh(true);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, kClockTimer);
            UnregisterHotKey(hwnd, kHotkeyCapture);
            UnregisterHotKey(hwnd, kHotkeyCaptureAlt);
            UnregisterHotKey(hwnd, kHotkeyWatermark);
            removeTrayIcon(hwnd);
            if (g_capture) g_capture->close();
            for (auto& p : g_pins) p->close();
            g_overlay.destroy();
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

// --smoke: exercise every real platform path (capture, overlay UI, composite,
// pin, watermark) without user input, then exit. Non-zero = which step failed.
int runSmoke(HINSTANCE instance) {
    WatermarkOverlay overlay;
    if (!overlay.create(instance)) return 2;
    bs::WatermarkProfile profile;
    profile.text = L"smoke {user} {date} {time}";
    overlay.updateProfile(profile);
    const bool watermarkOk = overlay.refresh(true);
    overlay.destroy();
    if (!watermarkOk) return 3;

    ScreenGrab grab = grabScreen();
    if (grab.image.empty() || grab.monitors.empty()) return 4;
    const float w = std::min(320.0f, static_cast<float>(grab.image.width));
    const float h = std::min(200.0f, static_cast<float>(grab.image.height));
    CaptureWindow capture;
    if (!capture.open(instance, std::move(grab), {})) return 5;
    const bool captureOk = capture.smoke({0.0f, 0.0f, w, h});
    capture.close();
    if (!captureOk) return 6;

    gfx::Image img;
    img.width = 120;
    img.height = 80;
    img.px.assign(static_cast<std::size_t>(img.width * img.height), 0xFF3366CCu);
    PinWindow pin;
    if (!pin.open(instance, img, {40, 40}, {})) return 7;
    const bool pinOk = pin.smoke();
    pin.close();
    return pinOk ? 0 : 8;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR cmdLine, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) return 1;
    g_instance = instance;
    if (cmdLine && std::wcsstr(cmdLine, L"--smoke")) {
        const int rc = runSmoke(instance);
        CoUninitialize();
        return rc;
    }

    HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (mutex) CloseHandle(mutex);
        return 0;  // already running: never stack a second overlay
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = hostProc;
    wc.hInstance = instance;
    wc.lpszClassName = kHostClass;
    RegisterClassExW(&wc);
    // Hidden top-level (not HWND_MESSAGE) so it receives TaskbarCreated broadcasts.
    g_host = CreateWindowExW(WS_EX_TOOLWINDOW, kHostClass, L"Boringshot", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
                             instance, nullptr);
    if (!g_host || !g_overlay.create(instance)) {
        CloseHandle(mutex);
        return 1;
    }

    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    addTrayIcon(g_host);
    RegisterHotKey(g_host, kHotkeyCapture, MOD_NOREPEAT, VK_F1);  // may be taken; Ctrl+Alt+A is the fallback
    RegisterHotKey(g_host, kHotkeyCaptureAlt, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'A');
    RegisterHotKey(g_host, kHotkeyWatermark, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'W');
    SetTimer(g_host, kClockTimer, kClockIntervalMs, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    g_capture.reset();
    g_pins.clear();
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
