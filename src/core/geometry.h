#pragma once

#include <algorithm>
#include <cmath>

namespace nanote {

struct Rect {
  float left = 0;
  float top = 0;
  float right = 0;
  float bottom = 0;

  static Rect MakeEmpty() {
    return {0, 0, 0, 0};
  }
  static Rect MakeXYWH(float x, float y, float w, float h) {
    return {x, y, x + w, y + h};
  }
  static Rect MakeLTRB(float l, float t, float r, float b) {
    return {l, t, r, b};
  }

  float width() const {
    return right - left;
  }
  float height() const {
    return bottom - top;
  }
  float centerX() const {
    return (left + right) * 0.5f;
  }
  float centerY() const {
    return (top + bottom) * 0.5f;
  }

  bool isEmpty() const {
    return right <= left || bottom <= top;
  }

  bool contains(float x, float y) const {
    return x >= left && x < right && y >= top && y < bottom;
  }
  bool contains(const Rect& o) const {
    return o.left >= left && o.right <= right && o.top >= top &&
           o.bottom <= bottom;
  }
  bool intersects(const Rect& o) const {
    return left < o.right && right > o.left && top < o.bottom && bottom > o.top;
  }
  bool intersect(const Rect& o) {
    left = std::max(left, o.left);
    top = std::max(top, o.top);
    right = std::min(right, o.right);
    bottom = std::min(bottom, o.bottom);
    return !isEmpty();
  }
  void join(const Rect& o) {
    if (o.isEmpty()) return;
    if (isEmpty()) {
      *this = o;
      return;
    }
    left = std::min(left, o.left);
    top = std::min(top, o.top);
    right = std::max(right, o.right);
    bottom = std::max(bottom, o.bottom);
  }
  void offset(float dx, float dy) {
    left += dx;
    right += dx;
    top += dy;
    bottom += dy;
  }
  void outset(float dx, float dy) {
    left -= dx;
    top -= dy;
    right += dx;
    bottom += dy;
  }
};

// 2D affine transform: [ m00 m10 tx ]
//                      [ m01 m11 ty ]
// Compatible with BLMatrix2D layout (m00,m01,m10,m11,m20,m21).
struct Matrix {
  double m00 = 1, m01 = 0;
  double m10 = 0, m11 = 1;
  double m20 = 0, m21 = 0;

  void setIdentity() {
    m00 = 1;
    m01 = 0;
    m10 = 0;
    m11 = 1;
    m20 = 0;
    m21 = 0;
  }
  void setTranslate(float tx, float ty) {
    setIdentity();
    m20 = tx;
    m21 = ty;
  }
  void setScale(float sx, float sy) {
    setIdentity();
    m00 = sx;
    m11 = sy;
  }
  void setRotate(float degrees, float cx = 0, float cy = 0) {
    const double rad = degrees * 3.14159265358979323846 / 180.0;
    const double c = std::cos(rad);
    const double s = std::sin(rad);
    // T(cx,cy) * R * T(-cx,-cy)
    m00 = c;
    m01 = s;
    m10 = -s;
    m11 = c;
    m20 = cx - cx * c + cy * s;
    m21 = cy - cx * s - cy * c;
  }
  void preTranslate(float tx, float ty) {
    // this = T(tx,ty) * this
    m20 += tx * m00 + ty * m10;
    m21 += tx * m01 + ty * m11;
  }
  void preScale(float sx, float sy) {
    m00 *= sx;
    m01 *= sx;
    m10 *= sy;
    m11 *= sy;
  }
  void postTranslate(float tx, float ty) {
    // this = this * T(tx,ty)  => m20 += tx? Actually:
    // [M | t] * [I | d] = [M | t + M*d]
    m20 += m00 * tx + m10 * ty;
    m21 += m01 * tx + m11 * ty;
  }
  void postScale(float sx, float sy) {
    // this = this * S
    m00 *= sx;
    m01 *= sy;
    m10 *= sx;
    m11 *= sy;
  }
  bool invert(Matrix* out) const {
    const double det = m00 * m11 - m01 * m10;
    if (std::abs(det) < 1e-12) {
      if (out) out->setIdentity();
      return false;
    }
    const double inv = 1.0 / det;
    Matrix r;
    r.m00 = m11 * inv;
    r.m01 = -m01 * inv;
    r.m10 = -m10 * inv;
    r.m11 = m00 * inv;
    r.m20 = (m10 * m21 - m11 * m20) * inv;
    r.m21 = (m01 * m20 - m00 * m21) * inv;
    if (out) *out = r;
    return true;
  }
  void mapXY(float x, float y, float* ox, float* oy) const {
    *ox = static_cast<float>(m00 * x + m10 * y + m20);
    *oy = static_cast<float>(m01 * x + m11 * y + m21);
  }
};

}  // namespace nanote
