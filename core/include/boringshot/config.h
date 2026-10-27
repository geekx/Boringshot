#pragma once

#include <optional>
#include <string>
#include <vector>

#include "boringshot/watermark.h"

namespace bs {

// Modifier bits match Win32 MOD_ALT / MOD_CONTROL / MOD_SHIFT / MOD_WIN.
inline constexpr unsigned kModAlt = 0x1, kModCtrl = 0x2, kModShift = 0x4, kModWin = 0x8;

struct Hotkey {
    unsigned modifiers = 0;
    unsigned vk = 0;  // Win32 virtual-key code; 0 = disabled
    bool enabled() const { return vk != 0; }
    constexpr bool operator==(const Hotkey&) const = default;
};

// "Ctrl+Alt+A", "F1", "Shift + PrintScreen", "none". nullopt on syntax error.
std::optional<Hotkey> parseHotkey(const std::wstring& text);
std::wstring formatHotkey(const Hotkey& h);

enum class CaptureBackend { Auto, Wgc, Gdi };

struct Config {
    Hotkey capture{0, 0x70};              // F1
    Hotkey captureAlt{kModCtrl | kModAlt, 'A'};
    Hotkey watermark{kModCtrl | kModAlt, 'W'};
    CaptureBackend backend = CaptureBackend::Auto;
    bool animation = true;
    WatermarkProfile watermarkProfile;
    std::vector<std::wstring> warnings;  // bad lines are reported, never fatal
};

// INI subset: [section], key = value, ';' / '#' full-line comments.
Config parseConfig(const std::wstring& text);
std::wstring defaultConfigText();

// "#RRGGBB" → 0..1 channels.
bool parseHexColor(const std::wstring& text, float& r, float& g, float& b);

}  // namespace bs
