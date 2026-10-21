#include "render/canvas.h"

#include <algorithm>
#include <cmath>

namespace nanote {

static BLMatrix2D ToBL(const Matrix& m) {
  return BLMatrix2D(m.m00, m.m01, m.m10, m.m11, m.m20, m.m21);
}

Canvas::Canvas(BLImage& target, int w, int h) : target_w_(w), target_h_(h) {
  ctx_.begin(target);
  transform_.setIdentity();
}

Canvas::Canvas(Bitmap& bitmap)
    : target_w_(bitmap.width()), target_h_(bitmap.height()) {
  ctx_.begin(bitmap.image());
  transform_.setIdentity();
}

Canvas::~Canvas() {
  ctx_.end();
}

void Canvas::clear(Color c) {
  ctx_.set_comp_op(BL_COMP_OP_SRC_COPY);
  ctx_.fill_all(ToBLRgba(c));
  ctx_.set_comp_op(BL_COMP_OP_SRC_OVER);
}

void Canvas::save() {
  ctx_.save();
  stack_.push_back(transform_);
}

void Canvas::restore() {
  ctx_.restore();
  if (!stack_.empty()) {
    transform_ = stack_.back();
    stack_.pop_back();
  }
}

void Canvas::translate(float tx, float ty) {
  Matrix out;
  out.m00 = transform_.m00;
  out.m01 = transform_.m01;
  out.m10 = transform_.m10;
  out.m11 = transform_.m11;
  out.m20 = transform_.m00 * tx + transform_.m10 * ty + transform_.m20;
  out.m21 = transform_.m01 * tx + transform_.m11 * ty + transform_.m21;
  transform_ = out;
  ctx_.set_transform(ToBL(transform_));
}

void Canvas::scale(float sx, float sy) {
  Matrix out = transform_;
  out.m00 *= sx;
  out.m01 *= sy;
  out.m10 *= sx;
  out.m11 *= sy;
  transform_ = out;
  ctx_.set_transform(ToBL(transform_));
}

void Canvas::rotate(float degrees) {
  const double rad = degrees * 3.14159265358979323846 / 180.0;
  const double c = std::cos(rad);
  const double s = std::sin(rad);
  Matrix r;
  r.m00 = c;
  r.m01 = s;
  r.m10 = -s;
  r.m11 = c;
  r.m20 = 0;
  r.m21 = 0;
  Matrix old = transform_;
  Matrix out;
  out.m00 = old.m00 * r.m00 + old.m10 * r.m01;
  out.m01 = old.m01 * r.m00 + old.m11 * r.m01;
  out.m10 = old.m00 * r.m10 + old.m10 * r.m11;
  out.m11 = old.m01 * r.m10 + old.m11 * r.m11;
  out.m20 = old.m00 * r.m20 + old.m10 * r.m21 + old.m20;
  out.m21 = old.m01 * r.m20 + old.m11 * r.m21 + old.m21;
  transform_ = out;
  ctx_.set_transform(ToBL(transform_));
}

void Canvas::concat(const Matrix& m) {
  Matrix out;
  out.m00 = transform_.m00 * m.m00 + transform_.m10 * m.m01;
  out.m01 = transform_.m01 * m.m00 + transform_.m11 * m.m01;
  out.m10 = transform_.m00 * m.m10 + transform_.m10 * m.m11;
  out.m11 = transform_.m01 * m.m10 + transform_.m11 * m.m11;
  out.m20 = transform_.m00 * m.m20 + transform_.m10 * m.m21 + transform_.m20;
  out.m21 = transform_.m01 * m.m20 + transform_.m11 * m.m21 + transform_.m21;
  transform_ = out;
  ctx_.set_transform(ToBL(transform_));
}

void Canvas::applyPaint(const Paint& p, bool stroking) {
  if (p.clear) {
    ctx_.set_comp_op(BL_COMP_OP_CLEAR);
    return;
  }
  ctx_.set_comp_op(BL_COMP_OP_SRC_OVER);
  ctx_.set_fill_style(ToBLRgba(p.color, p.alpha));
  if (stroking) {
    ctx_.set_stroke_style(ToBLRgba(p.color, p.alpha));
    ctx_.set_stroke_width(p.strokeWidth);
    ctx_.set_stroke_start_cap(BL_STROKE_CAP_ROUND);
    ctx_.set_stroke_end_cap(BL_STROKE_CAP_ROUND);
    ctx_.set_stroke_join(BL_STROKE_JOIN_ROUND);
  }
}

void Canvas::drawRect(const Rect& r, const Paint& p) {
  BLRect bl(r.left, r.top, r.width(), r.height());
  if (p.clear) {
    ctx_.set_comp_op(BL_COMP_OP_CLEAR);
    ctx_.fill_rect(bl);
    ctx_.set_comp_op(BL_COMP_OP_SRC_OVER);
    return;
  }
  if (p.fill) {
    ctx_.set_fill_style(ToBLRgba(p.color, p.alpha));
    ctx_.fill_rect(bl);
  } else {
    applyPaint(p, true);
    ctx_.stroke_rect(bl);
  }
}

void Canvas::drawPath(const Path& path, const Paint& p) {
  BLPath bl = path.toBLPath();
  if (p.clear) {
    ctx_.set_comp_op(BL_COMP_OP_CLEAR);
    ctx_.fill_path(bl);
    ctx_.set_comp_op(BL_COMP_OP_SRC_OVER);
    return;
  }
  if (p.fill) {
    ctx_.set_fill_style(ToBLRgba(p.color, p.alpha));
    ctx_.fill_path(bl);
  } else {
    applyPaint(p, true);
    ctx_.stroke_path(bl);
  }
}

void Canvas::drawLine(float x0, float y0, float x1, float y1, const Paint& p) {
  if (p.clear) {
    ctx_.set_comp_op(BL_COMP_OP_CLEAR);
    ctx_.set_stroke_width(p.strokeWidth > 0 ? p.strokeWidth : 1.0f);
    ctx_.set_stroke_start_cap(BL_STROKE_CAP_ROUND);
    ctx_.set_stroke_end_cap(BL_STROKE_CAP_ROUND);
    ctx_.stroke_line(BLLine(x0, y0, x1, y1));
    ctx_.set_comp_op(BL_COMP_OP_SRC_OVER);
    return;
  }
  applyPaint(p, true);
  // Ensure round caps for freehand strokes.
  ctx_.stroke_line(BLLine(x0, y0, x1, y1));
}

void Canvas::drawCircle(float cx, float cy, float r, const Paint& p) {
  BLCircle c(cx, cy, r);
  if (p.clear) {
    ctx_.set_comp_op(BL_COMP_OP_CLEAR);
    ctx_.fill_circle(c);
    ctx_.set_comp_op(BL_COMP_OP_SRC_OVER);
    return;
  }
  if (p.fill) {
    ctx_.set_fill_style(ToBLRgba(p.color, p.alpha));
    ctx_.fill_circle(c);
  } else {
    applyPaint(p, true);
    ctx_.stroke_circle(c);
  }
}

void Canvas::drawOval(const Rect& r, const Paint& p) {
  BLEllipse e((r.left + r.right) * 0.5, (r.top + r.bottom) * 0.5,
              r.width() * 0.5, r.height() * 0.5);
  if (p.clear) {
    ctx_.set_comp_op(BL_COMP_OP_CLEAR);
    ctx_.fill_ellipse(e);
    ctx_.set_comp_op(BL_COMP_OP_SRC_OVER);
    return;
  }
  if (p.fill) {
    ctx_.set_fill_style(ToBLRgba(p.color, p.alpha));
    ctx_.fill_ellipse(e);
  } else {
    applyPaint(p, true);
    ctx_.stroke_ellipse(e);
  }
}

void Canvas::drawImage(const Bitmap& bmp, float x, float y) {
  drawImage(bmp, x, y, 1.0f);
}

void Canvas::drawImage(const Bitmap& bmp, float x, float y, float opacity) {
  if (bmp.empty()) return;
  opacity = std::clamp(opacity, 0.0f, 1.0f);
  if (opacity <= 0.0f) return;
  // Nearest-neighbor composite: Blend2D blits always bilinear-filter
  // under fractional transforms, which blurred raster layers on any
  // pan/zoom, so layers are resampled with nearest sampling instead.
  const Matrix& t = transform_;
  const bool unitScale = std::abs(t.m00 - 1.0) < 1e-9 &&
                         std::abs(t.m11 - 1.0) < 1e-9 &&
                         std::abs(t.m01) < 1e-9 && std::abs(t.m10) < 1e-9;
  if (unitScale && opacity >= 0.999f) {
    const double tx = x + t.m20;
    const double ty = y + t.m21;
    if (std::abs(tx - std::round(tx)) < 1e-6 &&
        std::abs(ty - std::round(ty)) < 1e-6) {
      ctx_.blit_image(BLPointI(static_cast<int>(std::round(tx)),
                               static_cast<int>(std::round(ty))),
                      bmp.image());
      return;
    }
  }
  // General path: NN-resample the source into a temp image covering the
  // integer device bbox, then integer-blit it (exact copy, no filtering).
  Matrix inv;
  if (!t.invert(&inv)) {
    return;
  }
  const int sw = bmp.width();
  const int sh = bmp.height();
  float dx0, dy0, dx1, dy1, dx2, dy2, dx3, dy3;
  t.mapXY(x, y, &dx0, &dy0);
  t.mapXY(x + sw, y, &dx1, &dy1);
  t.mapXY(x, y + sh, &dx2, &dy2);
  t.mapXY(x + sw, y + sh, &dx3, &dy3);
  int bx0 = static_cast<int>(
      std::floor(std::min(std::min(dx0, dx1), std::min(dx2, dx3))));
  int by0 = static_cast<int>(
      std::floor(std::min(std::min(dy0, dy1), std::min(dy2, dy3))));
  int bx1 = static_cast<int>(
      std::ceil(std::max(std::max(dx0, dx1), std::max(dx2, dx3))));
  int by1 = static_cast<int>(
      std::ceil(std::max(std::max(dy0, dy1), std::max(dy2, dy3))));
  bx0 = std::max(bx0, 0);
  by0 = std::max(by0, 0);
  bx1 = std::min(bx1, target_w_);
  by1 = std::min(by1, target_h_);
  if (bx1 <= bx0 || by1 <= by0) return;

  BLImage tmp;
  if (tmp.create(bx1 - bx0, by1 - by0, BL_FORMAT_PRGB32) != BL_SUCCESS) {
    return;
  }
  BLImageData srcData, tmpData;
  if (bmp.image().get_data(&srcData) != BL_SUCCESS) return;
  if (tmp.make_mutable(&tmpData) != BL_SUCCESS) return;
  const uint32_t* src = static_cast<const uint32_t*>(srcData.pixel_data);
  uint32_t* dst = static_cast<uint32_t*>(tmpData.pixel_data);
  const int sstride = static_cast<int>(srcData.stride / 4);
  const int dstride = static_cast<int>(tmpData.stride / 4);
  for (int j = 0; j < by1 - by0; ++j) {
    for (int i = 0; i < bx1 - bx0; ++i) {
      float ux, uy;
      inv.mapXY(static_cast<float>(bx0 + i), static_cast<float>(by0 + j), &ux,
                &uy);
      const int si = static_cast<int>(std::floor(ux - x + 0.5f));
      const int sj = static_cast<int>(std::floor(uy - y + 0.5f));
      uint32_t px = 0;
      if (si >= 0 && si < sw && sj >= 0 && sj < sh) {
        px = src[sj * sstride + si];
        if (opacity < 0.999f) {
          // Scale the premultiplied pixel by the global opacity so the
          // follow-up SRC_OVER blit composites it correctly.
          const uint32_t a =
              static_cast<uint32_t>(((px >> 24) & 0xFF) * opacity);
          const uint32_t r =
              static_cast<uint32_t>(((px >> 16) & 0xFF) * opacity);
          const uint32_t g =
              static_cast<uint32_t>(((px >> 8) & 0xFF) * opacity);
          const uint32_t b = static_cast<uint32_t>((px & 0xFF) * opacity);
          px = (a << 24) | (r << 16) | (g << 8) | b;
        }
      }
      dst[j * dstride + i] = px;
    }
  }
  save();
  transform_.setIdentity();
  ctx_.set_transform(ToBL(transform_));
  ctx_.blit_image(BLPointI(bx0, by0), tmp);
  restore();
}

void Canvas::drawString(const char* text, float x, float y, const Font& font,
                        const Paint& p) {
  if (text == nullptr || text[0] == '\0' || !font.valid()) return;
  if (p.fill) {
    ctx_.set_fill_style(ToBLRgba(p.color, p.alpha));
    ctx_.fill_utf8_text(BLPoint(x, y), font.blFont(), text);
  } else {
    // Stroked text (outline). Used for palette key hints so they stay
    // legible over any swatch color.
    applyPaint(p, true);
    ctx_.stroke_utf8_text(BLPoint(x, y), font.blFont(), text);
  }
}

}  // namespace nanote
