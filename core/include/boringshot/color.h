#pragma once

#include <cstdint>
#include <string>

namespace bs {

struct Rgb8 {
    std::uint8_t r = 0, g = 0, b = 0;
    constexpr bool operator==(const Rgb8&) const = default;
};

// Pixel as stored in a 32-bit BGRA DIB (0xAARRGGBB little-endian word).
constexpr Rgb8 fromBgra(std::uint32_t px) {
    return {static_cast<std::uint8_t>((px >> 16) & 0xFF), static_cast<std::uint8_t>((px >> 8) & 0xFF),
            static_cast<std::uint8_t>(px & 0xFF)};
}

std::wstring hexString(Rgb8 c);  // "#1A2B3C"
std::wstring rgbString(Rgb8 c);  // "26, 43, 60"

}  // namespace bs
