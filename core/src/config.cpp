#include "boringshot/config.h"

#include <cwchar>

namespace bs {
namespace {

// ASCII-only helpers: config keys are ASCII, values keep their Unicode as-is.
bool isSpace(wchar_t c) { return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == L'\v' || c == L'\f'; }
wchar_t toLower(wchar_t c) { return c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c - L'A' + L'a') : c; }
wchar_t toUpper(wchar_t c) { return c >= L'a' && c <= L'z' ? static_cast<wchar_t>(c - L'a' + L'A') : c; }

std::wstring trim(const std::wstring& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && isSpace(s[b])) ++b;
    while (e > b && isSpace(s[e - 1])) --e;
    return s.substr(b, e - b);
}

std::wstring lower(std::wstring s) {
    for (auto& c : s) c = toLower(c);
    return s;
}

struct NamedKey {
    const wchar_t* name;
    unsigned vk;
};

constexpr NamedKey kNamedKeys[] = {
    {L"space", 0x20},     {L"printscreen", 0x2C}, {L"prtsc", 0x2C},   {L"insert", 0x2D}, {L"delete", 0x2E},
    {L"home", 0x24},      {L"end", 0x23},         {L"pageup", 0x21},  {L"pagedown", 0x22}, {L"pause", 0x13},
    {L"tab", 0x09},       {L"`", 0xC0},
};

std::optional<unsigned> keyCode(const std::wstring& k) {
    if (k.size() == 1 && ((k[0] >= L'a' && k[0] <= L'z') || (k[0] >= L'0' && k[0] <= L'9')))
        return static_cast<unsigned>(toUpper(k[0]));
    if (k.size() >= 2 && k[0] == L'f') {
        wchar_t* end = nullptr;
        const long n = std::wcstol(k.c_str() + 1, &end, 10);
        if (end && *end == 0 && n >= 1 && n <= 24) return 0x70u + static_cast<unsigned>(n - 1);
    }
    for (const auto& nk : kNamedKeys)
        if (k == nk.name) return nk.vk;
    return std::nullopt;
}

bool parseBool(const std::wstring& v, bool& out) {
    const std::wstring l = lower(v);
    if (l == L"true" || l == L"on" || l == L"yes" || l == L"1") return out = true, true;
    if (l == L"false" || l == L"off" || l == L"no" || l == L"0") return out = false, true;
    return false;
}

bool parseFloat(const std::wstring& v, float& out) {
    wchar_t* end = nullptr;
    const float f = std::wcstof(v.c_str(), &end);
    if (v.empty() || !end || *end != 0) return false;
    out = f;
    return true;
}

}  // namespace

std::optional<Hotkey> parseHotkey(const std::wstring& text) {
    const std::wstring t = lower(trim(text));
    if (t.empty() || t == L"none" || t == L"off") return Hotkey{};
    Hotkey h;
    std::size_t start = 0;
    while (true) {
        const std::size_t plus = t.find(L'+', start);
        const std::wstring part = trim(t.substr(start, plus == std::wstring::npos ? std::wstring::npos : plus - start));
        const bool last = plus == std::wstring::npos;
        if (part.empty()) return std::nullopt;
        if (!last) {
            if (part == L"ctrl" || part == L"control") h.modifiers |= kModCtrl;
            else if (part == L"alt") h.modifiers |= kModAlt;
            else if (part == L"shift") h.modifiers |= kModShift;
            else if (part == L"win") h.modifiers |= kModWin;
            else return std::nullopt;
        } else {
            const auto vk = keyCode(part);
            if (!vk) return std::nullopt;
            h.vk = *vk;
            return h;
        }
        start = plus + 1;
    }
}

std::wstring formatHotkey(const Hotkey& h) {
    if (!h.enabled()) return L"none";
    std::wstring s;
    if (h.modifiers & kModCtrl) s += L"Ctrl+";
    if (h.modifiers & kModAlt) s += L"Alt+";
    if (h.modifiers & kModShift) s += L"Shift+";
    if (h.modifiers & kModWin) s += L"Win+";
    if (h.vk >= 0x70 && h.vk <= 0x87) return s + L"F" + std::to_wstring(h.vk - 0x6F);
    if ((h.vk >= 'A' && h.vk <= 'Z') || (h.vk >= '0' && h.vk <= '9')) return s + static_cast<wchar_t>(h.vk);
    for (const auto& nk : kNamedKeys)
        if (nk.vk == h.vk) {
            std::wstring n = nk.name;
            n[0] = toUpper(n[0]);
            return s + n;
        }
    return s + L"?";
}

bool parseHexColor(const std::wstring& text, float& r, float& g, float& b) {
    const std::wstring t = trim(text);
    if (t.size() != 7 || t[0] != L'#') return false;
    wchar_t* end = nullptr;
    const unsigned long v = std::wcstoul(t.c_str() + 1, &end, 16);
    if (!end || *end != 0) return false;
    r = static_cast<float>((v >> 16) & 0xFF) / 255.0f;
    g = static_cast<float>((v >> 8) & 0xFF) / 255.0f;
    b = static_cast<float>(v & 0xFF) / 255.0f;
    return true;
}

Config parseConfig(const std::wstring& text) {
    Config cfg;
    std::wstring section;
    std::size_t start = 0;
    int lineNo = 0;
    while (start < text.size()) {
        std::size_t nl = text.find(L'\n', start);
        if (nl == std::wstring::npos) nl = text.size();
        std::wstring line = trim(text.substr(start, nl - start));
        start = nl + 1;
        ++lineNo;
        if (!line.empty() && line[0] == L'﻿') line = trim(line.substr(1));  // UTF-8 BOM decoded
        if (line.empty() || line[0] == L';' || line[0] == L'#') continue;
        const auto warn = [&](const std::wstring& what) {
            cfg.warnings.push_back(L"line " + std::to_wstring(lineNo) + L": " + what);
        };
        if (line.front() == L'[') {
            if (line.back() != L']') {
                warn(L"bad section");
                continue;
            }
            section = lower(trim(line.substr(1, line.size() - 2)));
            continue;
        }
        const std::size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) {
            warn(L"expected key = value");
            continue;
        }
        const std::wstring key = lower(trim(line.substr(0, eq)));
        const std::wstring value = trim(line.substr(eq + 1));
        const std::wstring id = section + L"." + key;
        auto& wm = cfg.watermarkProfile;
        bool ok = true;
        if (section == L"hotkeys" && (key == L"capture" || key == L"capture_alt" || key == L"watermark")) {
            const auto h = parseHotkey(value);
            if (!h) ok = false;
            else (key == L"capture" ? cfg.capture : key == L"capture_alt" ? cfg.captureAlt : cfg.watermark) = *h;
        } else if (id == L"capture.backend") {
            const std::wstring v = lower(value);
            if (v == L"auto") cfg.backend = CaptureBackend::Auto;
            else if (v == L"wgc") cfg.backend = CaptureBackend::Wgc;
            else if (v == L"gdi") cfg.backend = CaptureBackend::Gdi;
            else ok = false;
        } else if (id == L"capture.animation") {
            ok = parseBool(value, cfg.animation);
        } else if (id == L"watermark.text") {
            wm.text = value;
        } else if (id == L"watermark.font") {
            ok = !value.empty();
            if (ok) wm.fontFamily = value;
        } else if (id == L"watermark.size") {
            ok = parseFloat(value, wm.fontSize) && wm.fontSize > 0;
        } else if (id == L"watermark.opacity") {
            ok = parseFloat(value, wm.opacity) && wm.opacity >= 0 && wm.opacity <= 1;
        } else if (id == L"watermark.angle") {
            ok = parseFloat(value, wm.rotationDeg);
        } else if (id == L"watermark.spacing_x") {
            ok = parseFloat(value, wm.tileSpacingX) && wm.tileSpacingX >= 1;
        } else if (id == L"watermark.spacing_y") {
            ok = parseFloat(value, wm.tileSpacingY) && wm.tileSpacingY >= 1;
        } else if (id == L"watermark.color") {
            ok = parseHexColor(value, wm.colorR, wm.colorG, wm.colorB);
        } else if (id == L"watermark.all_monitors") {
            ok = parseBool(value, wm.enableOnAllMonitors);
        } else {
            warn(L"unknown key " + id);
            continue;
        }
        if (!ok) warn(L"invalid value for " + id + L": " + value);
    }
    if (cfg.watermarkProfile.opacity < 0 || cfg.watermarkProfile.opacity > 1) cfg.watermarkProfile.opacity = 0.18f;
    return cfg;
}

std::wstring defaultConfigText() {
    return L"; Boringshot 设置 — 保存后自动生效\n"
           L"[hotkeys]\n"
           L"; 例如 F1 / Ctrl+Alt+A / Shift+PrintScreen / none\n"
           L"capture = F1\n"
           L"capture_alt = Ctrl+Alt+A\n"
           L"watermark = Ctrl+Alt+W\n"
           L"\n"
           L"[capture]\n"
           L"; auto = Win11 用 WGC（无黄框），其余用 GDI；也可强制 wgc / gdi\n"
           L"backend = auto\n"
           L"; 呼出水波纹 + 抖动\n"
           L"animation = true\n"
           L"\n"
           L"[watermark]\n"
           L"; 变量 {user} {date} {time}\n"
           L"text = {user}  {date} {time}\n"
           L"font = Segoe UI\n"
           L"size = 18\n"
           L"color = #808080\n"
           L"opacity = 0.18\n"
           L"angle = -30\n"
           L"spacing_x = 320\n"
           L"spacing_y = 180\n"
           L"all_monitors = true\n";
}

}  // namespace bs
