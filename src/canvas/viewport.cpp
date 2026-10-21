#include "canvas/viewport.h"

#include <algorithm>
#include <cmath>

namespace nanote {

void Viewport::Pan(float dx, float dy) {
  center_.x += dx;
  center_.y += dy;
}

void Viewport::ZoomIn() {
  target_zoom_ *= 1.2f;
  if (min_zoom_ > 0.0f) target_zoom_ = std::max(target_zoom_, min_zoom_);
  smooth_zooming_ = true;
  zoom_anchor_screen_ = {window_width_ / 2.0f, window_height_ / 2.0f};
}

void Viewport::ZoomOut() {
  target_zoom_ /= 1.2f;
  if (min_zoom_ > 0.0f) target_zoom_ = std::max(target_zoom_, min_zoom_);
  smooth_zooming_ = true;
  zoom_anchor_screen_ = {window_width_ / 2.0f, window_height_ / 2.0f};
}

void Viewport::ResetZoom() {
  target_zoom_ = 1.0f;
  if (min_zoom_ > 0.0f) target_zoom_ = std::max(target_zoom_, min_zoom_);
  zoom_ = target_zoom_;
  smooth_zooming_ = false;
}

void Viewport::SetZoom(float zoom) {
  target_zoom_ = zoom;
  if (min_zoom_ > 0.0f) target_zoom_ = std::max(target_zoom_, min_zoom_);
  zoom_ = target_zoom_;
  smooth_zooming_ = false;
}

void Viewport::SetGridSize(float size) {
  if (size > 0.0f) {
    grid_size_ = size;
  }
}

void Viewport::ZoomAt(float screen_x, float screen_y, float factor) {
  target_zoom_ *= factor;
  if (min_zoom_ > 0.0f) target_zoom_ = std::max(target_zoom_, min_zoom_);
  smooth_zooming_ = true;
  zoom_anchor_screen_ = {screen_x, screen_y};
}

void Viewport::Update(float delta_time) {
  if (min_zoom_ > 0.0f) {
    target_zoom_ = std::max(target_zoom_, min_zoom_);
    if (zoom_ < min_zoom_) zoom_ = min_zoom_;
  }
  if (std::abs(zoom_ - target_zoom_) > 0.0001f) {
    if (smooth_zooming_) {
      const Vec2 before =
          ScreenToCanvas(zoom_anchor_screen_.x, zoom_anchor_screen_.y);
      const float t = std::min(1.0f, zoom_speed_ * delta_time);
      zoom_ = zoom_ + (target_zoom_ - zoom_) * t;
      const Vec2 after =
          ScreenToCanvas(zoom_anchor_screen_.x, zoom_anchor_screen_.y);
      center_.x += before.x - after.x;
      center_.y += before.y - after.y;
    } else {
      const float t = std::min(1.0f, zoom_speed_ * delta_time);
      zoom_ = zoom_ + (target_zoom_ - zoom_) * t;
    }
  } else {
    zoom_ = target_zoom_;
    smooth_zooming_ = false;
  }
}

Matrix Viewport::GetCanvasToScreen() const {
  // Screen = T(win/2) * R(angle) * S(zoom) * T(-center), built directly.
  const double c = std::cos(angle_);
  const double s = std::sin(angle_);
  Matrix res;
  res.m00 = zoom_ * c;
  res.m01 = zoom_ * s;
  res.m10 = -zoom_ * s;
  res.m11 = zoom_ * c;
  res.m20 =
      window_width_ / 2.0 - (zoom_ * c * center_.x - zoom_ * s * center_.y);
  res.m21 =
      window_height_ / 2.0 - (zoom_ * s * center_.x + zoom_ * c * center_.y);
  return res;
}

Vec2 Viewport::ScreenToCanvas(float screen_x, float screen_y) const {
  Matrix matrix = GetCanvasToScreen();
  Matrix inverse;
  if (!matrix.invert(&inverse)) {
    inverse.setIdentity();
  }
  float ox = 0, oy = 0;
  inverse.mapXY(screen_x, screen_y, &ox, &oy);
  return {ox, oy};
}

namespace {
// Axis-aligned bounds of the (possibly rotated) visible quad, in canvas
// coordinates. Used for page covers and other canvas-space culling.
void VisibleBounds(const Viewport& view, float* left, float* top, float* right,
                   float* bottom) {
  const Vec2 corners[4] = {
      view.ScreenToCanvas(0.0f, 0.0f),
      view.ScreenToCanvas(static_cast<float>(view.window_width_), 0.0f),
      view.ScreenToCanvas(0.0f, static_cast<float>(view.window_height_)),
      view.ScreenToCanvas(static_cast<float>(view.window_width_),
                          static_cast<float>(view.window_height_)),
  };
  *left = *right = corners[0].x;
  *top = *bottom = corners[0].y;
  for (int i = 1; i < 4; ++i) {
    *left = std::min(*left, corners[i].x);
    *right = std::max(*right, corners[i].x);
    *top = std::min(*top, corners[i].y);
    *bottom = std::max(*bottom, corners[i].y);
  }
}
}  // namespace

float Viewport::VisibleLeft() const {
  float l = 0, t = 0, r = 0, b = 0;
  VisibleBounds(*this, &l, &t, &r, &b);
  return l;
}

float Viewport::VisibleRight() const {
  float l = 0, t = 0, r = 0, b = 0;
  VisibleBounds(*this, &l, &t, &r, &b);
  return r;
}

float Viewport::VisibleTop() const {
  float l = 0, t = 0, r = 0, b = 0;
  VisibleBounds(*this, &l, &t, &r, &b);
  return t;
}

float Viewport::VisibleBottom() const {
  float l = 0, t = 0, r = 0, b = 0;
  VisibleBounds(*this, &l, &t, &r, &b);
  return b;
}

}  // namespace nanote
