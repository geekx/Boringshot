#pragma once

#include <windows.h>

#include <string>

#include "boringshot/config.h"

// Settings live in the registry (HKCU\Software\Boringshot, value "Config", the
// serializeConfig text) — no files, so disk-encryption / DLP agents can't
// mangle them. Parsing stays in core (bs::parseConfig / bs::serializeConfig).
inline constexpr wchar_t kSettingsKey[] = L"Software\\Boringshot";

bs::Config loadSettings(const wchar_t* subKey = kSettingsKey);
bool saveSettings(const bs::Config& c, const wchar_t* subKey = kSettingsKey);
void deleteSettings(const wchar_t* subKey);  // smoke-test cleanup

std::wstring utf8ToWide(const std::string& s);
std::string wideToUtf8(const std::wstring& s);
