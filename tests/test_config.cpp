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
