#pragma once

#include <windows.h>

#include <vector>

#include "boringshot/geometry.h"
#include "gfx.h"

// Frozen copy of the whole virtual desktop plus the snap targets under it.
struct ScreenGrab {
    RECT virtualRect{};               // desktop coordinates of pixel (0,0)
    gfx::Image image;
    std::vector<bs::Rect> windows;    // local coords, Z order (top first), monitors last
    std::vector<bs::Rect> monitors;   // local coords
};

// GDI BitBlt grab (WGC comes later); must run before any overlay is shown.
ScreenGrab grabScreen();
