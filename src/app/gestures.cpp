#include "app/gestures.h"

#include <algorithm>
#include <cmath>

namespace nanote {
namespace {

constexpr float kPi = 3.14159265358979323846f;

float NormalizeAngle(float a) {
  while (a > kPi) a -= 2.0f * kPi;
  while (a < -kPi) a += 2.0f * kPi;
  return a;
}

}  // namespace

void GestureState::StartPan(float x, float y, int source, const Vec2& center) {
  pan_active = true;
  pan_source = source;
  pan_ax = x;
  pan_ay = y;
  pan_center = center;
}

void GestureState::ApplyPan(Viewport& viewport, float x, float y) const {
  viewport.center_.x = pan_center.x + (pan_ax - x) / viewport.zoom_;
  viewport.center_.y = pan_center.y + (pan_ay - y) / viewport.zoom_;
}

void GestureState::StartZoomDrag(float x, float y, int source,
                                 Viewport& viewport) {
  zoom_active = true;
  zoom_source = source;
  zoom_ax = x;
  zoom_ay = y;
  zoom_start = viewport.zoom_;
  zoom_anchor_canvas = viewport.ScreenToCanvas(x, y);
}

void GestureState::ApplyZoomDrag(Viewport& viewport, float x, float y,
                                 int window_width, int window_height) {
  (void)x;  // Horizontal position is irrelevant: zoom follows the vertical
  // distance from the press point, which stays put. Krita-style continuous
  // zoom: vertical drag distance maps exponentially onto zoom (100 px up =
  // 2x). Dragging down past the press point zooms back out. The press point
  // stays put on screen (Krita's still point), corrected for view rotation.
  const float dy_up = zoom_ay - y;
  const float z =
      std::clamp(zoom_start * std::pow(2.0f, dy_up / 100.0f), 0.02f, 64.0f);
  viewport.SetZoom(z);  // also enforces the bounded fit-zoom floor
  const float zc = viewport.zoom_;
  const float c = std::cos(viewport.angle_);
  const float sn = std::sin(viewport.angle_);
  const float dx = zoom_ax - window_width * 0.5f;
  const float dy = zoom_ay - window_height * 0.5f;
  viewport.center_.x = zoom_anchor_canvas.x - (c * dx + sn * dy) / zc;
  viewport.center_.y = zoom_anchor_canvas.y - (-sn * dx + c * dy) / zc;
}

void GestureState::StartRotate(float cursor_x, float cursor_y, int window_width,
                               int window_height, float current_angle) {
  rot_active = true;
  rot_anchor_angle = std::atan2(cursor_y - window_height * 0.5f,
                                cursor_x - window_width * 0.5f);
  rot_start = current_angle;
}

void GestureState::ApplyRotate(Viewport& viewport, float cursor_x,
                               float cursor_y, int window_width,
                               int window_height) const {
  const float a = std::atan2(cursor_y - window_height * 0.5f,
                             cursor_x - window_width * 0.5f);
  float na = rot_start + NormalizeAngle(a - rot_anchor_angle);
  na = NormalizeAngle(na);
  if (std::abs(na) < 0.035f) na = 0.0f;  // snap to unrotated
  viewport.angle_ = na;
}

void GestureState::StartSize(float x, float y) {
  size_active = true;
  size_ax = x;
  size_ay = y;
  size_last_x = x;
  // Hide the system cursor for the drag: the static ring at the press point
  // shows the size instead, so nothing visibly moves until release restores
  // everything.
  SDL_HideCursor();
}

float GestureState::ConsumeSizeDelta(float x, float zoom) {
  // Krita-style: only horizontal motion counts, converted to canvas pixels
  // (1 canvas px per screen px at 100% zoom), so dragging right grows the
  // tool and left shrinks it at the same rate Krita uses.
  const float delta = (x - size_last_x) / std::max(zoom, 1e-6f);
  if (std::abs(delta) < 0.01f) return 0.0f;
  size_last_x = x;
  return delta;
}

void GestureState::EndSize(SDL_Window* window) {
  if (!size_active) return;
  size_active = false;
  SDL_ShowCursor();
  SDL_WarpMouseInWindow(window, static_cast<int>(size_ax),
                        static_cast<int>(size_ay));
}

void GestureState::CancelAll(SDL_Window* window) {
  pan_active = false;
  pan_source = 0;
  zoom_active = false;
  zoom_source = 0;
  EndSize(window);
  rot_active = false;
}

}  // namespace nanote
