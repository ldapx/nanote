#include "core/font.h"

namespace nanote {

std::shared_ptr<Typeface> Typeface::LoadFromFile(const std::string& path) {
  auto tf = std::shared_ptr<Typeface>(new Typeface());
  if (tf->face_.create_from_file(path.c_str()) != BL_SUCCESS) {
    return nullptr;
  }
  return tf;
}

Font::Font(std::shared_ptr<Typeface> tf, float size)
    : typeface_(std::move(tf)), size_(size) {
  if (typeface_ && typeface_->valid()) {
    bl_font_.create_from_face(typeface_->face(), size_);
  }
}

float Font::measureText(const char* text, size_t len) const {
  if (!valid() || text == nullptr || len == 0) return 0.0f;
  BLGlyphBuffer gb;
  gb.set_text(text, len, BL_TEXT_ENCODING_UTF8);
  // const_cast because shape/get_text_metrics are non-const in this version.
  BLFont& font = const_cast<BLFont&>(bl_font_);
  if (font.shape(gb) != BL_SUCCESS) return 0.0f;
  BLTextMetrics metrics;
  if (font.get_text_metrics(gb, metrics) != BL_SUCCESS) return 0.0f;
  return static_cast<float>(metrics.advance.x);
}

void Font::measureBounds(const char* text, size_t len, float* w,
                         float* h) const {
  if (!valid() || text == nullptr || len == 0) {
    if (w) *w = 0;
    if (h) *h = 0;
    return;
  }
  BLGlyphBuffer gb;
  gb.set_text(text, len, BL_TEXT_ENCODING_UTF8);
  BLFont& font = const_cast<BLFont&>(bl_font_);
  if (font.shape(gb) != BL_SUCCESS) {
    if (w) *w = 0;
    if (h) *h = 0;
    return;
  }
  BLTextMetrics metrics;
  if (font.get_text_metrics(gb, metrics) != BL_SUCCESS) {
    if (w) *w = 0;
    if (h) *h = 0;
    return;
  }
  if (w)
    *w = static_cast<float>(metrics.bounding_box.x1 - metrics.bounding_box.x0);
  if (h)
    *h = static_cast<float>(metrics.bounding_box.y1 - metrics.bounding_box.y0);
}

std::shared_ptr<Typeface> LoadUiTypeface() {
  // System font locations differ per OS; the platform's usual spots come
  // first, then the Linux distro paths as a fallback (harmless elsewhere:
  // missing files simply fail to load).
  static const char* kPaths[] = {
#if defined(_WIN32)
      "C:/Windows/Fonts/segoeui.ttf",
      "C:/Windows/Fonts/arial.ttf",
#elif defined(__APPLE__)
      "/System/Library/Fonts/Supplemental/Arial.ttf",
      "/Library/Fonts/Arial.ttf",
      "/System/Library/Fonts/Helvetica.ttc",
#endif
      "/usr/share/fonts/TTF/FiraCode-Regular.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
      "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
      "/usr/share/fonts/TTF/DejaVuSans.ttf",
      "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
  };
  for (const char* p : kPaths) {
    if (auto tf = Typeface::LoadFromFile(p)) return tf;
  }
  return nullptr;
}

}  // namespace nanote
