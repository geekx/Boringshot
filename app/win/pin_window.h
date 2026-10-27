#pragma once

#include <windows.h>

#include <d2d1.h>
#include <wrl/client.h>

#include <functional>

#include "gfx.h"

// Screenshot pinned to the desktop: topmost, drag to move, wheel to zoom,
// Ctrl+wheel for opacity, double-click / Esc to dismiss, right-click menu.
class PinWindow {
public:
    // `closed` fires from WM_NCDESTROY; the owner must defer deleting us.
    bool open(HINSTANCE instance, gfx::Image image, POINT topLeft, std::function<void(PinWindow*)> closed);
    void close();
    HWND hwnd() const { return hwnd_; }

    // Test hook: render a frame and run one zoom step.
    bool smoke();

private:
    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);
    void paint();
    void zoom(int wheelDelta, POINT anchor);
    void setOpacity(int alpha);
    void showMenu(POINT at);

    HWND hwnd_ = nullptr;
    gfx::Image image_;
    float scale_ = 1.0f;
    int alpha_ = 255;
    std::function<void(PinWindow*)> closed_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> rt_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
};
