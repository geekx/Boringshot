#pragma once

#include <windows.h>

#include <string>

#include "boringshot/config.h"

// %APPDATA%\Boringshot\config.ini (UTF-8). Parsing lives in core (bs::parseConfig).
std::wstring defaultConfigPath();

// Reads and parses `path`; writes the default file first when missing.
bs::Config loadConfig(const std::wstring& path, bool createIfMissing);

// Last-write stamp for cheap change polling (0 if the file is missing).
ULONGLONG configStamp(const std::wstring& path);

std::wstring utf8ToWide(const std::string& s);
std::string wideToUtf8(const std::wstring& s);
