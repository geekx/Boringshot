#pragma once

#include <windows.h>

#include <functional>
#include <vector>

#include "boringshot/config.h"

// Native settings window: hotkey pickers, dropdowns and checkboxes with a
// hover tooltip on every control. Modeless; the host's message loop routes
// keyboard navigation through IsDialogMessage(hwnd()).
class SettingsDialog {
public:
    using ApplyFn = std::function<bool(const bs::Config&)>;  // false keeps the window open

    bool open(HINSTANCE instance, const bs::Config& current, ApplyFn onApply);
    void close();
    HWND hwnd() const { return hwnd_; }
    bool isOpen() const { return hwnd_ != nullptr; }

    void populate(const bs::Config& c);
    bs::Config collect() const;

private:
    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);
    void build();
    HWND label(const wchar_t* text, int x, int y, int w);
    HWND control(const wchar_t* cls, DWORD style, int id, int x, int y, int w, int h, const wchar_t* text = L"");
    void tip(HWND ctrl, const wchar_t* text);
    bool apply();
    int px(int v) const { return MulDiv(v, static_cast<int>(dpi_), 96); }

    HWND hwnd_ = nullptr;
    HWND tips_ = nullptr;
    HFONT font_ = nullptr;
    UINT dpi_ = 96;
    ApplyFn onApply_;
    bs::Config base_;  // keeps what the controls can't express (Win-key hotkeys, custom values)
    std::vector<float> opacities_, angles_;

    HWND capture_ = nullptr, captureAlt_ = nullptr, watermarkKey_ = nullptr;
    HWND backend_ = nullptr, animation_ = nullptr;
    HWND text_ = nullptr, font_family_ = nullptr, size_ = nullptr, color_ = nullptr;
    HWND opacity_ = nullptr, angle_ = nullptr, spacing_ = nullptr, allMonitors_ = nullptr;
};
