#pragma once

#include <blend2d/blend2d.h>

#include <memory>
#include <string>

namespace nanote {

// Owns a Blend2D font face loaded from a file. Shared across fonts.
class Typeface {
 public:
  Typeface() = default;

  bool valid() const {
    return !face_.is_empty();
  }
  const BLFontFace& face() const {
    return face_;
  }

  static std::shared_ptr<Typeface> LoadFromFile(const std::string& path);

 private:
  BLFontFace face_;
};

class Font {
 public:
  Font() = default;
  Font(std::shared_ptr<Typeface> tf, float size);

  bool valid() const {
    return bl_font_.is_valid();
  }
  const BLFont& blFont() const {
    return bl_font_;
  }
  float size() const {
    return size_;
  }

  float measureText(const char* text, size_t len) const;
  // Measure text bounds offset at origin (tight box, origin at baseline start).
  void measureBounds(const char* text, size_t len, float* w, float* h) const;

 private:
  std::shared_ptr<Typeface> typeface_;
  BLFont bl_font_;
  float size_ = 12.0f;
};

std::shared_ptr<Typeface> LoadUiTypeface();

}  // namespace nanote
