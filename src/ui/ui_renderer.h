#pragma once

#include "canvas/viewport.h"
#include "color/color_manager.h"
#include "core/font.h"
#include "core/geometry.h"
#include "input/input_manager.h"
#include "layer/layer.h"

#include <memory>
#include <string>
#include <vector>

namespace nanote {

class Canvas;

constexpr int kToolbarHeight = 32;
constexpr int kStatusBarHeight = 24;
constexpr int kCommandBarHeight = 28;
constexpr int kCommandMenuHeight = 120;
constexpr int kCommandMenuItemHeight = 20;
constexpr int kToolMargin = 4;
constexpr int kToolPaddingX = 12;

// Palette strip on the window's right edge.
constexpr int kPaletteBoxOuter = 28;
constexpr int kPaletteGap = 4;
constexpr int kPaletteColumnX = 8;

// Bounding box of the collapsible palette strip. Shared by the renderer,
// its click hit-testing and palette hover checks so the three can't drift.
struct PaletteStripGeometry {
  bool valid = false;
  float start_x = 0.0f;
  int start_y = 0;
  float width = 0.0f;
  int height = 0;
  int full_width = 0;
};

PaletteStripGeometry ComputePaletteStripGeometry(
    const InputManager& input, const ColorManager& color_manager,
    int window_width, int window_height);

enum class ToolbarItemKind { kMode, kModeIndicator, kTool };

struct ToolbarItem {
  ToolbarItemKind kind;
  std::string label;
  Rect rect;
  bool selected = false;
  bool has_strip = false;
  Color strip_color = kColorTransparent;
  Mode mode = Mode::kDraw;  // valid when kind == kMode
  Tool tool = Tool::kNone;  // valid when kind == kTool
};

// Shared toolbar layout: box order, labels and rects used by both the
// toolbar renderer and its click hit-testing, so the two can't drift.
std::vector<ToolbarItem> ComputeToolbarItems(
    const InputManager& input, const std::shared_ptr<Typeface>& typeface,
    int window_width);

void DrawCheckerboard(Canvas* canvas, float zoom, float grid_size);
void DrawCheckerboardRect(Canvas* canvas, float zoom, float grid_size,
                          const Rect& area);
void DrawToolbar(Canvas* canvas, const InputManager& input,
                 const std::shared_ptr<Typeface>& typeface, int window_width);
void DrawCurrentColor(Canvas* canvas, const ColorManager& color_manager,
                      const InputManager& input,
                      const std::shared_ptr<Typeface>& typeface,
                      int window_width);
void DrawPalette(Canvas* canvas, const InputManager& input,
                 const ColorManager& color_manager,
                 const std::shared_ptr<Typeface>& typeface, int window_width,
                 int window_height);
void DrawPaletteBrowser(Canvas* canvas, const InputManager& input,
                        const ColorManager& color_manager,
                        const std::shared_ptr<Typeface>& typeface,
                        int window_width, int window_height);
void DrawToolMenu(Canvas* canvas, const InputManager& input,
                  const std::shared_ptr<Typeface>& typeface, int window_width,
                  int window_height);
void DrawLayersPanel(Canvas* canvas, const LayerStack& layer_stack,
                     const std::shared_ptr<Typeface>& typeface,
                     int window_width, int window_height);
void DrawStatusBar(Canvas* canvas, const InputManager& input,
                   const Vec2& mouse_canvas,
                   const std::shared_ptr<Typeface>& typeface, int window_width,
                   int window_height, const std::string& hover_info = "");
void DrawCommandMenu(Canvas* canvas, const InputManager& input,
                     const std::shared_ptr<Typeface>& typeface,
                     int window_width, int window_height);
void DrawCommandBar(Canvas* canvas, const InputManager& input,
                    const std::shared_ptr<Typeface>& typeface, int window_width,
                    int window_height);

void DrawTransformGizmo(Canvas* canvas, const Rect& bbox, float zoom);
// Gizmo handle hit test for press handling. Returns 4 for the rotation
// handle, 0-3 for the corner handles, -1 for a miss. Uses the same
// geometry as DrawTransformGizmo (8/zoom squares, rotate circle 20/zoom
// above top-center) plus a small grab padding so the hit area matches
// what is drawn. Handles sit on/outside the bbox edge, outside what
// Rect::contains reports, so callers must test handles first.
int HitGizmoHandle(const Rect& bbox, const Vec2& p, float zoom);

void DrawRulerOverlay(Canvas* canvas, Tool tool, bool measuring,
                      const Vec2& start, const Vec2& current, int angle_stage,
                      const Vec2& angle_p0, const Vec2& angle_p1,
                      const Vec2& angle_p2, float zoom,
                      const std::shared_ptr<Typeface>& typeface);

}  // namespace nanote
