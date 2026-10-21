#pragma once

#include <cstdint>

namespace nanote {

// Packed ARGB, 0xAARRGGBB.
using Color = uint32_t;

constexpr Color kColorTransparent = 0x00000000u;
constexpr Color kColorBlack = 0xFF000000u;
constexpr Color kColorWhite = 0xFFFFFFFFu;
constexpr Color kColorRed = 0xFFFF0000u;
constexpr Color kColorGreen = 0xFF00FF00u;
constexpr Color kColorBlue = 0xFF0000FFu;
constexpr Color kColorYellow = 0xFFFFFF00u;
constexpr Color kColorCyan = 0xFF00FFFFu;
constexpr Color kColorMagenta = 0xFFFF00FFu;

inline constexpr Color MakeARGB(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
  return (static_cast<Color>(a) << 24) | (static_cast<Color>(r) << 16) |
         (static_cast<Color>(g) << 8) | static_cast<Color>(b);
}

inline constexpr uint8_t GetA(Color c) {
  return static_cast<uint8_t>((c >> 24) & 0xFF);
}
inline constexpr uint8_t GetR(Color c) {
  return static_cast<uint8_t>((c >> 16) & 0xFF);
}
inline constexpr uint8_t GetG(Color c) {
  return static_cast<uint8_t>((c >> 8) & 0xFF);
}
inline constexpr uint8_t GetB(Color c) {
  return static_cast<uint8_t>(c & 0xFF);
}

inline constexpr float GetAf(Color c) {
  return GetA(c) / 255.0f;
}

}  // namespace nanote
