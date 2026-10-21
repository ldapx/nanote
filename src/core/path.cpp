#include "core/path.h"

#include <cmath>

namespace nanote {

void Path::moveTo(float x, float y) {
  IterCmd c{};
  c.verb = 0;
  c.pts[0] = x;
  c.pts[1] = y;
  cmds_.push_back(c);
}

void Path::lineTo(float x, float y) {
  IterCmd c{};
  c.verb = 1;
  c.pts[0] = x;
  c.pts[1] = y;
  cmds_.push_back(c);
}

void Path::quadTo(float cx, float cy, float x, float y) {
  IterCmd c{};
  c.verb = 2;
  c.pts[0] = cx;
  c.pts[1] = cy;
  c.pts[2] = x;
  c.pts[3] = y;
  cmds_.push_back(c);
}

void Path::cubicTo(float c0x, float c0y, float c1x, float c1y, float x,
                   float y) {
  IterCmd c{};
  c.verb = 3;
  c.pts[0] = c0x;
  c.pts[1] = c0y;
  c.pts[2] = c1x;
  c.pts[3] = c1y;
  c.pts[4] = x;
  c.pts[5] = y;
  cmds_.push_back(c);
}

void Path::close() {
  IterCmd c{};
  c.verb = 4;
  cmds_.push_back(c);
}

void Path::addRect(const Rect& r, PathDirection) {
  moveTo(r.left, r.top);
  lineTo(r.right, r.top);
  lineTo(r.right, r.bottom);
  lineTo(r.left, r.bottom);
  close();
}

void Path::addCircle(float cx, float cy, float radius, PathDirection) {
  // Approximate with 4 cubic (kappa) segments.
  const float k = 0.5522847498f * radius;
  moveTo(cx + radius, cy);
  cubicTo(cx + radius, cy + k, cx + k, cy + radius, cx, cy + radius);
  cubicTo(cx - k, cy + radius, cx - radius, cy + k, cx - radius, cy);
  cubicTo(cx - radius, cy - k, cx - k, cy - radius, cx, cy - radius);
  cubicTo(cx + k, cy - radius, cx + radius, cy - k, cx + radius, cy);
  close();
}

void Path::addOval(const Rect& r, PathDirection) {
  const float cx = (r.left + r.right) * 0.5f;
  const float cy = (r.top + r.bottom) * 0.5f;
  const float rx = (r.right - r.left) * 0.5f;
  const float ry = (r.bottom - r.top) * 0.5f;
  if (rx <= 0 || ry <= 0) return;
  const float kx = 0.5522847498f * rx;
  const float ky = 0.5522847498f * ry;
  moveTo(cx + rx, cy);
  cubicTo(cx + rx, cy + ky, cx + kx, cy + ry, cx, cy + ry);
  cubicTo(cx - kx, cy + ry, cx - rx, cy + ky, cx - rx, cy);
  cubicTo(cx - rx, cy - ky, cx - kx, cy - ry, cx, cy - ry);
  cubicTo(cx + kx, cy - ry, cx + rx, cy - ky, cx + rx, cy);
  close();
}

void Path::arcTo(const Rect& oval, float startDeg, float sweepDeg,
                 bool forceMoveTo) {
  const float cx = (oval.left + oval.right) * 0.5f;
  const float cy = (oval.top + oval.bottom) * 0.5f;
  const float rx = (oval.right - oval.left) * 0.5f;
  const float ry = (oval.bottom - oval.top) * 0.5f;
  if (rx <= 0 || ry <= 0) return;
  const float kPi = 3.14159265358979323846f;
  const float start = startDeg * kPi / 180.0f;
  const float sweep = sweepDeg * kPi / 180.0f;
  const int segs =
      std::max(1, static_cast<int>(std::ceil(std::abs(sweep) / (kPi / 8.0f))));
  float prev_x = cx + rx * std::cos(start);
  float prev_y = cy + ry * std::sin(start);
  if (forceMoveTo || cmds_.empty()) {
    moveTo(prev_x, prev_y);
  } else {
    lineTo(prev_x, prev_y);
  }
  for (int i = 1; i <= segs; ++i) {
    const float a = start + sweep * (static_cast<float>(i) / segs);
    lineTo(cx + rx * std::cos(a), cy + ry * std::sin(a));
  }
}

void Path::clear() {
  cmds_.clear();
}

Rect Path::getBounds() const {
  if (cmds_.empty()) return Rect::MakeEmpty();
  BLPath bl = toBLPath();
  BLBox box;
  if (bl.get_bounding_box(&box) != BL_SUCCESS) return Rect::MakeEmpty();
  return Rect::MakeLTRB(static_cast<float>(box.x0), static_cast<float>(box.y0),
                        static_cast<float>(box.x1), static_cast<float>(box.y1));
}

bool Path::contains(float x, float y) const {
  if (cmds_.empty()) return false;
  BLPath bl = toBLPath();
  BLPoint p{x, y};
  return bl.hit_test(p, BL_FILL_RULE_EVEN_ODD) != BL_HIT_TEST_OUT;
}

Path Path::transformed(const Matrix& m) const {
  Path out = *this;
  out.transform(m);
  return out;
}

void Path::transform(const Matrix& m) {
  for (auto& c : cmds_) {
    const int n = (c.verb == 0 || c.verb == 1)
                      ? 1
                      : (c.verb == 2 ? 2 : (c.verb == 3 ? 3 : 0));
    for (int i = 0; i < n; ++i) {
      float ox = 0, oy = 0;
      // Matrix::mapXY is const; use manual math here.
      const double x = c.pts[i * 2];
      const double y = c.pts[i * 2 + 1];
      ox = static_cast<float>(m.m00 * x + m.m10 * y + m.m20);
      oy = static_cast<float>(m.m01 * x + m.m11 * y + m.m21);
      c.pts[i * 2] = ox;
      c.pts[i * 2 + 1] = oy;
    }
  }
}

BLPath Path::toBLPath() const {
  BLPath bl;
  for (const auto& c : cmds_) {
    switch (c.verb) {
      case 0:
        bl.move_to(c.pts[0], c.pts[1]);
        break;
      case 1:
        bl.line_to(c.pts[0], c.pts[1]);
        break;
      case 2:
        bl.quad_to(c.pts[0], c.pts[1], c.pts[2], c.pts[3]);
        break;
      case 3:
        bl.cubic_to(c.pts[0], c.pts[1], c.pts[2], c.pts[3], c.pts[4], c.pts[5]);
        break;
      case 4:
        bl.close();
        break;
    }
  }
  return bl;
}

}  // namespace nanote
