#pragma once

#include <blend2d/blend2d.h>

#include "core/bitmap.h"
#include "core/color.h"
#include "core/font.h"
#include "core/geometry.h"
#include "core/path.h"

#include <string>
#include <vector>

namespace nanote {

struct Paint {
  Color color = kColorWhite;
  bool fill = true;
  float strokeWidth = 1.0f;
  bool clear = false;  // Porter-Duff clear (erase to transparent).
  float alpha = 1.0f;  // 0..1 multiplier applied to color alpha.

  static Paint Fill(Color c) {
    Paint p;
    p.color = c;
    p.fill = true;
    return p;
  }
  static Paint Stroke(Color c, float w = 1.0f) {
    Paint p;
    p.color = c;
    p.fill = false;
    p.strokeWidth = w;
    return p;
  }
};

inline BLRgba32 ToBLRgba(Color c, float alphaMul = 1.0f) {
  uint8_t a = GetA(c);
  if (alphaMul < 1.0f) a = static_cast<uint8_t>(a * alphaMul);
  return BLRgba32(GetR(c), GetG(c), GetB(c), a);
}

// 2D canvas backed by a Blend2D context.
class Canvas {
 public:
  // Main surface canvas.
  Canvas(BLImage& target, int w, int h);
  // Offscreen bitmap canvas.
  explicit Canvas(Bitmap& bitmap);

  ~Canvas();

  Canvas(const Canvas&) = delete;
  Canvas& operator=(const Canvas&) = delete;

  void clear(Color c);
  void save();
  void restore();

  void translate(float tx, float ty);
  void scale(float sx, float sy);
  void rotate(float degrees);
  void concat(const Matrix& m);

  Matrix getTotalMatrix() const {
    return transform_;
  }
  int targetWidth() const {
    return target_w_;
  }
  int targetHeight() const {
    return target_h_;
  }

  void drawRect(const Rect& r, const Paint& p);
  void drawPath(const Path& path, const Paint& p);
  void drawLine(float x0, float y0, float x1, float y1, const Paint& p);
  void drawCircle(float cx, float cy, float r, const Paint& p);
  void drawOval(const Rect& r, const Paint& p);
  void drawImage(const Bitmap& bmp, float x, float y);
  // Global-alpha variant used for layer opacity.
  void drawImage(const Bitmap& bmp, float x, float y, float opacity);
  void drawString(const char* text, float x, float y, const Font& font,
                  const Paint& p);

  // Flush (no-op for CPU, kept for API parity).
  void flush() {
    ctx_.flush(BL_CONTEXT_FLUSH_NO_FLAGS);
  }

 private:
  void applyPaint(const Paint& p, bool stroking);
  void restoreCompOp();

  BLContext ctx_;
  Matrix transform_;
  std::vector<Matrix> stack_;
  BLCompOp saved_comp_ = BL_COMP_OP_SRC_OVER;
  bool comp_saved_ = false;
  int target_w_ = 0;
  int target_h_ = 0;
};

}  // namespace nanote
