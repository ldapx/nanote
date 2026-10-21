#pragma once

#include <SDL3/SDL.h>

#include "canvas/viewport.h"
#include "core/geometry.h"

namespace nanote {

// View-navigation gestures shared by mouse and pen: space/middle-drag pan,
// Ctrl-drag still-point zoom, Shift+barrel view rotation, and Shift+drag
// tool-size. Groups the gesture scalars and their start/apply steps; event
// code keeps direct field access for press-source routing.
struct GestureState {
  // Space-pan gesture (middle mouse or pen barrel drag, no Ctrl, no Shift).
  // Absolute mapping from the press anchor, so duplicate delivery (pen
  // events plus emulated mouse events) is idempotent.
  bool pan_active = false;
  int pan_source = 0;  // 1 = middle mouse, 2 = pen barrel
  float pan_ax = 0.0f;
  float pan_ay = 0.0f;
  Vec2 pan_center{0.0f, 0.0f};

  // Smooth zoom-drag: Ctrl + middle mouse or Ctrl + pen barrel, dragged
  // vertically. Up zooms in, down zooms out (Krita-style), anchored at the
  // press point.
  bool zoom_active = false;
  int zoom_source = 0;  // 1 = middle mouse, 2 = pen barrel
  float zoom_ax = 0.0f;
  float zoom_ay = 0.0f;
  float zoom_start = 1.0f;
  Vec2 zoom_anchor_canvas{0.0f, 0.0f};

  // Shift + pen barrel drag rotates the whole canvas view around the window
  // center (Krita-style: cursor angle relative to the press angle). Snaps
  // back to exactly 0 near it.
  bool rot_active = false;
  float rot_anchor_angle = 0.0f;
  float rot_start = 0.0f;

  // Shift+drag tool-size gesture (draw/erase modes, left button):
  // incremental horizontal distance, converted to canvas pixels. The system
  // cursor is hidden for the drag and a static ring is drawn at the press
  // point instead, so nothing visibly moves.
  bool size_active = false;
  float size_ax = 0.0f;      // press point (ring drawn here; cursor
  float size_ay = 0.0f;      // restored here on release)
  float size_last_x = 0.0f;  // last applied position (anchor for deltas)

  void StartPan(float x, float y, int source, const Vec2& center);
  void ApplyPan(Viewport& viewport, float x, float y) const;
  void StartZoomDrag(float x, float y, int source, Viewport& viewport);
  void ApplyZoomDrag(Viewport& viewport, float x, float y, int window_width,
                     int window_height);
  void StartRotate(float cursor_x, float cursor_y, int window_width,
                   int window_height, float current_angle);
  void ApplyRotate(Viewport& viewport, float cursor_x, float cursor_y,
                   int window_width, int window_height) const;
  void StartSize(float x, float y);
  // Applies one motion sample; returns the canvas-pixel size delta (0 when
  // the motion is below the apply threshold).
  float ConsumeSizeDelta(float x, float zoom);
  void EndSize(SDL_Window* window);
  // Ends pan/zoom/size/rotate drags (e.g. focus loss, document switch).
  void CancelAll(SDL_Window* window);
};

}  // namespace nanote
