#pragma once

#include <windows.h>

#include "gfx.h"

// Windows.Graphics.Capture backend (MSVC builds only; see BORINGSHOT_HAS_WGC).
bool wgcSupported();
// Win11+: the yellow capture border can be switched off, so WGC is the auto choice.
bool wgcBorderless();
// Captures every monitor into `out`, laid out in virtual-desktop space.
bool grabWithWgc(const RECT& virtualRect, gfx::Image& out);
