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
// Inverse of parseConfig (warnings dropped); used to persist settings.
std::wstring serializeConfig(const Config& c);

// msctls_hotkey32 value: vk in the low byte, HOTKEYF_SHIFT=1 / CONTROL=2 /
// ALT=4 / EXT=8 in the high byte (note: Shift and Alt are swapped vs MOD_*).
Hotkey hotkeyFromControl(unsigned value);
unsigned hotkeyToControl(const Hotkey& h);

// Dropdown presets for the settings window. *Index() returns -1 for a custom value.
struct ColorPreset {
    const wchar_t* name;
    float r, g, b;
};
inline constexpr ColorPreset kWatermarkColors[] = {
    {L"灰色", 128 / 255.0f, 128 / 255.0f, 128 / 255.0f}, {L"红色", 0.86f, 0.15f, 0.15f},
    {L"蓝色", 0.15f, 0.39f, 0.92f},                      {L"黑色", 0.0f, 0.0f, 0.0f},
    {L"白色", 1.0f, 1.0f, 1.0f},
};
int colorPresetIndex(float r, float g, float b);

struct SpacingPreset {
    const wchar_t* name;
    float x, y;
};
inline constexpr SpacingPreset kWatermarkSpacings[] = {
    {L"紧凑", 240, 140}, {L"标准", 320, 180}, {L"宽松", 420, 240}};
int spacingPresetIndex(float x, float y);

// "#RRGGBB" → 0..1 channels.
bool parseHexColor(const std::wstring& text, float& r, float& g, float& b);

}  // namespace bs
