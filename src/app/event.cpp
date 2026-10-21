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

void App::HandleEvent(SDL_Event& event) {
  if (HandleTextTyping(event)) {
    return;
  }
  // Each handler only sees its own event type (SDL_Event is a union:
  // reading e.g. event.button for a motion event yields garbage).
  switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
      HandleSlideStep(event);
      break;
    case SDL_EVENT_MOUSE_MOTION:
      HandleMouseMotion(event);
      break;
    case SDL_EVENT_MOUSE_WHEEL:
      HandleMouseWheel(event);
      break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
      HandleMouseButtonDown(event);
      break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
      HandleMouseButtonUp(event);
      break;
    case SDL_EVENT_PEN_BUTTON_DOWN:
    case SDL_EVENT_PEN_BUTTON_UP:
    case SDL_EVENT_PEN_MOTION:
      HandlePenEvent(event);
      break;
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
      HandleWindowEvent(event);
      break;
    default:
      break;
  }
  input.HandleEvent(event);
  // Entering the main menu: persist work first so the file list shows it,
  // then refresh and highlight the open document.
  if (input.IsInMainMenu() != was_in_menu) {
    if (input.IsInMainMenu()) {
      AutosaveIfNeeded(session, layer_stack, viewport);
      input.RefreshMainMenuDocs();
      int sel = 0;
      const auto& docs = input.GetMainMenuDocs();
      for (size_t i = 0; i < docs.size(); ++i) {
        if (docs[i].name == session.name) {
          sel = static_cast<int>(i) + 1;
          break;
        }
      }
      input.SetMainMenuSelected(sel);
      CancelGestures();
      // Start the zoom-out transition: ease the viewport out to fit while
      // each frame flies into the menu tile.
      menu_transition = MenuEnterTransition();
      menu_transition.active = true;
      menu_transition.from_center = viewport.center_;
      menu_transition.from_zoom = viewport.zoom_;
      if (session.props.has_page() && session.props.page_w > 0.0f &&
          session.props.page_h > 0.0f) {
        // Bounded/slideshow: the entire page.
        menu_transition.fit_center = {0.0f, 0.0f};
        menu_transition.fit_zoom =
            std::min(window_width / session.props.page_w,
                     window_height / session.props.page_h);
        menu_transition.do_zoom = true;
      } else {
        // Infinite: the fitted contents.
        const Rect content = DocContentBounds(layer_stack);
        if (!content.isEmpty() && content.width() > 0.0f &&
            content.height() > 0.0f) {
          menu_transition.fit_center = {content.centerX(), content.centerY()};
          menu_transition.fit_zoom =
              std::min(window_width / content.width(),
                       window_height / content.height()) *
              0.9f;
          menu_transition.do_zoom = true;
        }
      }
      if (menu_transition.do_zoom) {
        menu_transition.fit_zoom =
            std::clamp(menu_transition.fit_zoom, 0.02f, 4.0f);
        // Skip the viewport ease when the view already fits.
        const float dz = std::abs(std::log(menu_transition.from_zoom) -
                                  std::log(menu_transition.fit_zoom));
        const float dcx =
            (menu_transition.from_center.x - menu_transition.fit_center.x) *
            menu_transition.from_zoom;
        const float dcy =
            (menu_transition.from_center.y - menu_transition.fit_center.y) *
            menu_transition.from_zoom;
        if (dz < 0.02f && std::hypot(dcx, dcy) < 4.0f) {
          menu_transition.do_zoom = false;
        }
      }
    } else {
      // Back to the canvas: cancel any in-flight menu-enter animation.
      menu_transition.active = false;
    }
    was_in_menu = input.IsInMainMenu();
  }

  // Global Delete key: remove the current selection or the edited node.
  if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_DELETE &&
      !input.IsCommandModeActive() && !input.IsInMainMenu()) {
    if (input.GetMode() == Mode::kNode && node_editor.editing &&
        node_editor.selected_node >= 0) {
      DeleteNode(node_editor.path, node_editor.selected_node);
      node_editor.selected_node = -1;
      node_editor.selected_type = NodeHitType::kNone;
      Layer* active =
          layer_stack.GetLayers()[layer_stack.GetActiveLayerIndex()].get();
      if (active->type == LayerType::kVector) {
        auto* vector = static_cast<VectorLayer*>(active);
        node_editor.Commit(vector);
        session.dirty = true;
      }
      if (node_editor.path.nodes.empty()) {
        // Path became empty: remove the object.
        Layer* active =
            layer_stack.GetLayers()[layer_stack.GetActiveLayerIndex()].get();
        if (active != nullptr && active->type == LayerType::kVector) {
          auto* vector = static_cast<VectorLayer*>(active);
          if (node_editor.path_object_index >= 0 &&
              node_editor.path_object_index <
                  static_cast<int>(vector->objects.size())) {
            vector->objects.erase(vector->objects.begin() +
                                  node_editor.path_object_index);
            session.dirty = true;
          }
        }
        node_editor.Clear();
      }
    } else {
      auto& selection = layer_stack.GetSelection();
      if (selection.active) {
        Layer* active =
            layer_stack.GetLayers()[layer_stack.GetActiveLayerIndex()].get();
        if (active != nullptr) {
          if (active->type == LayerType::kRaster) {
            EraseRasterArea(static_cast<RasterLayer*>(active),
                            selection.canvas_rect);
            session.dirty = true;
          } else if (active->type == LayerType::kVector) {
            auto* vector = static_cast<VectorLayer*>(active);
            std::vector<int> indices = selection.object_indices;
            std::sort(indices.begin(), indices.end(), std::greater<int>());
            for (int idx : indices) {
              if (idx >= 0 && idx < static_cast<int>(vector->objects.size())) {
                vector->objects.erase(vector->objects.begin() + idx);
                session.dirty = true;
              }
            }
          }
        }
        selection.Clear();
      }
    }
  }
}

// Returns true when the event was fully consumed by text editing.
bool App::HandleTextTyping(SDL_Event& event) {
  // Intercept text editing events before the input manager sees them.
  if (text_editor.active) {
    if (event.type == SDL_EVENT_TEXT_INPUT) {
      text_editor.text += event.text.text;
      text_editor.blink_timer = 0;
      text_editor.cursor_visible = true;
      return true;
    }
    if (event.type == SDL_EVENT_KEY_DOWN) {
      const SDL_Keycode key = event.key.key;
      if (key == SDLK_BACKSPACE) {
        if (!text_editor.text.empty()) {
          text_editor.text.pop_back();
        }
        text_editor.blink_timer = 0;
        text_editor.cursor_visible = true;
        return true;
      }
      if (key == SDLK_RETURN || key == SDLK_RETURN2) {
        if (input.GetTool() == Tool::kParagraph) {
          text_editor.text += '\n';
          text_editor.blink_timer = 0;
          text_editor.cursor_visible = true;
        } else {
          // SingleLine: commit on Enter.
          Layer* active =
              layer_stack.GetLayers()[layer_stack.GetActiveLayerIndex()].get();
          CommitEditorText(
              active, text_editor.text, text_editor.x, text_editor.y,
              text_editor.font_size, text_editor.object_index,
              color_manager.GetCurrentColor(), ui_typeface, BoundedPage());
          session.dirty = true;
          text_editor.active = false;
        }
        return true;
      }
      if (key == SDLK_ESCAPE) {
        // Cancel: revert if editing existing.
        if (text_editor.object_index >= 0) {
          text_editor.text = text_editor.original_text;
        }
        text_editor.active = false;
        return true;
      }
      // Block all other keys while typing text.
      return true;
    }
  }
  return false;
}

void App::HandleSlideStep(SDL_Event& event) {
  // Slideshow stepping with PgUp/PgDn (canvas only, not while typing or
  // commanding).
  if (event.type == SDL_EVENT_KEY_DOWN && !text_editor.active &&
      !input.IsCommandModeActive() && !input.IsInMainMenu() &&
      session.props.type == DocType::kSlideshow &&
      !session.props.slides.empty()) {
    const SDL_Keycode skey = event.key.key;
    if (skey == SDLK_PAGEDOWN || skey == SDLK_PAGEUP) {
      const int n = static_cast<int>(session.props.slides.size());
      int cur = session.props.current_slide;
      if (cur < 0 || cur >= n) cur = 0;
      cur = (skey == SDLK_PAGEDOWN) ? (cur + 1) % n : (cur - 1 + n) % n;
      session.props.current_slide = cur;
      viewport.center_.x = session.props.slides[cur].cx;
      viewport.center_.y = session.props.slides[cur].cy;
      viewport.SetZoom(
          std::clamp(session.props.slides[cur].zoom, 0.05f, 32.0f));
      session.dirty = true;
      return;
    }
  }
}

void App::HandleMouseMotion(SDL_Event& event) {
  mouse_x = event.motion.x;
  mouse_y = event.motion.y;

  if (input.IsInMainMenu()) {
    // Hover highlight for the documents grid.
    const int menu_rows = input.GetMainMenuRowCount();
    const MainMenuLayout menu_layout = ComputeMainMenuLayout(
        window_width, window_height, menu_rows, input.GetMainMenuSelected());
    input.SetMainMenuCols(menu_layout.cols);
    const int menu_hit = MainMenuHitRow(menu_layout, mouse_x, mouse_y);
    if (menu_hit >= 0) input.SetMainMenuSelected(menu_hit);
  } else {
    UpdateToolMenuHover();
    RecordStrokeMotion(event);
    RouteGestureMotion(event);
    UpdateNodeMotion(event);
    DragGizmo(event);
    input.SetPaletteHovered(IsPointInPaletteBounds(
        mouse_x, mouse_y, input, color_manager, window_width, window_height));
    if (ruler.measuring) {
      ruler.current = viewport.ScreenToCanvas(event.motion.x, event.motion.y);
    }
    // Update current cursor position for ruler guides.
    if (input.GetMode() == Mode::kRuler) {
      ruler.current = viewport.ScreenToCanvas(event.motion.x, event.motion.y);
    }
  }
}

void App::UpdateToolMenuHover() {
  if (input.IsToolMenuOpen()) {
    const Tool menu_tool = input.GetToolMenuTool();
    const auto menu_rows =
        BuildToolMenuRows(menu_tool, input.GetToolSettings(menu_tool));
    const ToolMenuLayout menu_layout = ComputeToolMenuLayout(
        window_width, window_height, static_cast<int>(menu_rows.size()));
    if (input.IsToolMenuDragging()) {
      const float nt =
          ToolMenuSliderT(menu_layout, input.GetToolMenuDragRow(), mouse_x);
      ApplyToolMenuRowT(input, menu_tool, input.GetToolMenuDragRow(), nt);
    } else {
      const int hover = ToolMenuHitRow(menu_layout, mouse_x, mouse_y);
      if (hover >= 0 && menu_rows[hover].adjustable) {
        input.SetToolMenuRow(hover);
      }
    }
  }
}

void App::RecordStrokeMotion(SDL_Event& event) {
  if (stroke.is_drawing && !input.IsToolMenuOpen()) {
    const Vec2 p =
        ClampToPage(viewport.ScreenToCanvas(event.motion.x, event.motion.y));
    stroke.points.push_back(p);
  }
  if (shape.is_active) {
    shape.current =
        ClampToPage(viewport.ScreenToCanvas(event.motion.x, event.motion.y));
  }
  if (select_rect.is_active) {
    select_rect.current =
        viewport.ScreenToCanvas(event.motion.x, event.motion.y);
  }
  if (lasso_active && input.GetMode() == Mode::kSelect &&
      input.GetTool() == Tool::kLasso) {
    const Vec2 lp = viewport.ScreenToCanvas(event.motion.x, event.motion.y);
    if (lasso_points.empty() ||
        std::hypot(lp.x - lasso_points.back().x, lp.y - lasso_points.back().y) >
            1.5f) {
      lasso_points.push_back(lp);
    }
    select_rect.current = lp;
  }
}

void App::RouteGestureMotion(SDL_Event& event) {
  // Krita-style gestures take priority over painting/dragging:
  // Shift+drag resizes the tool, space-pan moves the canvas, Ctrl+drag
  // zooms smoothly. Pan/zoom only follow the device that started them
  // (absolute anchor mappings stay idempotent when pen events arrive
  // alongside emulated mouse events).
  if (gestures.size_active) {
    const float delta =
        gestures.ConsumeSizeDelta(event.motion.x, viewport.zoom_);
    if (delta != 0.0f) {
      input.SetToolSize(input.GetToolSize() + delta);
    }
  } else if (gestures.pan_active && gestures.pan_source == 1) {
    gestures.ApplyPan(viewport, event.motion.x, event.motion.y);
  } else if (gestures.zoom_active && gestures.zoom_source == 1) {
    gestures.ApplyZoomDrag(viewport, event.motion.x, event.motion.y,
                           window_width, window_height);
  }
}

void App::UpdateNodeMotion(SDL_Event& event) {
  if (node_editor.editing && input.GetMode() == Mode::kNode) {
    const Vec2 p = viewport.ScreenToCanvas(event.motion.x, event.motion.y);
    if (node_editor.dragging) {
      MoveNode(node_editor.path, node_editor.selected_node,
               node_editor.selected_type, p.x, p.y);
    } else {
      int idx = -1;
      NodeHitType type = NodeHitType::kNone;
      const float tol = 10.0f / viewport.zoom_;
      if (HitTestNodes(node_editor.path, p.x, p.y, tol, &idx, &type)) {
        node_editor.hovered_node = idx;
        node_editor.hovered_type = type;
      } else {
        node_editor.hovered_node = -1;
        node_editor.hovered_type = NodeHitType::kNone;
      }
    }
  }
}

void App::DragGizmo(SDL_Event& event) {
  if (input.GetMode() != Mode::kTransform || !gizmo.active) {
    return;
  }
  const Vec2 p = viewport.ScreenToCanvas(event.motion.x, event.motion.y);
  auto& sel = layer_stack.GetSelection();
  Layer* active =
      layer_stack.GetLayers()[layer_stack.GetActiveLayerIndex()].get();

  // If not yet dragging but left button is held, start move drag once
  // the cursor moves beyond a small threshold.
  if (!gizmo.dragging && gizmo.handle == -1 &&
      (event.motion.state & SDL_BUTTON_LMASK)) {
    float dist =
        std::hypot(p.x - gizmo.start_mouse.x, p.y - gizmo.start_mouse.y);
    if (dist > 3.0f / viewport.zoom_) {
      gizmo.dragging = true;
    }
  }

  if (!gizmo.dragging) {
    return;  // Nothing to do until the drag threshold is crossed.
  }
  if (gizmo.handle == -1) {
    MoveGizmoSelection(p, sel, active);
  } else if (gizmo.handle == 4) {
    RotateGizmoSelection(p, sel, active);
  } else {
    ScaleGizmoSelection(p, sel, active);
  }
}

void App::MoveGizmoSelection(const Vec2& p, Selection& sel, Layer* active) {
  // Move. Raster selections float: only the lifted pixels move.
  float dx = p.x - gizmo.start_mouse.x;
  float dy = p.y - gizmo.start_mouse.y;
  if (active != nullptr && active->type == LayerType::kRaster) {
    auto* raster = static_cast<RasterLayer*>(active);
    if (!gizmo.raster_float.active) {
      RasterFloatBegin(raster, sel.canvas_rect, &gizmo.raster_float);
      gizmo.float_layer = layer_stack.GetActiveLayerIndex();
    }
    RasterFloatMove(&gizmo.raster_float, dx, dy);
    sel.canvas_rect.offset(dx, dy);
  } else {
    TransformSelectionMove(sel, active, dx, dy);
  }
  session.dirty = true;
  gizmo.start_mouse = p;
}

void App::RotateGizmoSelection(const Vec2& p, Selection& sel, Layer* active) {
  // Rotate. Raster drags re-render the pristine lifted pixels by
  // the total angle from press so quality doesn't compound; vector
  // drags stay incremental (exact affine math).
  const bool is_raster =
      active != nullptr && active->type == LayerType::kRaster;
  const Vec2& ref = is_raster ? gizmo.drag_press : gizmo.start_mouse;
  const float cx = gizmo.start_center.x;
  const float cy = gizmo.start_center.y;
  const float deg =
      (std::atan2(p.y - cy, p.x - cx) - std::atan2(ref.y - cy, ref.x - cx)) *
      180.0f / 3.14159265f;
  if (is_raster) {
    auto* raster = static_cast<RasterLayer*>(active);
    if (!gizmo.raster_float.active) {
      RasterFloatBegin(raster, sel.canvas_rect, &gizmo.raster_float);
      gizmo.float_layer = layer_stack.GetActiveLayerIndex();
    }
    RasterFloatRotateAbs(&gizmo.raster_float, cx, cy, deg);
    sel.canvas_rect =
        Rect::MakeXYWH(gizmo.raster_float.x, gizmo.raster_float.y,
                       static_cast<float>(gizmo.raster_float.view.width()),
                       static_cast<float>(gizmo.raster_float.view.height()));
  } else {
    TransformSelectionRotate(sel, active, cx, cy, deg);
  }
  session.dirty = true;
  gizmo.start_mouse = p;
}

void App::ScaleGizmoSelection(const Vec2& p, Selection& sel, Layer* active) {
  // Scale about the pivot pinned at press time, measured as totals
  // from the press pointer. Raster drags re-render the pristine
  // lifted pixels so error can't compound; the anchored corner
  // stays exactly put (no drift) and scaling never translates the
  // selection.
  const float ox = gizmo.scale_pivot.x;
  const float oy = gizmo.scale_pivot.y;
  auto total_factor = [](float pos, float press, float pivot) {
    const float den = press - pivot;
    if (std::fabs(den) < 1e-6f) return 1.0f;
    float s = (pos - pivot) / den;
    const float a = std::fabs(s);
    if (a > 32.0f) s = (s < 0.0f ? -32.0f : 32.0f);
    return s;
  };
  const float sx = total_factor(p.x, gizmo.drag_press.x, ox);
  const float sy = total_factor(p.y, gizmo.drag_press.y, oy);
  if (active != nullptr && active->type == LayerType::kRaster) {
    auto* raster = static_cast<RasterLayer*>(active);
    if (!gizmo.raster_float.active) {
      RasterFloatBegin(raster, sel.canvas_rect, &gizmo.raster_float);
      gizmo.float_layer = layer_stack.GetActiveLayerIndex();
    }
    RasterFloatScaleAbs(&gizmo.raster_float, ox, oy, sx, sy);
    sel.canvas_rect =
        Rect::MakeXYWH(gizmo.raster_float.x, gizmo.raster_float.y,
                       static_cast<float>(gizmo.raster_float.view.width()),
                       static_cast<float>(gizmo.raster_float.view.height()));
  } else {
    TransformSelectionScale(sel, active, ox, oy, sx, sy);
  }
  session.dirty = true;
  gizmo.start_mouse = p;
}

void App::HandleMouseWheel(SDL_Event& event) {
  if (input.IsInMainMenu()) {
    input.MoveMainMenuSelection((event.wheel.y > 0) ? -1 : 1);
  } else if (input.IsToolMenuOpen()) {
    const int dir = (event.wheel.y > 0) ? 1 : -1;
    for (int i = 0; i < std::abs((int)event.wheel.y); ++i) {
      input.AdjustToolMenuRow(dir);
    }
  } else {
    const bool ctrl_held = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
    if (ctrl_held) {
      float size = input.GetToolSize();
      size += (event.wheel.y > 0) ? 1.0f : -1.0f;
      size = std::clamp(size, 1.0f, 200.0f);
      input.SetToolSize(size);

    } else {
      const float zoom_factor = (event.wheel.y > 0) ? 1.2f : (1.0f / 1.2f);
      viewport.ZoomAt(mouse_x, mouse_y, zoom_factor);
    }
  }
}

void App::HandleMouseButtonDown(SDL_Event& event) {
  if (event.button.button == SDL_BUTTON_LEFT) {
    const bool ui_consumed = HandleMouseClick(
        event.button.x, event.button.y, window_width, window_height, input,
        layer_stack, color_manager, ui_typeface);
    if (!ui_consumed && input.GetMode() != Mode::kTransform) {
      // Any new canvas gesture anchors a floating raster selection:
      // further drags move only the lifted pixels until a new area is
      // selected. Transform presses keep the float alive.
      CommitFloat();
    }
    // Bounded canvas: drawing gestures can't start off limits. (Motion
    // points are clamped below; raster commits are scrubbed after, so dab
    // radius and flood spread stay inside too.)
    const Vec2 press_canvas =
        viewport.ScreenToCanvas(event.button.x, event.button.y);
    const Rect press_page = BoundedPage();
    const bool press_off_limits =
        !press_page.isEmpty() &&
        !press_page.contains(press_canvas.x, press_canvas.y);
    // Krita-style Shift+drag resizes the tool instead of painting
    // (draw/erase modes only; shape tools keep Shift-constrain).
    const bool shift_held = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
    if (!ui_consumed && !input.IsInMainMenu() && shift_held &&
        (input.GetMode() == Mode::kDraw || input.GetMode() == Mode::kErase)) {
      gestures.StartSize(event.button.x, event.button.y);
    }
    const PressGate press{ui_consumed, press_off_limits};
    HandleDrawPress(event, press);
    HandleShapePress(event, press);
    HandleErasePress(event, press);
    HandleSelectPress(event, press);
    HandleNodePress(event, press);
    HandleFillPress(event, press);
    HandleTransformPress(event, press);
    HandleTextPress(event, press);
    HandleRulerPress(event, press);
  } else if (event.button.button == SDL_BUTTON_MIDDLE &&
             !input.IsInMainMenu()) {
    // Ctrl + middle drag zooms smoothly, plain middle drag pans.
    const bool ctrl = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
    if (ctrl) {
      gestures.StartZoomDrag(event.button.x, event.button.y, 1, viewport);
    } else {
      gestures.StartPan(event.button.x, event.button.y, 1, viewport.center_);
    }
  }
}

void App::HandleDrawPress(const SDL_Event& event, const PressGate& press) {
  if (!press.ui_consumed && !press.off_limits && !gestures.size_active &&
      input.GetMode() == Mode::kDraw) {
    stroke.is_drawing = true;
    stroke.points.clear();
    const Vec2 p =
        ClampToPage(viewport.ScreenToCanvas(event.button.x, event.button.y));
    stroke.points.push_back(p);

    stroke.start_time_ms = frame_now;
    stroke.seed = static_cast<uint32_t>(frame_now & 0xFFFFFFFFu);
  }
}

void App::HandleShapePress(const SDL_Event& event, const PressGate& press) {
  if (!press.ui_consumed && !press.off_limits &&
      input.GetMode() == Mode::kShape) {
    shape.is_active = true;
    shape.anchor =
        ClampToPage(viewport.ScreenToCanvas(event.button.x, event.button.y));
    shape.current = shape.anchor;
    shape.tool = input.GetTool();
  }
}

void App::HandleErasePress(const SDL_Event& event, const PressGate& press) {
  if (!press.ui_consumed && !gestures.size_active &&
      input.GetMode() == Mode::kErase) {
    if (input.GetTool() == Tool::kStroke) {
      stroke.is_drawing = true;
      stroke.points.clear();
      stroke.points.push_back(
          viewport.ScreenToCanvas(event.button.x, event.button.y));
      stroke.seed = static_cast<uint32_t>(frame_now & 0xFFFFFFFFu);
    } else if (input.GetTool() == Tool::kArea) {
      select_rect.is_active = true;
      select_rect.anchor =
          viewport.ScreenToCanvas(event.button.x, event.button.y);
      select_rect.current = select_rect.anchor;
    } else if (input.GetTool() == Tool::kPoint) {
      const Vec2 pp = viewport.ScreenToCanvas(event.button.x, event.button.y);
      Layer* active =
          layer_stack.GetLayers()[layer_stack.GetActiveLayerIndex()].get();
      if (active != nullptr && active->type == LayerType::kRaster) {
        EraseRasterPoint(static_cast<RasterLayer*>(active), pp.x, pp.y,
                         input.GetToolSize());
      } else if (active != nullptr && active->type == LayerType::kVector) {
        EraseVectorPoint(static_cast<VectorLayer*>(active), pp.x, pp.y,
                         input.GetToolSize() * 0.5f);
        session.dirty = true;
      }
    }
  }
}

void App::HandleSelectPress(const SDL_Event& event, const PressGate& press) {
  if (!press.ui_consumed && input.GetMode() == Mode::kSelect) {
    const Vec2 p = viewport.ScreenToCanvas(event.button.x, event.button.y);
    Layer* active =
        layer_stack.GetLayers()[layer_stack.GetActiveLayerIndex()].get();
    if (input.GetTool() == Tool::kAll) {
      SelectAllOnLayer(active, layer_stack.GetSelection());
    } else if (input.GetTool() == Tool::kMagic) {
      // Click-based: contiguous region on raster, top-most object on
      // vector.
      if (active != nullptr && active->type == LayerType::kRaster) {
        Rect region;
        const int tol =
            static_cast<int>(input.GetToolSettings(input.GetTool()).tolerance);
        if (FloodRegionBounds(static_cast<RasterLayer*>(active), p.x, p.y, tol,
                              &region)) {
          layer_stack.GetSelection().active = true;
          layer_stack.GetSelection().canvas_rect = region;
        } else {
          layer_stack.GetSelection().Clear();
        }
      } else if (active != nullptr && active->type == LayerType::kVector) {
        auto* vector = static_cast<VectorLayer*>(active);
        layer_stack.GetSelection().Clear();
        for (int i = static_cast<int>(vector->objects.size()) - 1; i >= 0;
             --i) {
          if (vector->objects[i]->HitTest(p.x, p.y, 4.0f)) {
            layer_stack.GetSelection().active = true;
            layer_stack.GetSelection().object_indices.push_back(i);
            layer_stack.GetSelection().canvas_rect =
                vector->objects[i]->GetBounds();
            break;
          }
        }
      }
    } else {
      // kSingle: press starts a gesture; release decides click (point
      // select) vs drag (marquee). kLasso: press starts a freehand
      // polygon; release selects.
      select_rect.is_active = true;
      select_rect.anchor = p;
      select_rect.current = p;
      if (input.GetTool() == Tool::kLasso) {
        lasso_active = true;
        lasso_points.clear();
        lasso_points.push_back(p);
      } else {
        lasso_active = false;
        lasso_points.clear();
      }
    }
  }
}

void App::HandleNodePress(const SDL_Event& event, const PressGate& press) {
  if (press.ui_consumed || input.GetMode() != Mode::kNode) {
    return;
  }
  const Vec2 p = viewport.ScreenToCanvas(event.button.x, event.button.y);
  Layer* active =
      layer_stack.GetLayers()[layer_stack.GetActiveLayerIndex()].get();
  if (active->type != LayerType::kVector) {
    return;
  }
  auto* vector = static_cast<VectorLayer*>(active);
  if (!node_editor.editing) {
    PickNodePath(p, vector);
    return;
  }
  int idx = -1;
  NodeHitType type = NodeHitType::kNone;
  const float tol = 10.0f / viewport.zoom_;
  if (HitTestNodes(node_editor.path, p.x, p.y, tol, &idx, &type)) {
    node_editor.selected_node = idx;
    node_editor.selected_type = type;
    node_editor.dragging = true;
    node_editor.drag_start_canvas = Vec2{p.x, p.y};
    ApplyNodeTool(vector, idx);
  } else {
    EditToolNodeClick(p, vector);
  }
}

void App::PickNodePath(const Vec2& p, VectorLayer* vector) {
  for (int i = static_cast<int>(vector->objects.size()) - 1; i >= 0; --i) {
    if (vector->objects[i]->HitTest(p.x, p.y, 6.0f)) {
      if (auto* path_obj =
              dynamic_cast<PathObject*>(vector->objects[i].get())) {
        node_editor.BeginEdit(path_obj, i);
        break;
      }
      if (auto* brush_obj =
              dynamic_cast<BrushObject*>(vector->objects[i].get())) {
        // Convert brush stroke to an editable PathObject.
        auto path_obj = std::make_unique<PathObject>();
        bool first = true;
        for (const auto& pt : brush_obj->points) {
          if (first) {
            path_obj->path.moveTo(pt.x, pt.y);
            first = false;
          } else {
            path_obj->path.lineTo(pt.x, pt.y);
          }
        }
        path_obj->stroke_color = brush_obj->color;
        path_obj->fill_color = kColorTransparent;
        path_obj->stroke_width = brush_obj->width;
        vector->objects[i] = std::move(path_obj);
        node_editor.BeginEdit(
            static_cast<PathObject*>(vector->objects[i].get()), i);
        break;
      }
    }
  }
}

void App::ApplyNodeTool(VectorLayer* vector, int idx) {
  if (input.GetTool() == Tool::kSmooth) {
    SetNodeSmooth(node_editor.path, idx, true);
    node_editor.Commit(vector);
    session.dirty = true;
  } else if (input.GetTool() == Tool::kCorner) {
    SetNodeSmooth(node_editor.path, idx, false);
    node_editor.Commit(vector);
    session.dirty = true;
  } else if (input.GetTool() == Tool::kBreak) {
    auto second = BreakPathAtNode(node_editor.path, idx);
    if (second != nullptr) {
      // Split into two PathObjects.
      auto new_obj = std::make_unique<PathObject>();
      new_obj->path = second->ToPath();
      new_obj->stroke_color =
          vector->objects[node_editor.path_object_index]->stroke_color;
      new_obj->fill_color =
          vector->objects[node_editor.path_object_index]->fill_color;
      new_obj->stroke_width =
          vector->objects[node_editor.path_object_index]->stroke_width;
      if (auto* edited_obj = dynamic_cast<PathObject*>(
              vector->objects[node_editor.path_object_index].get())) {
        edited_obj->path = node_editor.path.ToPath();
      }
      vector->objects.insert(
          vector->objects.begin() + node_editor.path_object_index + 1,
          std::move(new_obj));
      node_editor.Clear();
    } else {
      // Just opened a closed path.
      node_editor.Commit(vector);
      session.dirty = true;
    }
  } else if (input.GetTool() == Tool::kJoin) {
    if (!node_editor.join_pending) {
      // Must be an endpoint.
      bool is_endpoint =
          (idx == 0) ||
          (idx == static_cast<int>(node_editor.path.nodes.size()) - 1);
      if (is_endpoint && !node_editor.path.closed) {
        node_editor.join_pending = true;
        node_editor.join_source_object = node_editor.path_object_index;
        node_editor.join_source_node = idx;
      }
    } else {
      bool is_endpoint =
          (idx == 0) ||
          (idx == static_cast<int>(node_editor.path.nodes.size()) - 1);
      if (is_endpoint && !node_editor.path.closed &&
          node_editor.join_source_object >= 0) {
        EditablePath merged =
            JoinPaths(node_editor.path, node_editor.join_source_node,
                      node_editor.path, idx);
        if (auto* edited_obj = dynamic_cast<PathObject*>(
                vector->objects[node_editor.path_object_index].get())) {
          edited_obj->path = merged.ToPath();
        }
        node_editor.Clear();
      }
    }
  }
}

void App::EditToolNodeClick(const Vec2& p, VectorLayer* vector) {
  if (input.GetTool() == Tool::kEdit) {
    // Click on empty space: try to add a node on the nearest
    // segment.
    int seg = FindNearestSegment(node_editor.path, p.x, p.y);
    if (seg >= 0) {
      InsertNodeAtSegment(node_editor.path, seg);
      node_editor.Commit(vector);
      session.dirty = true;
    } else {
      // Clicked away from path: deselect.
      node_editor.selected_node = -1;
      node_editor.selected_type = NodeHitType::kNone;
    }
  }
}

void App::HandleFillPress(const SDL_Event& event, const PressGate& press) {
  if (!press.ui_consumed && !press.off_limits &&
      input.GetMode() == Mode::kFill) {
    const Vec2 p =
        ClampToPage(viewport.ScreenToCanvas(event.button.x, event.button.y));
    Layer* active =
        layer_stack.GetLayers()[layer_stack.GetActiveLayerIndex()].get();
    if (active->type == LayerType::kRaster) {
      auto* raster = static_cast<RasterLayer*>(active);
      if (input.GetTool() == Tool::kGradient ||
          input.GetTool() == Tool::kPattern) {
        // Fill the selection if one is active, otherwise a square
        // around the click sized by the tool.
        Rect region;
        if (layer_stack.GetSelection().active) {
          region = layer_stack.GetSelection().canvas_rect;
        } else {
          const float half = input.GetToolSize() * 4.0f;
          region =
              Rect::MakeLTRB(p.x - half, p.y - half, p.x + half, p.y + half);
        }
        if (input.GetTool() == Tool::kGradient) {
          Color to = color_manager.GetFillColor();
          if (GetA(to) == 0) {
            to = kColorTransparent;
          }
          GradientFillRaster(raster, region, color_manager.GetCurrentColor(),
                             to,
                             /*horizontal=*/true);
          ConstrainRaster(raster);
          session.dirty = true;
        } else {
          PatternFillRaster(raster, region, color_manager.GetCurrentColor());
          ConstrainRaster(raster);
          session.dirty = true;
        }
      } else {
        FloodFillRaster(
            raster, p.x, p.y, color_manager.GetCurrentColor(),
            static_cast<int>(input.GetToolSettings(input.GetTool()).tolerance));
        ConstrainRaster(raster);
        session.dirty = true;
      }
    } else if (active->type == LayerType::kVector) {
      auto* vector = static_cast<VectorLayer*>(active);
      for (int i = static_cast<int>(vector->objects.size()) - 1; i >= 0; --i) {
        if (vector->objects[i]->HitTest(p.x, p.y, 4.0f)) {
          // Use current color for fill, not the separate fill_color.
          FillVectorObject(vector->objects[i], color_manager.GetCurrentColor());
          session.dirty = true;
          break;
        }
      }
    }
  }
}

void App::HandleTransformPress(const SDL_Event& event, const PressGate& press) {
  if (!press.ui_consumed && input.GetMode() == Mode::kTransform) {
    const Vec2 p = viewport.ScreenToCanvas(event.button.x, event.button.y);
    auto& sel = layer_stack.GetSelection();
    Layer* active =
        layer_stack.GetLayers()[layer_stack.GetActiveLayerIndex()].get();

    // Handles sit on/outside the bbox edge (outside what Rect::contains
    // reports), so hit-test them before deciding a click missed the
    // selection.
    int grabbed = -1;
    if (sel.active) {
      grabbed = HitGizmoHandle(sel.canvas_rect, p, viewport.zoom_);
      if (grabbed < 0 && !sel.canvas_rect.contains(p.x, p.y)) {
        CommitFloat();  // anchor any
                        // floating pixels first
        sel.Clear();
      }
    }

    // Auto-select object under cursor if nothing is selected.
    if (!sel.active && active != nullptr &&
        active->type == LayerType::kVector) {
      auto* vector = static_cast<VectorLayer*>(active);
      for (int i = static_cast<int>(vector->objects.size()) - 1; i >= 0; --i) {
        if (vector->objects[i]->HitTest(p.x, p.y, 6.0f)) {
          sel.active = true;
          sel.object_indices = {i};
          sel.canvas_rect = vector->objects[i]->GetBounds();
          break;
        }
      }
    }

    if (sel.active) {
      if (grabbed < 0) {
        // Selection was just created above (or press missed the old one
        // without leaving it): test against the new bbox.
        grabbed = HitGizmoHandle(sel.canvas_rect, p, viewport.zoom_);
      }
      const Rect& bbox = sel.canvas_rect;
      gizmo.start_mouse = p;
      gizmo.start_center = Vec2{bbox.centerX(), bbox.centerY()};
      if (grabbed == 4) {
        gizmo.active = true;
        gizmo.dragging = true;
        gizmo.handle = 4;
        gizmo.drag_press = p;
      } else if (grabbed >= 0) {
        // Pin the scale pivot (opposite corner) now; the scale drag
        // measures against it and the last pointer pos.
        const Vec2 cs[4] = {
            {bbox.left, bbox.top},
            {bbox.right, bbox.top},
            {bbox.right, bbox.bottom},
            {bbox.left, bbox.bottom},
        };
        gizmo.scale_pivot = cs[(grabbed + 2) % 4];
        gizmo.drag_press = p;
        gizmo.active = true;
        gizmo.dragging = true;
        gizmo.handle = grabbed;
      } else if (bbox.contains(p.x, p.y)) {
        // Clicked inside bbox: start move on first mouse motion.
        gizmo.active = true;
        gizmo.dragging = false;
        gizmo.handle = -1;
      }
    }
  }
}

void App::HandleTextPress(const SDL_Event& event, const PressGate& press) {
  if (!press.ui_consumed && !press.off_limits &&
      input.GetMode() == Mode::kText) {
    const Vec2 p =
        ClampToPage(viewport.ScreenToCanvas(event.button.x, event.button.y));
    Layer* active =
        layer_stack.GetLayers()[layer_stack.GetActiveLayerIndex()].get();
    if (active != nullptr) {
      // Commit any active text editor first.
      if (text_editor.active) {
        CommitEditorText(active, text_editor.text, text_editor.x, text_editor.y,
                         text_editor.font_size, text_editor.object_index,
                         color_manager.GetCurrentColor(), ui_typeface,
                         BoundedPage());
        session.dirty = true;
      }

      if (active->type == LayerType::kVector) {
        auto* vector = static_cast<VectorLayer*>(active);
        // Check if clicking on existing text object.
        int hit_index = -1;
        for (int i = static_cast<int>(vector->objects.size()) - 1; i >= 0;
             --i) {
          if (vector->objects[i]->HitTest(p.x, p.y, 6.0f)) {
            if (dynamic_cast<TextObject*>(vector->objects[i].get()) !=
                nullptr) {
              hit_index = i;
              break;
            }
          }
        }
        if (hit_index >= 0) {
          auto* text_obj =
              static_cast<TextObject*>(vector->objects[hit_index].get());
          text_editor.active = true;
          text_editor.x = text_obj->x;
          text_editor.y = text_obj->y;
          text_editor.text = text_obj->text;
          text_editor.original_text = text_obj->text;
          text_editor.font_size = text_obj->font_size;
          text_editor.object_index = hit_index;
          text_editor.blink_timer = 0;
          text_editor.cursor_visible = true;
          text_editor.typeface = ui_typeface;
          SDL_StartTextInput(window);
          return;
        }
      }
      // Start new text (vector or raster).
      text_editor.active = true;
      text_editor.x = p.x;
      text_editor.y = p.y;
      text_editor.text.clear();
      text_editor.original_text.clear();
      text_editor.font_size = input.GetToolSettings(input.GetTool()).font_size;
      text_editor.object_index = -1;
      text_editor.blink_timer = 0;
      text_editor.cursor_visible = true;
      text_editor.typeface = ui_typeface;
      SDL_StartTextInput(window);
    }
  }
}

void App::HandleRulerPress(const SDL_Event& event, const PressGate& press) {
  if (!press.ui_consumed && input.GetMode() == Mode::kRuler) {
    const Vec2 p = viewport.ScreenToCanvas(event.button.x, event.button.y);
    if (input.GetTool() == Tool::kDistance) {
      ruler.measuring = true;
      ruler.start = p;
      ruler.current = p;
    } else if (input.GetTool() == Tool::kAngle) {
      if (ruler.angle_stage == 0) {
        ruler.angle_p0 = p;
        ruler.angle_stage = 1;
      } else if (ruler.angle_stage == 1) {
        ruler.angle_p1 = p;
        ruler.angle_stage = 2;
      } else if (ruler.angle_stage == 2) {
        ruler.angle_p2 = p;
        ruler.angle_stage = 3;
      } else {
        ruler.angle_stage = 0;
      }
    }
  }
}

void App::HandleMouseButtonUp(SDL_Event& event) {
  if (event.button.button == SDL_BUTTON_LEFT) {
    HandleLeftRelease();
  } else if (event.button.button == SDL_BUTTON_MIDDLE) {
    if (gestures.pan_source == 1) {
      gestures.pan_active = false;
      gestures.pan_source = 0;
    }
    if (gestures.zoom_source == 1) {
      gestures.zoom_active = false;
      gestures.zoom_source = 0;
    }
  }
}

void App::HandleLeftRelease() {
  input.SetToolMenuDragging(-1);
  gestures.EndSize(window);
  CommitStrokeRelease();
  CommitShapeRelease();
  CommitSelectionRelease();
  CommitNodeRelease();
  if (gizmo.dragging) {
    gizmo.dragging = false;
    gizmo.handle = -1;
    gizmo.active = false;
  }
  // No commit here: the float stays pending so the next drag moves the
  // same lifted pixels. It anchors on the next gesture, command, layer
  // switch, or document operation.
  if (ruler.measuring) {
    ruler.measuring = false;
  }
}

void App::CommitStrokeRelease() {
  if (stroke.is_drawing) {
    stroke.is_drawing = false;
    session.dirty = true;

    Layer* active =
        layer_stack.GetLayers()[layer_stack.GetActiveLayerIndex()].get();
    if (input.GetMode() == Mode::kDraw) {
      if (active->type == LayerType::kRaster) {
        auto* raster = static_cast<RasterLayer*>(active);
        const DabParams dab_params = ResolveToolParams(
            input.GetTool(), input.GetToolSettings(input.GetTool()));
        CommitRasterStroke(raster, stroke.points,
                           color_manager.GetCurrentColor(), dab_params,
                           stroke.seed);
        ConstrainRaster(raster);
      } else if (active->type == LayerType::kVector) {
        auto* vector = static_cast<VectorLayer*>(active);
        CommitBrushStroke(vector, stroke.points,
                          color_manager.GetCurrentColor(), input.GetToolSize());
      }
    } else if (input.GetMode() == Mode::kErase) {
      if (active->type == LayerType::kRaster) {
        auto* raster = static_cast<RasterLayer*>(active);
        const DabParams erase_params = ResolveToolParams(
            input.GetTool(), input.GetToolSettings(input.GetTool()));
        EraseStrokeDabs(raster, stroke.points, erase_params.radius,
                        erase_params.hardness, erase_params.spacing,
                        stroke.seed);
      } else if (active->type == LayerType::kVector) {
        auto* vector = static_cast<VectorLayer*>(active);
        EraseVectorStroke(vector, stroke.points);
      }
    }
    stroke.points.clear();
  }
}

void App::CommitShapeRelease() {
  if (shape.is_active) {
    shape.is_active = false;
    const bool shift = SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_LSHIFT] ||
                       SDL_GetKeyboardState(nullptr)[SDL_SCANCODE_RSHIFT];
    Path path = BuildShapePath(shape.tool, shape.anchor.x, shape.anchor.y,
                               shape.current.x, shape.current.y, shift);
    if (!path.isEmpty()) {
      session.dirty = true;
      Layer* active =
          layer_stack.GetLayers()[layer_stack.GetActiveLayerIndex()].get();
      const float shape_width =
          input.GetToolSettings(input.GetTool()).stroke_width;
      if (active->type == LayerType::kRaster) {
        auto* raster = static_cast<RasterLayer*>(active);
        CommitShapeRaster(raster, path, color_manager.GetStrokeColor(),
                          color_manager.GetFillColor(), shape_width);
        ConstrainRaster(raster);
      } else if (active->type == LayerType::kVector) {
        auto* vector = static_cast<VectorLayer*>(active);
        CommitShapeVector(vector, path, color_manager.GetStrokeColor(),
                          color_manager.GetFillColor(), shape_width);
      }
    }
  }
}

void App::CommitSelectionRelease() {
  if (select_rect.is_active) {
    select_rect.is_active = false;
    const float x0 = std::min(select_rect.anchor.x, select_rect.current.x);
    const float y0 = std::min(select_rect.anchor.y, select_rect.current.y);
    const float x1 = std::max(select_rect.anchor.x, select_rect.current.x);
    const float y1 = std::max(select_rect.anchor.y, select_rect.current.y);
    const Rect rect = Rect::MakeLTRB(x0, y0, x1, y1);
    if (!rect.isEmpty()) {
      Layer* active =
          layer_stack.GetLayers()[layer_stack.GetActiveLayerIndex()].get();
      if (input.GetMode() == Mode::kErase) {
        if (active->type == LayerType::kRaster) {
          auto* raster = static_cast<RasterLayer*>(active);
          EraseRasterArea(raster, rect);
          session.dirty = true;
        } else if (active->type == LayerType::kVector) {
          auto* vector = static_cast<VectorLayer*>(active);
          EraseVectorArea(vector, rect);
          session.dirty = true;
        }
      } else if (input.GetMode() == Mode::kSelect) {
        // Click (no drag) with kSingle/kLasso falls back to point
        // select; a drag marquees (kSingle) or lassos (kLasso).
        const float click_tol = 4.0f / viewport.zoom_;
        const bool is_click =
            rect.width() < click_tol && rect.height() < click_tol;
        auto point_select = [&](const Vec2& q) {
          Layer* act =
              layer_stack.GetLayers()[layer_stack.GetActiveLayerIndex()].get();
          if (act == nullptr || act->type != LayerType::kVector) {
            return;
          }
          auto* vector = static_cast<VectorLayer*>(act);
          layer_stack.GetSelection().Clear();
          for (int i = static_cast<int>(vector->objects.size()) - 1; i >= 0;
               --i) {
            if (vector->objects[i]->HitTest(q.x, q.y, 4.0f)) {
              layer_stack.GetSelection().active = true;
              layer_stack.GetSelection().object_indices.push_back(i);
              layer_stack.GetSelection().canvas_rect =
                  vector->objects[i]->GetBounds();
              break;
            }
          }
        };
        if (input.GetTool() == Tool::kLasso && lasso_active) {
          if (is_click || lasso_points.size() < 3) {
            point_select(select_rect.anchor);
          } else if (active->type == LayerType::kRaster) {
            layer_stack.GetSelection().active = true;
            layer_stack.GetSelection().canvas_rect = rect;
          } else if (active->type == LayerType::kVector) {
            auto* vector = static_cast<VectorLayer*>(active);
            SelectVectorInPolygon(vector, lasso_points,
                                  layer_stack.GetSelection());
          }
          lasso_active = false;
          lasso_points.clear();
        } else if (input.GetTool() == Tool::kSingle) {
          if (is_click) {
            point_select(select_rect.anchor);
          } else if (active->type == LayerType::kRaster) {
            layer_stack.GetSelection().active = true;
            layer_stack.GetSelection().canvas_rect = rect;
          } else if (active->type == LayerType::kVector) {
            auto* vector = static_cast<VectorLayer*>(active);
            SelectVectorInRect(vector, rect, layer_stack.GetSelection());
          }
        }
        // kAll/kMagic apply on press; nothing to commit here.
      }
    }
  }
}

void App::CommitNodeRelease() {
  if (node_editor.dragging) {
    node_editor.dragging = false;
    Layer* active =
        layer_stack.GetLayers()[layer_stack.GetActiveLayerIndex()].get();
    if (active->type == LayerType::kVector) {
      auto* vector = static_cast<VectorLayer*>(active);
      node_editor.Commit(vector);
      session.dirty = true;
    }
  }
}

void App::HandlePenEvent(SDL_Event& event) {
  // Wacom pen lower-barrel button: drag pans like the middle mouse button;
  // with Shift held it rotates the whole canvas view instead, with Ctrl
  // held it zooms smoothly instead (vertical drag, anchored at the press
  // point).
  if (event.type == SDL_EVENT_PEN_BUTTON_DOWN &&
      event.pbutton.button == kPenLowerBarrel && !input.IsInMainMenu()) {
    const bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
    const bool ctrl = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
    if (shift) {
      gestures.StartRotate(event.pbutton.x, event.pbutton.y, window_width,
                           window_height, viewport.angle_);
    } else if (ctrl) {
      gestures.StartZoomDrag(event.pbutton.x, event.pbutton.y, 2, viewport);
    } else {
      gestures.StartPan(event.pbutton.x, event.pbutton.y, 2, viewport.center_);
    }
  }
  if (event.type == SDL_EVENT_PEN_BUTTON_UP &&
      event.pbutton.button == kPenLowerBarrel) {
    if (gestures.pan_source == 2) {
      gestures.pan_active = false;
      gestures.pan_source = 0;
    }
    if (gestures.zoom_source == 2) {
      gestures.zoom_active = false;
      gestures.zoom_source = 0;
    }
    gestures.rot_active = false;
  }
  // Pen motion drives active pen-started pan/zoom/rotate drags. Pan and
  // zoom use an absolute mapping from the press anchor, so any emulated
  // mouse motion arriving alongside is idempotent; rotation is absolute too
  // (cursor angle relative to the press angle).
  if (event.type == SDL_EVENT_PEN_MOTION) {
    mouse_x = event.pmotion.x;
    mouse_y = event.pmotion.y;
    if (!input.IsInMainMenu()) {
      if (gestures.pan_active && gestures.pan_source == 2) {
        gestures.ApplyPan(viewport, event.pmotion.x, event.pmotion.y);
      } else if (gestures.zoom_active && gestures.zoom_source == 2) {
        gestures.ApplyZoomDrag(viewport, event.pmotion.x, event.pmotion.y,
                               window_width, window_height);
      } else if (gestures.rot_active) {
        gestures.ApplyRotate(viewport, event.pmotion.x, event.pmotion.y,
                             window_width, window_height);
      }
    }
  }
}

void App::HandleWindowEvent(SDL_Event& event) {
  // Window backing size changes come through RESIZED (logical size, which
  // drives layout, viewport, and mouse mapping). The targets are recreated
  // defensively inside; zero-size configures are ignored.
  if (event.type == SDL_EVENT_WINDOW_RESIZED) {
    ResizeRenderTargets(event.window.data1, event.window.data2);
  }
  // Losing focus mid-gesture must not leave a drag latched: cancel
  // everything (also ends any tool-size drag).
  if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
    CancelGestures();
  }
}

}  // namespace nanote
