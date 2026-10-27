#include "settings_dialog.h"

#include <commctrl.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <string>

namespace {

constexpr wchar_t kClassName[] = L"Boringshot.Settings";
enum : int {
    kIdCapture = 1001, kIdCaptureAlt, kIdWatermark, kIdBackend, kIdAnimation, kIdText, kIdFont, kIdSize, kIdColor,
    kIdOpacity, kIdAngle, kIdSpacing, kIdAllMonitors, kIdApply
};

const wchar_t* const kBackends[] = {L"自动（推荐）", L"WGC（Windows.Graphics.Capture）", L"GDI（兼容性最好）"};
const wchar_t* const kFonts[] = {L"Segoe UI", L"Microsoft YaHei UI", L"SimSun", L"KaiTi", L"SimHei", L"Arial", L"Consolas"};
const float kSizes[] = {12, 14, 16, 18, 20, 24, 28, 32, 40};
const float kOpacities[] = {0.05f, 0.10f, 0.15f, 0.18f, 0.20f, 0.25f, 0.30f, 0.40f, 0.50f, 0.60f};
const float kAngles[] = {-60, -45, -30, -15, 0, 15, 30, 45, 60};

std::wstring fmt(float v, const wchar_t* suffix) {
    wchar_t buf[32];
    std::swprintf(buf, 32, L"%g%ls", static_cast<double>(v), suffix);
    return buf;
}

// Preset list with the current value merged in (sorted), so a custom value is never lost.
std::vector<float> withCurrent(const float* presets, std::size_t n, float current) {
    std::vector<float> v(presets, presets + n);
    if (std::none_of(v.begin(), v.end(), [&](float p) { return std::fabs(p - current) < 1e-4f; })) v.push_back(current);
    std::sort(v.begin(), v.end());
    return v;
}

int indexOf(const std::vector<float>& v, float x) {
    for (std::size_t i = 0; i < v.size(); ++i)
        if (std::fabs(v[i] - x) < 1e-4f) return static_cast<int>(i);
    return 0;
}

std::wstring windowText(HWND h) {
    const int n = GetWindowTextLengthW(h);
    std::wstring s(static_cast<std::size_t>(n) + 1, L'\0');
    GetWindowTextW(h, s.data(), n + 1);
    s.resize(static_cast<std::size_t>(n));
    return s;
}

int comboIndex(HWND h) { return static_cast<int>(SendMessageW(h, CB_GETCURSEL, 0, 0)); }

void addItem(HWND combo, const std::wstring& s) {
    SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s.c_str()));
}

}  // namespace

bool SettingsDialog::open(HINSTANCE instance, const bs::Config& current, ApplyFn onApply) {
    if (hwnd_) {
        SetForegroundWindow(hwnd_);
        return true;
    }
    onApply_ = std::move(onApply);
    dpi_ = GetDpiForSystem();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &SettingsDialog::wndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    RECT rc{0, 0, px(440), px(600)};
    AdjustWindowRectExForDpi(&rc, style, FALSE, WS_EX_DLGMODALFRAME, dpi_);
    const int w = rc.right - rc.left, h = rc.bottom - rc.top;
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    CreateWindowExW(WS_EX_DLGMODALFRAME, kClassName, L"Boringshot 设置", style, work.left + (work.right - work.left - w) / 2,
                    work.top + (work.bottom - work.top - h) / 2, w, h, nullptr, nullptr, instance, this);
    if (!hwnd_) return false;
    populate(current);
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    return true;
}

void SettingsDialog::close() {
    if (hwnd_) DestroyWindow(hwnd_);
}

LRESULT CALLBACK SettingsDialog::wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* self = static_cast<SettingsDialog*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<SettingsDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
    if (msg == WM_NCDESTROY) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        self->hwnd_ = nullptr;
        self->tips_ = nullptr;
        if (self->font_) DeleteObject(self->font_);
        self->font_ = nullptr;
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    return self->handle(msg, wp, lp);
}

LRESULT SettingsDialog::handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            build();
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDOK:
                    if (apply()) close();
                    return 0;
                case kIdApply:
                    apply();
                    return 0;
                case IDCANCEL:
                    close();
                    return 0;
                default:
                    break;
            }
            break;
        case WM_CLOSE:
            close();
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

HWND SettingsDialog::label(const wchar_t* text, int x, int y, int w) {
    return control(L"STATIC", SS_NOTIFY | SS_LEFT, 0, x, y + px(4), w, px(20), text);
}

HWND SettingsDialog::control(const wchar_t* cls, DWORD style, int id, int x, int y, int w, int h, const wchar_t* text) {
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, hwnd_,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    return c;
}

void SettingsDialog::tip(HWND ctrl, const wchar_t* text) {
    if (!tips_ || !ctrl) return;
    TOOLINFOW ti{};
    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = hwnd_;
    ti.uId = reinterpret_cast<UINT_PTR>(ctrl);
    ti.lpszText = const_cast<wchar_t*>(text);
    SendMessageW(tips_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
    // Editable combos: the pointer is over their inner edit, so register it too.
    if (HWND inner = GetWindow(ctrl, GW_CHILD)) {
        ti.uId = reinterpret_cast<UINT_PTR>(inner);
        SendMessageW(tips_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));
    }
}

void SettingsDialog::build() {
    font_ = CreateFontW(-MulDiv(9, static_cast<int>(dpi_), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    tips_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX, 0, 0, 0, 0,
                            hwnd_, nullptr, nullptr, nullptr);
    if (tips_) SendMessageW(tips_, TTM_SETMAXTIPWIDTH, 0, px(360));

    const int L = px(24), labelW = px(110), X = px(140), W = px(270), row = px(32), comboH = px(240);
    int y = px(12);
    const auto group = [&](const wchar_t* title, int rows) {
        control(L"BUTTON", BS_GROUPBOX, 0, px(10), y, px(420), px(28) + rows * row, title);
        y += px(24);
    };

    group(L"快捷键", 3);
    const wchar_t* hotkeyTip = L"点一下输入框，直接按下想要的组合键（如 Ctrl+Alt+A 或 F1）。\nBackspace 清空 = 禁用该热键。";
    tip(label(L"截图", L, y, labelW), hotkeyTip);
    capture_ = control(HOTKEY_CLASSW, WS_BORDER | WS_TABSTOP, kIdCapture, X, y, W, px(24));
    tip(capture_, hotkeyTip);
    y += row;
    tip(label(L"截图（备用）", L, y, labelW), L"第二个截图热键，主热键被别的软件占用时很有用。");
    captureAlt_ = control(HOTKEY_CLASSW, WS_BORDER | WS_TABSTOP, kIdCaptureAlt, X, y, W, px(24));
    tip(captureAlt_, hotkeyTip);
    y += row;
    tip(label(L"水印开关", L, y, labelW), L"显示 / 隐藏常驻水印。");
    watermarkKey_ = control(HOTKEY_CLASSW, WS_BORDER | WS_TABSTOP, kIdWatermark, X, y, W, px(24));
    tip(watermarkKey_, hotkeyTip);
    y += row + px(12);

    group(L"截图", 2);
    tip(label(L"截屏方式", L, y, labelW), L"截取屏幕画面的底层接口。");
    backend_ = control(WC_COMBOBOXW, CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, kIdBackend, X, y, W, comboH);
    for (const wchar_t* b : kBackends) addItem(backend_, b);
    tip(backend_, L"自动：Win11 用 WGC（可关闭黄色捕获边框），其余系统用 GDI。\nWGC 失败时自动回退到 GDI。");
    y += row;
    animation_ = control(L"BUTTON", BS_AUTOCHECKBOX | WS_TABSTOP, kIdAnimation, L, y, px(380), px(24),
                         L"呼出动画（水波纹 + 轻微抖动）");
    tip(animation_, L"截图呼出时的 0.4 秒动画。画面在动画开始前就已冻结，不会被拍进截图。");
    y += row + px(12);

    group(L"水印", 8);
    tip(label(L"文字", L, y, labelW), L"可用变量：{user} 用户名 · {date} 日期 · {time} 时间");
    text_ = control(L"EDIT", WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, kIdText, X, y, W, px(24));
    tip(text_, L"可用变量：{user} 用户名 · {date} 日期 · {time} 时间\n例：内部评审 {user} {date}");
    y += row;
    tip(label(L"字体", L, y, labelW), L"可从列表选，也可直接输入已安装的字体名。");
    font_family_ = control(WC_COMBOBOXW, CBS_DROPDOWN | WS_VSCROLL | WS_TABSTOP, kIdFont, X, y, W, comboH);
    for (const wchar_t* f : kFonts) addItem(font_family_, f);
    tip(font_family_, L"可从列表选，也可直接输入已安装的字体名。");
    y += row;
    tip(label(L"字号", L, y, labelW), L"水印文字大小（会按屏幕缩放自动放大）。");
    size_ = control(WC_COMBOBOXW, CBS_DROPDOWN | WS_VSCROLL | WS_TABSTOP, kIdSize, X, y, W, comboH);
    for (float s : kSizes) addItem(size_, fmt(s, L""));
    tip(size_, L"可选或直接输入数字。");
    y += row;
    tip(label(L"颜色", L, y, labelW), L"水印颜色。");
    color_ = control(WC_COMBOBOXW, CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, kIdColor, X, y, W, comboH);
    tip(color_, L"浅色背景用灰/蓝，深色背景用白色更清楚。");
    y += row;
    tip(label(L"不透明度", L, y, labelW), L"越低越不打扰，15%–25% 比较合适。");
    opacity_ = control(WC_COMBOBOXW, CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, kIdOpacity, X, y, W, comboH);
    tip(opacity_, L"越低越不打扰，15%–25% 比较合适。");
    y += row;
    tip(label(L"倾斜角度", L, y, labelW), L"水印文字旋转角度。");
    angle_ = control(WC_COMBOBOXW, CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, kIdAngle, X, y, W, comboH);
    tip(angle_, L"负数为向右上倾斜。");
    y += row;
    tip(label(L"平铺间距", L, y, labelW), L"水印之间的距离。");
    spacing_ = control(WC_COMBOBOXW, CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, kIdSpacing, X, y, W, comboH);
    tip(spacing_, L"紧凑 240×140 · 标准 320×180 · 宽松 420×240（像素）");
    y += row;
    allMonitors_ = control(L"BUTTON", BS_AUTOCHECKBOX | WS_TABSTOP, kIdAllMonitors, L, y, px(380), px(24),
                           L"覆盖所有显示器");
    tip(allMonitors_, L"关闭后只在主显示器显示水印。\n注意：水印让截图带上标记，但无法阻止拍照等物理方式。");
    y += row + px(14);

    const int bw = px(84), bh = px(28);
    tip(control(L"BUTTON", BS_DEFPUSHBUTTON | WS_TABSTOP, IDOK, px(440) - 3 * (bw + px(8)), y, bw, bh, L"确定"),
        L"保存并关闭（立即生效）");
    tip(control(L"BUTTON", BS_PUSHBUTTON | WS_TABSTOP, IDCANCEL, px(440) - 2 * (bw + px(8)), y, bw, bh, L"取消"),
        L"放弃未应用的修改");
    tip(control(L"BUTTON", BS_PUSHBUTTON | WS_TABSTOP, kIdApply, px(440) - (bw + px(8)), y, bw, bh, L"应用"),
        L"保存并立即生效，窗口保持打开");
}

void SettingsDialog::populate(const bs::Config& c) {
    base_ = c;
    const auto& w = c.watermarkProfile;
    SendMessageW(capture_, HKM_SETHOTKEY, bs::hotkeyToControl(c.capture), 0);
    SendMessageW(captureAlt_, HKM_SETHOTKEY, bs::hotkeyToControl(c.captureAlt), 0);
    SendMessageW(watermarkKey_, HKM_SETHOTKEY, bs::hotkeyToControl(c.watermark), 0);
    SendMessageW(backend_, CB_SETCURSEL, static_cast<WPARAM>(c.backend), 0);
    SendMessageW(animation_, BM_SETCHECK, c.animation ? BST_CHECKED : BST_UNCHECKED, 0);
    SetWindowTextW(text_, w.text.c_str());
    SetWindowTextW(font_family_, w.fontFamily.c_str());
    SetWindowTextW(size_, fmt(w.fontSize, L"").c_str());

    SendMessageW(color_, CB_RESETCONTENT, 0, 0);
    for (const auto& p : bs::kWatermarkColors) addItem(color_, p.name);
    const int ci = bs::colorPresetIndex(w.colorR, w.colorG, w.colorB);
    if (ci < 0) addItem(color_, L"自定义（保持当前）");
    SendMessageW(color_, CB_SETCURSEL, static_cast<WPARAM>(ci < 0 ? std::size(bs::kWatermarkColors) : static_cast<std::size_t>(ci)), 0);

    opacities_ = withCurrent(kOpacities, std::size(kOpacities), w.opacity);
    SendMessageW(opacity_, CB_RESETCONTENT, 0, 0);
    for (float o : opacities_) addItem(opacity_, fmt(std::round(o * 1000.0f) / 10.0f, L"%"));
    SendMessageW(opacity_, CB_SETCURSEL, static_cast<WPARAM>(indexOf(opacities_, w.opacity)), 0);

    angles_ = withCurrent(kAngles, std::size(kAngles), w.rotationDeg);
    SendMessageW(angle_, CB_RESETCONTENT, 0, 0);
    for (float a : angles_) addItem(angle_, fmt(a, L"°"));
    SendMessageW(angle_, CB_SETCURSEL, static_cast<WPARAM>(indexOf(angles_, w.rotationDeg)), 0);

    SendMessageW(spacing_, CB_RESETCONTENT, 0, 0);
    for (const auto& p : bs::kWatermarkSpacings) addItem(spacing_, p.name);
    const int si = bs::spacingPresetIndex(w.tileSpacingX, w.tileSpacingY);
    if (si < 0) addItem(spacing_, L"自定义（保持当前）");
    SendMessageW(spacing_, CB_SETCURSEL,
                 static_cast<WPARAM>(si < 0 ? std::size(bs::kWatermarkSpacings) : static_cast<std::size_t>(si)), 0);
    SendMessageW(allMonitors_, BM_SETCHECK, w.enableOnAllMonitors ? BST_CHECKED : BST_UNCHECKED, 0);
}

bs::Config SettingsDialog::collect() const {
    bs::Config c = base_;
    c.warnings.clear();
    // The hotkey control can't show Win+…; keep the original if the user didn't touch it.
    const auto key = [](HWND h, const bs::Hotkey& old) {
        const auto v = static_cast<unsigned>(SendMessageW(h, HKM_GETHOTKEY, 0, 0)) & 0xFFFFu;
        return v == bs::hotkeyToControl(old) ? old : bs::hotkeyFromControl(v);
    };
    c.capture = key(capture_, base_.capture);
    c.captureAlt = key(captureAlt_, base_.captureAlt);
    c.watermark = key(watermarkKey_, base_.watermark);
    c.backend = static_cast<bs::CaptureBackend>(std::clamp(comboIndex(backend_), 0, 2));
    c.animation = SendMessageW(animation_, BM_GETCHECK, 0, 0) == BST_CHECKED;

    auto& w = c.watermarkProfile;
    w.text = windowText(text_);
    if (const std::wstring f = windowText(font_family_); !f.empty()) w.fontFamily = f;
    const float size = std::wcstof(windowText(size_).c_str(), nullptr);
    if (size >= 4.0f && size <= 400.0f) w.fontSize = size;
    if (const int ci = comboIndex(color_); ci >= 0 && static_cast<std::size_t>(ci) < std::size(bs::kWatermarkColors)) {
        w.colorR = bs::kWatermarkColors[ci].r;
        w.colorG = bs::kWatermarkColors[ci].g;
        w.colorB = bs::kWatermarkColors[ci].b;
    }
    if (const int oi = comboIndex(opacity_); oi >= 0 && static_cast<std::size_t>(oi) < opacities_.size())
        w.opacity = opacities_[static_cast<std::size_t>(oi)];
    if (const int ai = comboIndex(angle_); ai >= 0 && static_cast<std::size_t>(ai) < angles_.size())
        w.rotationDeg = angles_[static_cast<std::size_t>(ai)];
    if (const int si = comboIndex(spacing_); si >= 0 && static_cast<std::size_t>(si) < std::size(bs::kWatermarkSpacings)) {
        w.tileSpacingX = bs::kWatermarkSpacings[si].x;
        w.tileSpacingY = bs::kWatermarkSpacings[si].y;
    }
    w.enableOnAllMonitors = SendMessageW(allMonitors_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    return c;
}

bool SettingsDialog::apply() {
    const bs::Config c = collect();
    if (const auto problems = bs::validateConfig(c); !problems.empty()) {
        std::wstring msg;
        for (const auto& p : problems) msg += L"• " + p + L"\n";
        if (MessageBoxW(hwnd_, (msg + L"\n仍然保存吗？").c_str(), L"Boringshot 设置", MB_YESNO | MB_ICONWARNING) != IDYES)
            return false;
    }
    if (onApply_ && !onApply_(c)) return false;
    base_ = c;
    return true;
}
