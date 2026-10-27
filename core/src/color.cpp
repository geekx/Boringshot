#include "boringshot/color.h"

#include <cwchar>

namespace bs {

std::wstring hexString(Rgb8 c) {
    wchar_t buf[16];
    std::swprintf(buf, 16, L"#%02X%02X%02X", c.r, c.g, c.b);
    return buf;
}

std::wstring rgbString(Rgb8 c) {
    wchar_t buf[32];
    std::swprintf(buf, 32, L"%u, %u, %u", unsigned{c.r}, unsigned{c.g}, unsigned{c.b});
    return buf;
}

}  // namespace bs
