#include "color/color_manager.h"

#include "core/string_utils.h"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace nanote {

namespace {

int HexValue(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

bool ParseHexByte(char hi, char lo, uint8_t* out) {
  const int hi_value = HexValue(hi);
  const int lo_value = HexValue(lo);
  if (hi_value < 0 || lo_value < 0) {
    return false;
  }
  *out = static_cast<uint8_t>((hi_value << 4) | lo_value);
  return true;
}

bool ParseHexNibble(char c, uint8_t* out) {
  const int value = HexValue(c);
  if (value < 0) {
    return false;
  }
  *out = static_cast<uint8_t>((value << 4) | value);
  return true;
}

std::vector<std::string> SplitArgs(std::string_view s) {
  std::vector<std::string> parts;
  std::string current;
  for (char c : s) {
    if (c == ',') {
      const std::string trimmed = Trim(current);
      if (!trimmed.empty()) {
        parts.push_back(trimmed);
      }
      current.clear();
      continue;
    }
    current.push_back(c);
  }

  const std::string trimmed = Trim(current);
  if (!trimmed.empty()) {
    parts.push_back(trimmed);
  }
  return parts;
}

bool ParseRgbComponent(std::string token, uint8_t* out) {
  if (token.empty()) {
    return false;
  }

  const bool is_percent = token.back() == '%';
  if (is_percent) {
    token.pop_back();
  }

  try {
    const float value = std::stof(token);
    if (is_percent) {
      *out = static_cast<uint8_t>(std::clamp(value, 0.0f, 100.0f) * 255.0f /
                                  100.0f);
    } else {
      *out = static_cast<uint8_t>(std::clamp(value, 0.0f, 255.0f));
    }
    return true;
  } catch (...) {
    return false;
  }
}

bool ParseAlpha(std::string token, uint8_t* out) {
  if (token.empty()) {
    return false;
  }

  const bool is_percent = token.back() == '%';
  if (is_percent) {
    token.pop_back();
  }

  try {
    const float value = std::stof(token);
    if (is_percent) {
      *out = static_cast<uint8_t>(std::clamp(value, 0.0f, 100.0f) * 255.0f /
                                  100.0f);
    } else if (value <= 1.0f) {
      *out = static_cast<uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f);
    } else {
      *out = static_cast<uint8_t>(std::clamp(value, 0.0f, 255.0f));
    }
    return true;
  } catch (...) {
    return false;
  }
}

bool ParsePercentFloat(std::string token, float* out) {
  if (token.empty() || token.back() != '%') {
    return false;
  }
  token.pop_back();
  try {
    *out = std::clamp(std::stof(token), 0.0f, 100.0f) / 100.0f;
    return true;
  } catch (...) {
    return false;
  }
}

std::optional<float> ParseFloat(const std::string& token) {
  try {
    return std::stof(token);
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<std::vector<std::string>> ParseFunctionArgs(
    const std::string& s) {
  const size_t open = s.find('(');
  const size_t close = s.rfind(')');
  if (open == std::string::npos || close == std::string::npos ||
      close <= open) {
    return std::nullopt;
  }

  const std::vector<std::string> parts =
      SplitArgs(std::string_view(s).substr(open + 1, close - open - 1));
  if (parts.empty()) {
    return std::nullopt;
  }
  return parts;
}

std::optional<Color> ParseHex(const std::string& s) {
  if (s.empty() || s[0] != '#') {
    return std::nullopt;
  }

  const std::string h = s.substr(1);
  if (h.size() == 3) {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    if (!ParseHexNibble(h[0], &r) || !ParseHexNibble(h[1], &g) ||
        !ParseHexNibble(h[2], &b)) {
      return std::nullopt;
    }
    return MakeARGB(255, r, g, b);
  }

  if (h.size() == 4) {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 0;
    if (!ParseHexNibble(h[0], &r) || !ParseHexNibble(h[1], &g) ||
        !ParseHexNibble(h[2], &b) || !ParseHexNibble(h[3], &a)) {
      return std::nullopt;
    }
    return MakeARGB(a, r, g, b);
  }

  if (h.size() == 6) {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    if (!ParseHexByte(h[0], h[1], &r) || !ParseHexByte(h[2], h[3], &g) ||
        !ParseHexByte(h[4], h[5], &b)) {
      return std::nullopt;
    }
    return MakeARGB(255, r, g, b);
  }

  if (h.size() == 8) {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 0;
    if (!ParseHexByte(h[0], h[1], &r) || !ParseHexByte(h[2], h[3], &g) ||
        !ParseHexByte(h[4], h[5], &b) || !ParseHexByte(h[6], h[7], &a)) {
      return std::nullopt;
    }
    return MakeARGB(a, r, g, b);
  }

  return std::nullopt;
}

std::optional<Color> ParseNamed(const std::string& s) {
  static const std::pair<const char*, Color> kNamed[] = {
      {"black", kColorBlack},
      {"white", kColorWhite},
      {"red", kColorRed},
      {"green", kColorGreen},
      {"blue", kColorBlue},
      {"yellow", kColorYellow},
      {"cyan", kColorCyan},
      {"magenta", kColorMagenta},
      {"orange", MakeARGB(255, 255, 165, 0)},
      {"purple", MakeARGB(255, 128, 0, 128)},
      {"steelblue", MakeARGB(255, 70, 130, 180)},
      {"transparent", kColorTransparent},
  };

  for (const auto& [name, color] : kNamed) {
    if (s == name) {
      return color;
    }
  }
  return std::nullopt;
}

std::optional<Color> ParseRgb(const std::string& s) {
  const auto args = ParseFunctionArgs(s);
  if (!args || args->size() < 3 || args->size() > 4) {
    return std::nullopt;
  }

  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  uint8_t a = 255;

  if (!ParseRgbComponent((*args)[0], &r) ||
      !ParseRgbComponent((*args)[1], &g) ||
      !ParseRgbComponent((*args)[2], &b)) {
    return std::nullopt;
  }

  if (args->size() == 4 && !ParseAlpha((*args)[3], &a)) {
    return std::nullopt;
  }

  return MakeARGB(a, r, g, b);
}

std::optional<Color> ParseHsl(const std::string& s) {
  const auto args = ParseFunctionArgs(s);
  if (!args || args->size() < 3 || args->size() > 4) {
    return std::nullopt;
  }

  const auto hue = ParseFloat((*args)[0]);
  if (!hue.has_value()) {
    return std::nullopt;
  }

  float sat = 0.0f;
  float lit = 0.0f;
  if (!ParsePercentFloat((*args)[1], &sat) ||
      !ParsePercentFloat((*args)[2], &lit)) {
    return std::nullopt;
  }

  float alpha = 1.0f;
  if (args->size() == 4) {
    uint8_t alpha_byte = 255;
    if (!ParseAlpha((*args)[3], &alpha_byte)) {
      return std::nullopt;
    }
    alpha = static_cast<float>(alpha_byte) / 255.0f;
  }

  float h = std::fmod(*hue, 360.0f);
  if (h < 0.0f) {
    h += 360.0f;
  }

  float r = lit;
  float g = lit;
  float b = lit;
  if (sat > 0.0f) {
    const auto hue2rgb = [](float p, float q, float t) -> float {
      if (t < 0.0f) {
        t += 1.0f;
      }
      if (t > 1.0f) {
        t -= 1.0f;
      }
      if (t < 1.0f / 6.0f) {
        return p + (q - p) * 6.0f * t;
      }
      if (t < 1.0f / 2.0f) {
        return q;
      }
      if (t < 2.0f / 3.0f) {
        return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
      }
      return p;
    };

    const float q = lit < 0.5f ? lit * (1.0f + sat) : lit + sat - lit * sat;
    const float p = 2.0f * lit - q;
    r = hue2rgb(p, q, h / 360.0f + 1.0f / 3.0f);
    g = hue2rgb(p, q, h / 360.0f);
    b = hue2rgb(p, q, h / 360.0f - 1.0f / 3.0f);
  }

  return MakeARGB(static_cast<uint8_t>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f),
                  static_cast<uint8_t>(std::clamp(r, 0.0f, 1.0f) * 255.0f),
                  static_cast<uint8_t>(std::clamp(g, 0.0f, 1.0f) * 255.0f),
                  static_cast<uint8_t>(std::clamp(b, 0.0f, 1.0f) * 255.0f));
}

int ClampRows(int row_count) {
  return std::clamp(row_count, Palette::kMinRows, Palette::kMaxRows);
}

int ClampTones(int tone_count) {
  return std::clamp(tone_count, Palette::kMinTones, Palette::kMaxTones);
}

}  // namespace

std::optional<Color> TryParseColor(const std::string& s) {
  const std::string trimmed = Trim(s);
  if (trimmed.empty()) {
    return std::nullopt;
  }

  if (trimmed[0] == '#') {
    return ParseHex(trimmed);
  }

  const std::string lower = Lower(trimmed);
  if (lower.rfind("rgb(", 0) == 0 || lower.rfind("rgba(", 0) == 0) {
    return ParseRgb(lower);
  }
  if (lower.rfind("hsl(", 0) == 0 || lower.rfind("hsla(", 0) == 0) {
    return ParseHsl(lower);
  }
  return ParseNamed(lower);
}

Color ParseColor(const std::string& s) {
  if (const auto parsed = TryParseColor(s); parsed.has_value()) {
    return *parsed;
  }
  return kColorTransparent;
}

ColorManager::ColorManager() {
  Palette default_palette;
  default_palette.SetDefaultTone(5);
  // Start on the first color of the palette (row 0 at the default tone,
  // a gray in the Default palette) instead of black.
  default_palette.SetActiveCell(0, default_palette.GetDefaultTone());
  palettes_.push_back({"Default", std::move(default_palette)});
  SyncCurrentColorFromPalette();
}

void ColorManager::SetCurrentColor(Color color) {
  current_color_ = color;
  stroke_color_ = color;
}

const Palette& ColorManager::GetActivePalette() const {
  return palettes_[current_palette_index_].palette;
}

Palette& ColorManager::GetActivePalette() {
  return palettes_[current_palette_index_].palette;
}

int ColorManager::GetActivePaletteRow() const {
  return GetActivePalette().GetActiveRow();
}

int ColorManager::GetActivePaletteTone() const {
  return GetActivePalette().GetActiveTone();
}

int ColorManager::GetActivePaletteDefaultTone() const {
  return GetActivePalette().GetDefaultTone();
}

const std::string& ColorManager::GetCurrentPaletteName() const {
  return palettes_[current_palette_index_].name;
}

void ColorManager::SetActivePaletteCell(int row, int tone) {
  Palette& palette = GetActivePalette();
  if (row < 0 || row >= palette.GetRowCount() || tone < 0 ||
      tone >= palette.GetToneCount()) {
    return;
  }
  palette.SetActiveCell(row, tone);
  SyncCurrentColorFromPalette();
}

bool ColorManager::NewPalette(const std::string& name, int row_count,
                              int tone_count) {
  std::string palette_name = Trim(name);
  if (palette_name.empty()) {
    palette_name = "Palette " + std::to_string(palettes_.size() + 1);
  }
  if (HasPalette(palette_name)) {
    return false;
  }

  palettes_.push_back(
      {palette_name, Palette(ClampRows(row_count), ClampTones(tone_count))});
  return true;
}

bool ColorManager::LoadPalette(const std::string& name) {
  return LoadPalette(FindPaletteIndex(name));
}

bool ColorManager::LoadPalette(int index) {
  if (index < 0 || index >= static_cast<int>(palettes_.size())) {
    return false;
  }
  current_palette_index_ = index;
  SyncCurrentColorFromPalette();
  return true;
}

bool ColorManager::RemovePalette(const std::string& name) {
  return RemovePalette(FindPaletteIndex(name));
}

bool ColorManager::RemovePalette(int index) {
  if (index <= 0 || index >= static_cast<int>(palettes_.size())) {
    return false;
  }

  palettes_.erase(palettes_.begin() + index);
  if (current_palette_index_ >= static_cast<int>(palettes_.size())) {
    current_palette_index_ = 0;
  } else if (current_palette_index_ == index) {
    current_palette_index_ = 0;
  } else if (current_palette_index_ > index) {
    --current_palette_index_;
  }
  SyncCurrentColorFromPalette();
  return true;
}

bool ColorManager::SetPaletteColor(const std::string& name, int row, int tone,
                                   Color color) {
  return SetPaletteColor(FindPaletteIndex(name), row, tone, color);
}

bool ColorManager::SetPaletteColor(int palette_index, int row, int tone,
                                   Color color) {
  if (palette_index < 0 ||
      palette_index >= static_cast<int>(palettes_.size())) {
    return false;
  }

  Palette& palette = palettes_[palette_index].palette;
  if (row < 0 || row >= palette.GetRowCount() || tone < 0 ||
      tone >= palette.GetToneCount()) {
    return false;
  }

  palette.SetColor(row, tone, color);
  if (palette_index == current_palette_index_ &&
      row == palette.GetActiveRow() && tone == palette.GetActiveTone()) {
    SyncCurrentColorFromPalette();
  }
  return true;
}

bool ColorManager::HasPalette(const std::string& name) const {
  return FindPaletteIndex(name) >= 0;
}

void ColorManager::CycleColor() {
  Palette& palette = GetActivePalette();
  const int next_row = (palette.GetActiveRow() + 1) % palette.GetRowCount();
  palette.SetActiveCell(next_row, palette.GetActiveTone());
  SyncCurrentColorFromPalette();
}

void ColorManager::PrevColor() {
  Palette& palette = GetActivePalette();
  const int prev_row = (palette.GetActiveRow() - 1 + palette.GetRowCount()) %
                       palette.GetRowCount();
  palette.SetActiveCell(prev_row, palette.GetActiveTone());
  SyncCurrentColorFromPalette();
}

Color ColorManager::PickColor(float x, float y,
                              const LayerStack& layers) const {
  for (auto it = layers.GetLayers().rbegin(); it != layers.GetLayers().rend();
       ++it) {
    const auto& layer = *it;
    if (!layer->visible) {
      continue;
    }

    if (layer->type == LayerType::kVector) {
      const auto* vector = static_cast<const VectorLayer*>(layer.get());
      if (const VectorObject* object = vector->HitTest(x, y);
          object != nullptr) {
        if (GetA(object->fill_color) > 0) {
          return object->fill_color;
        }
        if (GetA(object->stroke_color) > 0) {
          return object->stroke_color;
        }
      }
      continue;
    }

    const auto* raster = static_cast<const RasterLayer*>(layer.get());
    const int bx = static_cast<int>(x) - raster->offset_x;
    const int by = static_cast<int>(y) - raster->offset_y;
    if (bx < 0 || bx >= raster->bitmap.width() || by < 0 ||
        by >= raster->bitmap.height()) {
      continue;
    }

    const Color color = raster->bitmap.getColor(bx, by);
    if (GetA(color) > 0) {
      return color;
    }
  }

  return kColorTransparent;
}

int ColorManager::FindPaletteIndex(const std::string& name) const {
  const std::string trimmed = Trim(name);
  for (int i = 0; i < static_cast<int>(palettes_.size()); ++i) {
    if (palettes_[i].name == trimmed) {
      return i;
    }
  }
  return -1;
}

void ColorManager::SyncCurrentColorFromPalette() {
  const Palette& palette = GetActivePalette();
  SetCurrentColor(
      palette.GetColor(palette.GetActiveRow(), palette.GetActiveTone()));
}

}  // namespace nanote
