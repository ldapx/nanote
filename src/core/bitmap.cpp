#include "core/bitmap.h"

#include <algorithm>

namespace nanote {

static uint32_t Premultiply(Color c) {
  uint8_t a = GetA(c), r = GetR(c), g = GetG(c), b = GetB(c);
  if (a == 0) return 0;
  if (a == 255)
    return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(r) << 16) |
           (static_cast<uint32_t>(g) << 8) | b;
  // PRGB32 stores premultiplied in R,G,B with A in high byte.
  r = static_cast<uint8_t>((r * a + 127) / 255);
  g = static_cast<uint8_t>((g * a + 127) / 255);
  b = static_cast<uint8_t>((b * a + 127) / 255);
  return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(r) << 16) |
         (static_cast<uint32_t>(g) << 8) | b;
}

static Color Unpremultiply(uint32_t p) {
  uint8_t a = static_cast<uint8_t>((p >> 24) & 0xFF);
  uint8_t r = static_cast<uint8_t>((p >> 16) & 0xFF);
  uint8_t g = static_cast<uint8_t>((p >> 8) & 0xFF);
  uint8_t b = static_cast<uint8_t>(p & 0xFF);
  if (a == 0 || a == 255) return p;
  r = static_cast<uint8_t>(std::min(255, (r * 255 + a / 2) / a));
  g = static_cast<uint8_t>(std::min(255, (g * 255 + a / 2) / a));
  b = static_cast<uint8_t>(std::min(255, (b * 255 + a / 2) / a));
  return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(r) << 16) |
         (static_cast<uint32_t>(g) << 8) | b;
}

void Bitmap::eraseColor(Color c) {
  if (image_.is_empty()) return;
  BLImageData data;
  if (image_.make_mutable(&data) != BL_SUCCESS) return;
  uint32_t p = Premultiply(c);
  uint32_t* row = static_cast<uint32_t*>(data.pixel_data);
  const int w = image_.width();
  const int h = image_.height();
  const int stride = static_cast<int>(data.stride / 4);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) row[y * stride + x] = p;
  }
}

Color Bitmap::getColor(int x, int y) const {
  if (image_.is_empty()) return kColorTransparent;
  if (x < 0 || y < 0 || x >= image_.width() || y >= image_.height())
    return kColorTransparent;
  BLImageData data;
  if (image_.get_data(&data) != BL_SUCCESS) return kColorTransparent;
  const uint32_t* row = static_cast<const uint32_t*>(data.pixel_data);
  const int stride = static_cast<int>(data.stride / 4);
  return Unpremultiply(row[y * stride + x]);
}

void Bitmap::setColor(int x, int y, Color c) {
  if (image_.is_empty()) return;
  if (x < 0 || y < 0 || x >= image_.width() || y >= image_.height()) return;
  BLImageData data;
  if (image_.make_mutable(&data) != BL_SUCCESS) return;
  uint32_t* row = static_cast<uint32_t*>(data.pixel_data);
  const int stride = static_cast<int>(data.stride / 4);
  row[y * stride + x] = Premultiply(c);
}

void Bitmap::eraseRect(Color c, int x, int y, int w, int h) {
  if (image_.is_empty() || w <= 0 || h <= 0) return;
  const int iw = image_.width();
  const int ih = image_.height();
  const int x0 = std::max(0, x);
  const int y0 = std::max(0, y);
  const int x1 = std::min(iw, x + w);
  const int y1 = std::min(ih, y + h);
  if (x0 >= x1 || y0 >= y1) return;
  BLImageData data;
  if (image_.make_mutable(&data) != BL_SUCCESS) return;
  uint32_t p = Premultiply(c);
  uint32_t* base = static_cast<uint32_t*>(data.pixel_data);
  const int stride = static_cast<int>(data.stride / 4);
  for (int yy = y0; yy < y1; ++yy)
    for (int xx = x0; xx < x1; ++xx) base[yy * stride + xx] = p;
}

}  // namespace nanote
