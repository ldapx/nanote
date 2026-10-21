#pragma once

#include <blend2d/blend2d.h>

#include "core/color.h"

namespace nanote {

// CPU raster bitmap backed by BLImage (PRGB32).
class Bitmap {
 public:
  Bitmap() = default;
  Bitmap(const Bitmap& o) {
    *this = o;
  }
  Bitmap& operator=(const Bitmap& o) {
    if (this != &o) {
      image_.assign_deep(o.image_);
    }
    return *this;
  }

  void allocN32Pixels(int w, int h) {
    if (w <= 0) w = 1;
    if (h <= 0) h = 1;
    image_.create(w, h, BL_FORMAT_PRGB32);
    eraseColor(kColorTransparent);
  }

  int width() const {
    return image_.width();
  }
  int height() const {
    return image_.height();
  }
  bool empty() const {
    return image_.is_empty();
  }

  void eraseColor(Color c);
  Color getColor(int x, int y) const;
  void setColor(int x, int y, Color c);
  void eraseRect(Color c, int x, int y, int w, int h);

  BLImage& image() {
    return image_;
  }
  const BLImage& image() const {
    return image_;
  }

 private:
  BLImage image_;
};

}  // namespace nanote
