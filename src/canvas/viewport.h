#pragma once

#include "core/geometry.h"
#include "core/types.h"

namespace nanote {

class Viewport {
 public:
  void Pan(float dx, float dy);
  void ZoomIn();
  void ZoomOut();
  void ResetZoom();
  // Snap to an absolute zoom level immediately (no smoothing), so scripted
  // `zoom <pct>` commands stick instead of lerping back to a stale target.
  void SetZoom(float zoom);
  // Zoom by factor while keeping the screen point stable.
  void ZoomAt(float screen_x, float screen_y, float factor);
  void Update(float delta_time);

  // Minimum zoom (0 = unlimited). Bounded documents pin this to the
  // fit-page zoom so the page can never be zoomed out past.
  void SetMinZoom(float zoom) {
    min_zoom_ = zoom > 0.0f ? zoom : 0.0f;
  }
  [[nodiscard]] float GetMinZoom() const {
    return min_zoom_;
  }

  void SetGridVisible(bool visible) {
    show_grid_ = visible;
  }
  [[nodiscard]] bool IsGridVisible() const {
    return show_grid_;
  }
  void SetGridSize(float size);
  [[nodiscard]] float GetGridSize() const {
    return grid_size_;
  }

  [[nodiscard]] Matrix GetCanvasToScreen() const;
  [[nodiscard]] Vec2 ScreenToCanvas(float screen_x, float screen_y) const;

  [[nodiscard]] float VisibleLeft() const;
  [[nodiscard]] float VisibleRight() const;
  [[nodiscard]] float VisibleTop() const;
  [[nodiscard]] float VisibleBottom() const;

  Vec2 center_{0.0f, 0.0f};
  float zoom_ = 1.0f;
  // View rotation in radians (session-only, not persisted). Positive
  // values rotate canvas content counter-clockwise in canvas space, which
  // appears clockwise on screen (y-axis points down). The checkerboard,
  // grid, drawings, and axes all live in canvas space, so they rotate
  // together; only the view changes, never the document.
  float angle_ = 0.0f;
  int window_width_ = 800;
  int window_height_ = 600;
  bool show_grid_ = true;
  float grid_size_ = 50.0f;
  float min_zoom_ = 0.0f;

 private:
  float target_zoom_ = 1.0f;
  float zoom_speed_ = 12.0f;
  bool smooth_zooming_ = false;
  Vec2 zoom_anchor_screen_{0, 0};
};

}  // namespace nanote
