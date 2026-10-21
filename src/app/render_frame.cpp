#include "app/app.h"

#include "app/doc_session.h"
#include "app/file_dialog.h"
#include "canvas/viewport.h"
#include "color/color_manager.h"
#include "command/command_executor.h"
#include "core/color.h"
#include "core/easing.h"
#include "core/font.h"
#include "core/geometry.h"
#include "core/path.h"
#include "core/types.h"
#include "doc/document.h"
#include "drawing/brush_engine.h"
#include "drawing/drawing.h"
#include "input/input_manager.h"
#include "layer/layer.h"
#include "render/canvas.h"
#include "ui/main_menu.h"
#include "ui/tool_menu.h"
#include "ui/ui_hit_test.h"
#include "ui/ui_renderer.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace nanote {

void App::RenderFrame() {
  if (menu_transition.active) {
    RenderMenuTransition();
  } else if (input.IsInMainMenu()) {
    RenderMenu();
  } else {
    Canvas canvas(host.image(), window_width, window_height);
    RenderDocumentContent(canvas);
    RenderEditingOverlays(canvas);
    RenderChrome(canvas);
  }
  host.Present();
}

void App::RenderMenuTransition() {
  // Unified zoom-out + tile flight: ease the viewport toward fit while
  // every content frame is blitted shrinking into the menu tile.
  auto& tr = menu_transition;
  tr.t += std::min(frame_dt, 0.05f) / kMenuTransitionDur;
  const float t = std::min(tr.t, 1.0f);
  if (tr.do_zoom) {
    const float e = EaseOutCubic(t);
    viewport.SetZoom(tr.from_zoom * std::pow(tr.fit_zoom / tr.from_zoom, e));
    viewport.center_.x =
        tr.from_center.x + (tr.fit_center.x - tr.from_center.x) * e;
    viewport.center_.y =
        tr.from_center.y + (tr.fit_center.y - tr.from_center.y) * e;
  }
  // Content renders offscreen (never carries screen-space UI), then flies
  // into the tile over the live menu.
  if (tr.frame.width() != window_width || tr.frame.height() != window_height) {
    tr.frame.allocN32Pixels(window_width, window_height);
  }
  {
    Canvas frame_canvas(tr.frame);
    RenderDocumentContent(frame_canvas);
  }
  Canvas menu_canvas(host.image(), window_width, window_height);
  menu_canvas.clear(MakeARGB(255, 18, 18, 20));
  const double menu_time = static_cast<double>(frame_now) / 1000.0;
  DrawMainMenu(&menu_canvas, input, ui_typeface, window_width, window_height,
               session.name, session.dirty, menu_time);
  // Keep grid navigation in sync, same as the plain menu branch.
  const MainMenuLayout drawn_layout = ComputeMainMenuLayout(
      window_width, window_height, input.GetMainMenuRowCount(),
      input.GetMainMenuSelected());
  input.SetMainMenuCols(drawn_layout.cols);
  Rect target = Rect::MakeXYWH((window_width - 176.0f) * 0.5f,
                               (window_height - 96.0f) * 0.5f, 176.0f, 96.0f);
  for (const auto& cell : drawn_layout.cells) {
    if (cell.row == input.GetMainMenuSelected()) {
      target = cell.thumb;
      break;
    }
  }
  const Rect full = Rect::MakeXYWH(0, 0, static_cast<float>(window_width),
                                   static_cast<float>(window_height));
  const Rect cur = LerpRect(full, target, EaseInOutCubic(t));
  // Short crossfade at the end hides the handoff to the file thumbnail.
  const float alpha = t < 0.8f ? 1.0f : 1.0f - (t - 0.8f) / 0.2f;
  const float sx = cur.width() / static_cast<float>(window_width);
  const float sy = cur.height() / static_cast<float>(window_height);
  if (sx > 0.0f && sy > 0.0f) {
    menu_canvas.save();
    menu_canvas.translate(cur.left, cur.top);
    menu_canvas.scale(sx, sy);
    menu_canvas.drawImage(tr.frame, 0, 0, alpha);
    menu_canvas.restore();
  }
}

void App::RenderMenu() {
  Canvas menu_canvas(host.image(), window_width, window_height);
  menu_canvas.clear(MakeARGB(255, 18, 18, 20));
  const double menu_time = static_cast<double>(frame_now) / 1000.0;
  DrawMainMenu(&menu_canvas, input, ui_typeface, window_width, window_height,
               session.name, session.dirty, menu_time);
  // Keep grid navigation in sync with the layout just drawn so arrow keys
  // move by the visible column count next frame.
  const MainMenuLayout drawn_layout = ComputeMainMenuLayout(
      window_width, window_height, input.GetMainMenuRowCount(),
      input.GetMainMenuSelected());
  input.SetMainMenuCols(drawn_layout.cols);
}

void App::RenderEditingOverlays(Canvas& canvas) {
  canvas.save();
  canvas.concat(viewport.GetCanvasToScreen());
  RenderPaintPreviews(canvas);
  RenderSelectionOverlays(canvas);
  RenderEditorOverlays(canvas);
  canvas.restore();
}

void App::RenderPaintPreviews(Canvas& canvas) {
  // Ephemeral stroke preview (same dab engine as commit).
  if (stroke.is_drawing && !stroke.points.empty()) {
    const DabParams preview_params = ResolveToolParams(
        input.GetTool(), input.GetToolSettings(input.GetTool()));
    if (input.GetMode() == Mode::kDraw) {
      PreviewStrokeDabs(&canvas, stroke.points, preview_params,
                        color_manager.GetCurrentColor(), stroke.seed,
                        /*erase=*/false);
    } else if (input.GetMode() == Mode::kErase) {
      PreviewStrokeDabs(&canvas, stroke.points, preview_params,
                        color_manager.GetCurrentColor(), stroke.seed,
                        /*erase=*/true);
    }
  }

  // Ephemeral shape preview.
  if (shape.is_active) {
    const bool shift = SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_LSHIFT] ||
                       SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_RSHIFT];
    Path preview_path =
        BuildShapePath(shape.tool, shape.anchor.x, shape.anchor.y,
                       shape.current.x, shape.current.y, shift);
    if (!preview_path.isEmpty()) {
      Paint fill_preview;
      fill_preview.color = (color_manager.GetFillColor());
      fill_preview.fill = true;

      Paint stroke_preview;
      stroke_preview.color = (color_manager.GetStrokeColor());
      stroke_preview.fill = false;
      stroke_preview.strokeWidth = 2.0f;

      if (GetA(color_manager.GetFillColor()) > 0) {
        canvas.drawPath(preview_path, fill_preview);
      }
      if (GetA(color_manager.GetStrokeColor()) > 0) {
        canvas.drawPath(preview_path, stroke_preview);
      }
    }
  }

  // Ephemeral erase/select rectangle preview.
  if (select_rect.is_active &&
      (input.GetMode() == Mode::kErase || input.GetMode() == Mode::kSelect)) {
    const float x0 = std::min(select_rect.anchor.x, select_rect.current.x);
    const float y0 = std::min(select_rect.anchor.y, select_rect.current.y);
    const float x1 = std::max(select_rect.anchor.x, select_rect.current.x);
    const float y1 = std::max(select_rect.anchor.y, select_rect.current.y);
    Rect rect = Rect::MakeLTRB(x0, y0, x1, y1);
    if (!rect.isEmpty()) {
      Paint fill_preview;
      fill_preview.color = (color_manager.GetCurrentColor());
      fill_preview.fill = true;
      fill_preview.alpha = 0.15f;

      Paint stroke_preview;
      stroke_preview.color = (color_manager.GetCurrentColor());
      stroke_preview.fill = false;
      stroke_preview.strokeWidth = 1.0f;

      canvas.drawRect(rect, fill_preview);
      canvas.drawRect(rect, stroke_preview);
    }
  }

  // Freehand lasso trail preview.
  if (lasso_active && lasso_points.size() >= 2 &&
      input.GetMode() == Mode::kSelect && input.GetTool() == Tool::kLasso) {
    Paint lasso_paint;
    lasso_paint.color = (color_manager.GetCurrentColor());
    lasso_paint.fill = false;
    lasso_paint.strokeWidth = 1.5f;
    for (size_t i = 1; i < lasso_points.size(); ++i) {
      canvas.drawLine(lasso_points[i - 1].x, lasso_points[i - 1].y,
                      lasso_points[i].x, lasso_points[i].y, lasso_paint);
    }
    canvas.drawLine(lasso_points.back().x, lasso_points.back().y,
                    lasso_points.front().x, lasso_points.front().y,
                    lasso_paint);
  }
}

void App::RenderSelectionOverlays(Canvas& canvas) {
  // Selection highlight.
  DrawSelectionHighlight(&canvas, layer_stack.GetSelection());

  // Floating raster selection: pixels lifted for the current drag.
  if (gizmo.raster_float.active && !gizmo.raster_float.view.empty()) {
    const auto& layers = layer_stack.GetLayers();
    float opacity = 1.0f;
    if (gizmo.float_layer >= 0 &&
        gizmo.float_layer < static_cast<int>(layers.size())) {
      opacity = layers[gizmo.float_layer]->opacity;
    }
    canvas.drawImage(gizmo.raster_float.view, gizmo.raster_float.x,
                     gizmo.raster_float.y, opacity);
  }

  // Transform gizmo.
  if (input.GetMode() == Mode::kTransform &&
      layer_stack.GetSelection().active) {
    DrawTransformGizmo(&canvas, layer_stack.GetSelection().canvas_rect,
                       viewport.zoom_);
  }
}

void App::RenderEditorOverlays(Canvas& canvas) {
  // Node editor UI.
  if (input.GetMode() == Mode::kNode && node_editor.editing) {
    // Draw the path being edited with a highlight stroke.
    Paint highlight_paint;
    highlight_paint.color = MakeARGB(200, 100, 200, 255);
    highlight_paint.fill = false;
    highlight_paint.strokeWidth = 2.0f / viewport.zoom_;
    Path edited_path = node_editor.path.ToPath();
    canvas.drawPath(edited_path, highlight_paint);

    DrawNodeEditor(&canvas, node_editor, viewport.zoom_);
  }

  // Text editor preview.
  if (text_editor.active) {
    Font font(text_editor.typeface, text_editor.font_size);
    Paint paint = Paint::Fill(color_manager.GetCurrentColor());
    if (!text_editor.text.empty()) {
      canvas.drawString(text_editor.text.c_str(), text_editor.x, text_editor.y,
                        font, paint);
    }
    // Draw cursor.
    if (text_editor.cursor_visible) {
      float cursor_x = text_editor.x;
      if (!text_editor.text.empty()) {
        cursor_x +=
            font.measureText(text_editor.text.c_str(), text_editor.text.size());
      }
      Paint cursor_paint;
      cursor_paint.color = (color_manager.GetCurrentColor());
      cursor_paint.strokeWidth = 1.0f / viewport.zoom_;
      float half_height = text_editor.font_size * 0.5f;
      canvas.drawLine(cursor_x, text_editor.y - half_height, cursor_x,
                      text_editor.y + half_height, cursor_paint);
    }
  }

  // Ruler overlays.
  if (input.GetMode() == Mode::kRuler) {
    DrawRulerOverlay(&canvas, input.GetTool(), ruler.measuring, ruler.start,
                     ruler.current, ruler.angle_stage, ruler.angle_p0,
                     ruler.angle_p1, ruler.angle_p2, viewport.zoom_,
                     ui_typeface);
  }
}

void App::RenderChrome(Canvas& canvas) {
  // Shift+drag tool-size preview: ring at the cursor sized like the dab
  // (diameter = tool size), plus the pixel size readout.
  if (gestures.size_active) {
    Paint ring = Paint::Stroke(MakeARGB(255, 235, 235, 235), 1.5f);
    canvas.drawCircle(gestures.size_ax, gestures.size_ay,
                      input.GetToolSize() * 0.5f * viewport.zoom_, ring);
    Font size_font(ui_typeface, 13);
    Paint size_paint = Paint::Fill(MakeARGB(255, 220, 220, 220));
    char size_text[32];
    std::snprintf(size_text, sizeof(size_text), "%.0f px",
                  static_cast<double>(input.GetToolSize()));
    canvas.drawString(size_text, gestures.size_ax + 12.0f,
                      gestures.size_ay - 12.0f, size_font, size_paint);
  }

  // Compute canvas-space mouse position and vector hit-test.
  const Vec2 mouse_canvas = viewport.ScreenToCanvas(mouse_x, mouse_y);
  std::string hover_info;
  if (const Layer* active = layer_stack.GetActiveLayer()) {
    if (active->type == LayerType::kVector) {
      const auto* vlayer = static_cast<const VectorLayer*>(active);
      if (input.GetMode() == Mode::kNode && node_editor.editing) {
        if (node_editor.hovered_node >= 0) {
          hover_info = "node " + std::to_string(node_editor.hovered_node);
          if (node_editor.hovered_type == NodeHitType::kCpIn) {
            hover_info += " (cp-in)";
          } else if (node_editor.hovered_type == NodeHitType::kCpOut) {
            hover_info += " (cp-out)";
          }
        } else {
          hover_info = "editing path";
        }
      } else if (const VectorObject* hit =
                     vlayer->HitTest(mouse_canvas.x, mouse_canvas.y)) {
        if (dynamic_cast<const TextObject*>(hit) != nullptr) {
          hover_info = "hit: text";
        } else {
          hover_info = "hit: path";
        }
      }
    }
  }

  // Screen-space UI.
  DrawToolbar(&canvas, input, ui_typeface, window_width);
  DrawCurrentColor(&canvas, color_manager, input, ui_typeface, window_width);
  DrawPalette(&canvas, input, color_manager, ui_typeface, window_width,
              window_height);
  DrawLayersPanel(&canvas, layer_stack, ui_typeface, window_width,
                  window_height);

  if (input.IsCommandModeActive()) {
    DrawCommandMenu(&canvas, input, ui_typeface, window_width, window_height);
  } else {
    DrawStatusBar(&canvas, input, mouse_canvas, ui_typeface, window_width,
                  window_height, hover_info);
  }

  // Slideshow position indicator (top-center, below the toolbar).
  if (session.props.type == DocType::kSlideshow &&
      !session.props.slides.empty()) {
    const int n = static_cast<int>(session.props.slides.size());
    const int cur = std::clamp(session.props.current_slide, 0, n - 1);
    const std::string slide_text = "Slide " + std::to_string(cur + 1) + "/" +
                                   std::to_string(n) + " \xc2\xb7 " +
                                   session.props.slides[cur].name;
    Font slide_font(ui_typeface, 13);
    Paint slide_paint = Paint::Fill(MakeARGB(255, 200, 200, 200));
    const float slide_w =
        slide_font.measureText(slide_text.c_str(), slide_text.size());
    canvas.drawString(slide_text.c_str(), (window_width - slide_w) * 0.5f,
                      kToolbarHeight + 16.0f, slide_font, slide_paint);
  }

  DrawCommandBar(&canvas, input, ui_typeface, window_width, window_height);
  DrawPaletteBrowser(&canvas, input, color_manager, ui_typeface, window_width,
                     window_height);
  DrawToolMenu(&canvas, input, ui_typeface, window_width, window_height);
}

}  // namespace nanote
