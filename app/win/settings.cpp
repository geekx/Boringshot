#include "settings.h"

#include <vector>

std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

std::string wideToUtf8(const std::wstring& s) {
    if (s.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}

bs::Config loadSettings(const wchar_t* subKey) {
    DWORD bytes = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, subKey, L"Config", RRF_RT_REG_SZ, nullptr, nullptr, &bytes) != ERROR_SUCCESS ||
        bytes < sizeof(wchar_t))
        return bs::parseConfig(bs::defaultConfigText());
    std::vector<wchar_t> buf(bytes / sizeof(wchar_t) + 1, L'\0');
    if (RegGetValueW(HKEY_CURRENT_USER, subKey, L"Config", RRF_RT_REG_SZ, nullptr, buf.data(), &bytes) != ERROR_SUCCESS)
        return bs::parseConfig(bs::defaultConfigText());
    return bs::parseConfig(buf.data());
}

bool saveSettings(const bs::Config& c, const wchar_t* subKey) {
    const std::wstring text = bs::serializeConfig(c);
    return RegSetKeyValueW(HKEY_CURRENT_USER, subKey, L"Config", REG_SZ, text.c_str(),
                           static_cast<DWORD>((text.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

void deleteSettings(const wchar_t* subKey) { RegDeleteTreeW(HKEY_CURRENT_USER, subKey); }
