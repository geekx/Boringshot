// Boringshot resident host: single instance, tray icon, configurable global
// hotkeys, the capture overlay, pinned images and the click-through watermark.

#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <memory>
#include <string>
#include <vector>

#include "capture_window.h"
#include "pin_window.h"
#include "settings.h"
#include "settings_dialog.h"
#include "watermark_overlay.h"
#ifdef BORINGSHOT_HAS_WGC
#include "capture_wgc.h"
#endif

namespace {

constexpr wchar_t kHostClass[] = L"Boringshot.Host";
constexpr wchar_t kMutexName[] = L"Local\\Boringshot.SingleInstance";
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kReapMessage = WM_APP + 2;  // delete closed capture/pin objects outside their wndProc
constexpr UINT kTrayId = 1;
constexpr int kHotkeyCapture = 1;
constexpr int kHotkeyCaptureAlt = 2;
constexpr int kHotkeyWatermark = 3;
constexpr UINT_PTR kClockTimer = 1;
constexpr UINT kClockIntervalMs = 15000;  // {time} has minute granularity

enum : UINT { kCmdCapture = 100, kCmdToggleWatermark, kCmdClosePins, kCmdSettings, kCmdExit };

HINSTANCE g_instance = nullptr;
HWND g_host = nullptr;
UINT g_taskbarCreated = 0;
WatermarkOverlay g_overlay;
std::unique_ptr<CaptureWindow> g_capture;
bool g_captureClosed = false;
std::vector<std::unique_ptr<PinWindow>> g_pins;
bs::Config g_config;
SettingsDialog g_settings;

void addTrayIcon(HWND hwnd) {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = kTrayId;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = kTrayMessage;
    nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    const std::wstring tip = L"Boringshot " BORINGSHOT_VERSION L" · " + bs::formatHotkey(g_config.capture) + L" 截图";
    wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_ADD, &nid);
    Shell_NotifyIconW(NIM_MODIFY, &nid);  // refresh the tooltip after a reload
}

void removeTrayIcon(HWND hwnd) {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = kTrayId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

// Tray balloon for problems the user can fix (taken hotkeys, bad config lines).
void notify(const std::wstring& title, const std::wstring& body) {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_host;
    nid.uID = kTrayId;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_WARNING;
    wcsncpy_s(nid.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(nid.szInfo, body.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
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
    CaptureOptions opts;
    opts.animate = g_config.animation;
    if (capture->open(g_instance, grabScreen(g_config.backend), std::move(cb), opts)) g_capture = std::move(capture);
}

void reap() {
    if (g_captureClosed) {
        g_capture.reset();
        g_captureClosed = false;
    }
    g_pins.erase(std::remove_if(g_pins.begin(), g_pins.end(), [](const auto& p) { return p->hwnd() == nullptr; }),
                 g_pins.end());
}

// (Re)binds every hotkey; returns the ones the system refused (taken by another app).
std::vector<std::wstring> registerHotkeys() {
    std::vector<std::wstring> failed;
    const struct {
        int id;
        bs::Hotkey key;
    } binds[] = {{kHotkeyCapture, g_config.capture},
                 {kHotkeyCaptureAlt, g_config.captureAlt},
                 {kHotkeyWatermark, g_config.watermark}};
    for (const auto& b : binds) {
        UnregisterHotKey(g_host, b.id);
        if (b.key.enabled() && !RegisterHotKey(g_host, b.id, b.key.modifiers | MOD_NOREPEAT, b.key.vk))
            failed.push_back(bs::formatHotkey(b.key));
    }
    return failed;
}

void applyConfig(const bs::Config& c) {
    g_config = c;
    g_overlay.updateProfile(g_config.watermarkProfile);
    const auto failed = registerHotkeys();
    addTrayIcon(g_host);
    std::wstring problems;
    for (const auto& f : failed) problems += L"热键 " + f + L" 已被其他程序占用，请在设置里换一个\n";
    for (const auto& w : g_config.warnings) problems += L"设置 " + w + L"\n";
    if (!problems.empty()) notify(L"Boringshot 设置", problems);
}

void openSettings() {
    g_settings.open(g_instance, g_config, [](const bs::Config& c) {
        if (!saveSettings(c)) {
            MessageBoxW(g_settings.hwnd(), L"无法写入注册表（HKCU\\Software\\Boringshot）。", L"Boringshot 设置",
                        MB_OK | MB_ICONERROR);
            return false;
        }
        applyConfig(c);
        return true;
    });
}

void toggleWatermark() { g_overlay.enable(!g_overlay.enabled()); }

void showTrayMenu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    const std::wstring capture = L"截图\t" + bs::formatHotkey(g_config.capture);
    const std::wstring watermark = L"水印\t" + bs::formatHotkey(g_config.watermark);
    AppendMenuW(menu, MF_STRING, kCmdCapture, capture.c_str());
    AppendMenuW(menu, MF_STRING | (g_overlay.enabled() ? MF_CHECKED : MF_UNCHECKED), kCmdToggleWatermark,
                watermark.c_str());
    AppendMenuW(menu, MF_STRING | (g_pins.empty() ? MF_GRAYED : 0u), kCmdClosePins, L"关闭全部贴图");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCmdSettings, L"设置…");
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
                case kCmdSettings: openSettings(); break;
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
            g_settings.close();
            for (auto& p : g_pins) p->close();
            g_overlay.destroy();
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

// Smoke progress goes to the inherited stderr (ctest pipe) so CI pinpoints a failing step.
void smokeLog(const std::wstring& step) {
    HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
    if (!err || err == INVALID_HANDLE_VALUE) return;
    const std::string line = wideToUtf8(L"smoke: " + step + L"\n");
    DWORD written = 0;
    WriteFile(err, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
}

// --smoke: exercise every real platform path (settings, capture backends,
// overlay UI, composite, pin, watermark) without user input, then exit.
// Non-zero = which step failed.
int runSmoke(HINSTANCE instance) {
    smokeLog(L"settings");
    {
        constexpr wchar_t kSmokeKey[] = L"Software\\Boringshot.Smoke";
        bs::Config c;
        c.capture = {bs::kModCtrl | bs::kModShift, 'X'};
        c.backend = bs::CaptureBackend::Gdi;
        c.animation = false;
        c.watermarkProfile.text = L"评审 {user}";
        c.watermarkProfile.opacity = 0.3f;
        c.watermarkProfile.rotationDeg = -45;
        if (!saveSettings(c, kSmokeKey)) return 9;
        const bs::Config r = loadSettings(kSmokeKey);
        deleteSettings(kSmokeKey);
        if (!r.warnings.empty() || !(r.capture == c.capture) || r.backend != c.backend || r.animation ||
            r.watermarkProfile.text != c.watermarkProfile.text)
            return 9;

        // Settings window: populate → collect round-trips every control.
        SettingsDialog dlg;
        if (!dlg.open(instance, c, [](const bs::Config&) { return true; })) return 10;
        const bs::Config back = dlg.collect();
        dlg.close();
        const auto& w = back.watermarkProfile;
        if (!(back.capture == c.capture) || !(back.captureAlt == c.captureAlt) || back.backend != c.backend ||
            back.animation || w.text != c.watermarkProfile.text || std::fabs(w.opacity - 0.3f) > 1e-4f ||
            std::fabs(w.rotationDeg + 45.0f) > 1e-4f || w.fontFamily != c.watermarkProfile.fontFamily) {
            smokeLog(L"settings window round-trip mismatch");
            return 10;
        }
    }

    smokeLog(L"watermark");
    WatermarkOverlay overlay;
    if (!overlay.create(instance)) return 2;
    bs::WatermarkProfile profile;
    profile.text = L"smoke {user} {date} {time}";
    overlay.updateProfile(profile);
    const bool watermarkOk = overlay.refresh(true);
    overlay.destroy();
    if (!watermarkOk) return 3;

#ifdef BORINGSHOT_HAS_WGC
    // WGC availability depends on the machine (CI runners may lack it): report, never fail.
    gfx::Image wgcImage;
    const RECT v{GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                 GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
                 GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
    const bool wgcOk = wgcSupported() && grabWithWgc(v, wgcImage) && !wgcImage.empty();
    smokeLog(std::wstring(L"wgc ") + (wgcSupported() ? L"supported" : L"unsupported") +
             (wgcBorderless() ? L", borderless" : L"") + (wgcOk ? L", grab ok" : L", grab skipped/failed"));
#endif

    ScreenGrab grab = grabScreen(bs::CaptureBackend::Gdi);
    smokeLog(std::wstring(L"grab via ") + grab.backend);
    if (grab.image.empty() || grab.monitors.empty()) return 4;
    const float w = std::min(320.0f, static_cast<float>(grab.image.width));
    const float h = std::min(200.0f, static_cast<float>(grab.image.height));

    smokeLog(L"capture window");
    CaptureWindow capture;
    if (!capture.open(instance, std::move(grab), {}, {})) return 5;
    const wchar_t* captureError = capture.smoke({0.0f, 0.0f, w, h});
    capture.close();
    if (captureError) {
        smokeLog(std::wstring(L"capture failed: ") + captureError);
        return 6;
    }

    smokeLog(L"pin");
    gfx::Image img;
    img.width = 120;
    img.height = 80;
    img.px.assign(static_cast<std::size_t>(img.width * img.height), 0xFF3366CCu);
    PinWindow pin;
    if (!pin.open(instance, img, {40, 40}, {})) return 7;
    const bool pinOk = pin.smoke();
    pin.close();
    smokeLog(pinOk ? L"ok" : L"pin failed");
    return pinOk ? 0 : 8;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR cmdLine, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))) return 1;
    g_instance = instance;
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_HOTKEY_CLASS | ICC_TAB_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    if (cmdLine && std::wcsstr(cmdLine, L"--smoke")) {
        const int rc = runSmoke(instance);
        gfx::shutdown();
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
    applyConfig(loadSettings());
    SetTimer(g_host, kClockTimer, kClockIntervalMs, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (g_settings.isOpen() && IsDialogMessageW(g_settings.hwnd(), &msg)) continue;  // Tab / Enter / Esc
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    g_capture.reset();
    g_pins.clear();
    gfx::shutdown();
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
