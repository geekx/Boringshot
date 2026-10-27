#pragma once

#include <windows.h>

#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <string>

#include "boringshot/watermark.h"

// Topmost, click-through, Alt+Tab-invisible layered window spanning the
// virtual desktop (doc §3.3 / §8). The watermark is rasterised once into a DIB
// and handed to DWM via UpdateLayeredWindow, so steady state costs nothing;
// we only re-render when the profile, displays, or expanded text change.
class WatermarkOverlay {
public:
    bool create(HINSTANCE instance);
    void destroy();

    void enable(bool on);
    bool enabled() const { return enabled_; }

    void updateProfile(const bs::WatermarkProfile& profile);
    // Re-expands the template; re-renders when the text changed or `force`.
    bool refresh(bool force);

    const bs::WatermarkProfile& profile() const { return profile_; }

private:
    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    bool render();
    std::wstring expandedText() const;

    HWND hwnd_ = nullptr;
    bool enabled_ = false;
    bs::WatermarkProfile profile_;
    std::wstring lastText_;
    Microsoft::WRL::ComPtr<ID2D1Factory> d2d_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwrite_;
};
