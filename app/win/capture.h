#pragma once

#include <windows.h>

#include <vector>

#include "boringshot/config.h"
#include "boringshot/geometry.h"
#include "gfx.h"

// Frozen copy of the whole virtual desktop plus the snap targets under it.
struct ScreenGrab {
    RECT virtualRect{};               // desktop coordinates of pixel (0,0)
    gfx::Image image;
    std::vector<bs::Rect> windows;    // local coords, Z order (top first), monitors last
    std::vector<bs::Rect> monitors;   // local coords
    const wchar_t* backend = L"none"; // which path produced `image`
};

// Must run before any overlay is shown. Auto picks WGC where its border can be
// disabled (Win11), GDI BitBlt otherwise; a failed WGC grab falls back to GDI.
ScreenGrab grabScreen(bs::CaptureBackend backend = bs::CaptureBackend::Auto);
