#include "boringshot/config.h"
#include "bstest.h"

using namespace bs;

TEST(hotkey_parse_variants) {
    CHECK((*parseHotkey(L"F1") == Hotkey{0, 0x70}));
    CHECK((*parseHotkey(L"ctrl + alt + a") == Hotkey{kModCtrl | kModAlt, 'A'}));
    CHECK((*parseHotkey(L"Shift+PrintScreen") == Hotkey{kModShift, 0x2C}));
    CHECK((*parseHotkey(L"Win+Shift+S") == Hotkey{kModWin | kModShift, 'S'}));
    CHECK((*parseHotkey(L"F24") == Hotkey{0, 0x87}));
    CHECK(!parseHotkey(L"none")->enabled());
    CHECK(!parseHotkey(L"")->enabled());
    CHECK(!parseHotkey(L"Ctrl+").has_value());
    CHECK(!parseHotkey(L"Hyper+A").has_value());
    CHECK(!parseHotkey(L"F25").has_value());
    CHECK(!parseHotkey(L"Ctrl+Banana").has_value());
}

TEST(hotkey_format_roundtrip) {
    for (const wchar_t* s : {L"F1", L"Ctrl+Alt+A", L"Shift+Printscreen", L"Ctrl+Shift+Win+9", L"none"}) {
        const auto h = parseHotkey(s);
        CHECK(h.has_value());
        CHECK((*parseHotkey(formatHotkey(*h)) == *h));
    }
    CHECK(formatHotkey({kModCtrl | kModAlt, 'A'}) == L"Ctrl+Alt+A");
}

TEST(config_defaults_roundtrip) {
    const Config c = parseConfig(defaultConfigText());
    const Config d;
    CHECK(c.warnings.empty());
    CHECK(c.capture == d.capture && c.captureAlt == d.captureAlt && c.watermark == d.watermark);
    CHECK(c.backend == CaptureBackend::Auto && c.animation);
    CHECK(c.watermarkProfile.text == d.watermarkProfile.text);
    CHECK_NEAR(c.watermarkProfile.opacity, d.watermarkProfile.opacity, 1e-6);
    CHECK_NEAR(c.watermarkProfile.colorR, 128.0 / 255.0, 1e-6);
}

TEST(config_overrides_and_warnings) {
    const Config c = parseConfig(
        L"﻿[hotkeys]\r\ncapture = Ctrl+Shift+X\r\nwatermark = none\n"
        L"[capture]\nbackend = GDI\nanimation = off\n"
        L"[watermark]\ntext = 评审稿 {user}; 勿外传\nopacity = 2\ncolor = #FF0000\n"
        L"bogus line\nunknown = 1\n[broken\n");
    CHECK((c.capture == Hotkey{kModCtrl | kModShift, 'X'}));
    CHECK(!c.watermark.enabled());
    CHECK(c.backend == CaptureBackend::Gdi && !c.animation);
    CHECK(c.watermarkProfile.text == L"评审稿 {user}; 勿外传");  // ';' inside values is kept
    CHECK_NEAR(c.watermarkProfile.opacity, 0.18, 1e-6);        // out of range → default kept
    CHECK_NEAR(c.watermarkProfile.colorR, 1.0, 1e-6);
    CHECK(c.warnings.size() == 4);  // opacity, bogus line, unknown key, broken section
}

TEST(config_hex_color) {
    float r = 0, g = 0, b = 0;
    CHECK(parseHexColor(L"#00FF80", r, g, b));
    CHECK_NEAR(g, 1.0, 1e-6);
    CHECK_NEAR(b, 128.0 / 255.0, 1e-6);
    CHECK(!parseHexColor(L"00FF80", r, g, b));
    CHECK(!parseHexColor(L"#00FF8", r, g, b));
    CHECK(!parseHexColor(L"#GG0000", r, g, b));
}

TEST(config_serialize_roundtrip) {
    Config c;
    c.capture = {kModCtrl | kModShift, 'X'};
    c.captureAlt = {};
    c.backend = CaptureBackend::Wgc;
    c.animation = false;
    c.watermarkProfile.text = L"内部评审 {user}; 勿外传";
    c.watermarkProfile.fontFamily = L"KaiTi";
    c.watermarkProfile.fontSize = 22.5f;
    c.watermarkProfile.colorR = 0.86f;
    c.watermarkProfile.colorG = 0.15f;
    c.watermarkProfile.colorB = 0.15f;
    c.watermarkProfile.opacity = 0.3f;
    c.watermarkProfile.rotationDeg = -45;
    c.watermarkProfile.tileSpacingX = 420;
    c.watermarkProfile.tileSpacingY = 240;
    c.watermarkProfile.enableOnAllMonitors = false;
    const Config r = parseConfig(serializeConfig(c));
    CHECK(r.warnings.empty());
    CHECK(r.capture == c.capture && !r.captureAlt.enabled() && r.watermark == c.watermark);
    CHECK(r.backend == CaptureBackend::Wgc && !r.animation);
    CHECK(r.watermarkProfile.text == c.watermarkProfile.text);
    CHECK(r.watermarkProfile.fontFamily == L"KaiTi");
    CHECK_NEAR(r.watermarkProfile.fontSize, 22.5, 1e-4);
    CHECK_NEAR(r.watermarkProfile.opacity, 0.3, 1e-4);
    CHECK_NEAR(r.watermarkProfile.rotationDeg, -45, 1e-4);
    CHECK(!r.watermarkProfile.enableOnAllMonitors);
    CHECK(colorPresetIndex(r.watermarkProfile.colorR, r.watermarkProfile.colorG, r.watermarkProfile.colorB) == 1);
    CHECK(spacingPresetIndex(r.watermarkProfile.tileSpacingX, r.watermarkProfile.tileSpacingY) == 2);
}

TEST(config_hotkey_control_mapping) {
    // HOTKEYF_SHIFT=1, CONTROL=2, ALT=4 in the high byte.
    CHECK((hotkeyFromControl((0x2u | 0x4u) << 8 | 'A') == Hotkey{kModCtrl | kModAlt, 'A'}));
    CHECK((hotkeyFromControl(0x1u << 8 | 0x70) == Hotkey{kModShift, 0x70}));
    CHECK(!hotkeyFromControl(0x2u << 8).enabled());  // modifiers alone = nothing
    CHECK(hotkeyFromControl(0x2u << 8).modifiers == 0);
    for (const Hotkey h : {Hotkey{kModCtrl | kModAlt, 'A'}, Hotkey{kModShift, 0x2C}, Hotkey{0, 0x70}, Hotkey{}})
        CHECK((hotkeyFromControl(hotkeyToControl(h)) == h));
}

TEST(config_presets) {
    CHECK(colorPresetIndex(128 / 255.0f, 128 / 255.0f, 128 / 255.0f) == 0);
    CHECK(colorPresetIndex(0.3f, 0.7f, 0.1f) == -1);
    CHECK(spacingPresetIndex(320, 180) == 1);
    CHECK(spacingPresetIndex(321, 180) == -1);
    const Config d;
    CHECK(spacingPresetIndex(d.watermarkProfile.tileSpacingX, d.watermarkProfile.tileSpacingY) == 1);
    CHECK(colorPresetIndex(d.watermarkProfile.colorR, d.watermarkProfile.colorG, d.watermarkProfile.colorB) == 0);
}

TEST(config_validate_duplicates) {
    Config c;
    CHECK(validateConfig(c).empty());
    c.watermark = c.capture;
    const auto p = validateConfig(c);
    CHECK(p.size() == 1 && p[0].find(L"F1") != std::wstring::npos);
    Config off;
    off.capture = {};
    off.captureAlt = {};
    CHECK(validateConfig(off).size() == 1);  // warns that only the tray can capture
    Config none;
    none.capture = {};
    none.watermark = {};
    CHECK(validateConfig(none).empty());  // disabled keys never collide
}
