// Boringshot resident host: single instance, tray icon, global hotkey and the
// click-through watermark overlay (doc §12). Capture/annotation UI lands next.

#include <windows.h>
#include <shellapi.h>

#include <cwchar>

#include "watermark_overlay.h"

namespace {

constexpr wchar_t kHostClass[] = L"Boringshot.Host";
constexpr wchar_t kMutexName[] = L"Local\\Boringshot.SingleInstance";
constexpr UINT kTrayMessage = WM_APP + 1;
constexpr UINT kTrayId = 1;
constexpr int kHotkeyWatermark = 1;       // Ctrl+Alt+W
constexpr UINT_PTR kClockTimer = 1;
constexpr UINT kClockIntervalMs = 15000;  // {time} has minute granularity

enum : UINT { kCmdToggleWatermark = 100, kCmdExit };

WatermarkOverlay g_overlay;
UINT g_taskbarCreated = 0;

void addTrayIcon(HWND hwnd) {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = kTrayId;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = kTrayMessage;
    nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(nid.szTip, L"Boringshot " BORINGSHOT_VERSION);
    Shell_NotifyIconW(NIM_ADD, &nid);
}

void removeTrayIcon(HWND hwnd) {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = kTrayId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

void toggleWatermark() { g_overlay.enable(!g_overlay.enabled()); }

void showTrayMenu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING | (g_overlay.enabled() ? MF_CHECKED : MF_UNCHECKED),
                kCmdToggleWatermark, L"水印  Ctrl+Alt+W");
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
            if (LOWORD(lp) == WM_LBUTTONDBLCLK) toggleWatermark();
            return 0;
        case WM_COMMAND:
            if (LOWORD(wp) == kCmdToggleWatermark) toggleWatermark();
            if (LOWORD(wp) == kCmdExit) DestroyWindow(hwnd);
            return 0;
        case WM_HOTKEY:
            if (static_cast<int>(wp) == kHotkeyWatermark) toggleWatermark();
            return 0;
        case WM_TIMER:
            if (wp == kClockTimer && g_overlay.enabled()) g_overlay.refresh(false);
            return 0;
        case WM_DISPLAYCHANGE:
        case WM_DPICHANGED:
            if (g_overlay.enabled()) g_overlay.refresh(true);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, kClockTimer);
            UnregisterHotKey(hwnd, kHotkeyWatermark);
            removeTrayIcon(hwnd);
            g_overlay.destroy();
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

// --smoke: exercise the real overlay path (window + D2D + layered update) and exit.
int runSmoke(HINSTANCE instance) {
    WatermarkOverlay overlay;
    if (!overlay.create(instance)) return 2;
    bs::WatermarkProfile profile;
    profile.text = L"smoke {user} {date} {time}";
    overlay.updateProfile(profile);
    const bool ok = overlay.refresh(true);
    overlay.destroy();
    return ok ? 0 : 3;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR cmdLine, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (cmdLine && std::wcsstr(cmdLine, L"--smoke")) return runSmoke(instance);

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
    HWND host = CreateWindowExW(WS_EX_TOOLWINDOW, kHostClass, L"Boringshot", WS_POPUP, 0, 0, 0, 0,
                                nullptr, nullptr, instance, nullptr);
    if (!host || !g_overlay.create(instance)) {
        CloseHandle(mutex);
        return 1;
    }

    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    addTrayIcon(host);
    RegisterHotKey(host, kHotkeyWatermark, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'W');
    SetTimer(host, kClockTimer, kClockIntervalMs, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return static_cast<int>(msg.wParam);
}
