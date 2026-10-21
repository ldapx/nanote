#pragma once

#include "color/palette.h"
#include "core/color.h"
#include "layer/layer.h"

#include <optional>
#include <string>
#include <vector>

namespace nanote {

// Supports: hex (#RGB[A], #RRGGBB[AA]), named colors, rgb[a](), hsl[a]().
[[nodiscard]] std::optional<Color> TryParseColor(const std::string& s);
[[nodiscard]] Color ParseColor(const std::string& s);

struct NamedPalette {
  std::string name;
  Palette palette;
};

class ColorManager {
 public:
  ColorManager();

  [[nodiscard]] Color GetCurrentColor() const {
    return current_color_;
  }
  [[nodiscard]] Color GetStrokeColor() const {
    return stroke_color_;
  }
  [[nodiscard]] Color GetFillColor() const {
    return fill_color_;
  }

  void SetCurrentColor(Color color);
  void SetStrokeColor(Color color) {
    stroke_color_ = color;
  }
  void SetFillColor(Color color) {
    fill_color_ = color;
  }

  [[nodiscard]] const Palette& GetActivePalette() const;
  [[nodiscard]] Palette& GetActivePalette();
  [[nodiscard]] int GetActivePaletteRow() const;
  [[nodiscard]] int GetActivePaletteTone() const;
  [[nodiscard]] int GetActivePaletteDefaultTone() const;
  [[nodiscard]] int GetCurrentPaletteIndex() const {
    return current_palette_index_;
  }
  [[nodiscard]] const std::string& GetCurrentPaletteName() const;
  void SetActivePaletteCell(int row, int tone);

  [[nodiscard]] const std::vector<NamedPalette>& GetPalettes() const {
    return palettes_;
  }

  [[nodiscard]] bool NewPalette(const std::string& name, int row_count,
                                int tone_count);
  [[nodiscard]] bool LoadPalette(const std::string& name);
  [[nodiscard]] bool LoadPalette(int index);
  [[nodiscard]] bool RemovePalette(const std::string& name);
  [[nodiscard]] bool RemovePalette(int index);
  [[nodiscard]] bool SetPaletteColor(const std::string& name, int row, int tone,
                                     Color color);
  [[nodiscard]] bool SetPaletteColor(int palette_index, int row, int tone,
                                     Color color);
  [[nodiscard]] bool HasPalette(const std::string& name) const;

  void CycleColor();
  void PrevColor();

  [[nodiscard]] Color PickColor(float x, float y,
                                const LayerStack& layers) const;

 private:
  [[nodiscard]] int FindPaletteIndex(const std::string& name) const;
  void SyncCurrentColorFromPalette();

  Color current_color_ = kColorBlack;
  Color stroke_color_ = kColorBlack;
  Color fill_color_ = kColorTransparent;

  std::vector<NamedPalette> palettes_;
  int current_palette_index_ = 0;
};

}  // namespace nanote
