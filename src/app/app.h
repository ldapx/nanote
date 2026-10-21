#pragma once

#include <SDL3/SDL.h>

#include <memory>
#include <string>
#include <vector>

#include "app/gestures.h"
#include "app/sdl_host.h"
#include "canvas/viewport.h"
#include "color/color_manager.h"
#include "command/command_executor.h"
#include "core/bitmap.h"
#include "core/font.h"
#include "core/geometry.h"
#include "doc/document.h"
#include "drawing/drawing.h"
#include "drawing/node_editor.h"
#include "input/input_manager.h"
#include "layer/layer.h"

namespace nanote {

constexpr int kWindowWidth = 800;
constexpr int kWindowHeight = 600;

// Transform gizmo state.
struct TransformGizmoState {
  bool active = false;
  bool dragging = false;
  int handle = -1;  // 0-3 corners, 4 rotation, -1 move
  Vec2 start_mouse;
  Vec2 start_center;
  // Scale pivot (bbox corner opposite the grabbed handle), pinned at press
  // time so the anchored corner can't drift mid-drag.
  Vec2 scale_pivot;
  // Pointer position at press time; scale/rotate totals are measured against
  // it so raster drags can re-render from pristine pixels.
  Vec2 drag_press;
  // Floating raster selection: pixels lifted from this layer for the current
  // drag (stays put until drop). Vector layers transform in place and don't
  // use this.
  RasterFloat raster_float;
  int float_layer = -1;
};

// Text editor state.
struct TextEditorState {
  bool active = false;
  float x = 0.0f;
  float y = 0.0f;
  std::string text;
  float font_size = 16.0f;
  int object_index = -1;  // -1 = new, >=0 = editing existing
  std::string original_text;
  float blink_timer = 0.0f;
  bool cursor_visible = true;
  std::shared_ptr<Typeface> typeface;
};

// Ruler measurement state.
struct RulerState {
  bool measuring = false;
  Vec2 start;
  Vec2 current;
  int angle_stage = 0;  // 0=idle, 1=p0 set, 2=p1 set
  Vec2 angle_p0;
  Vec2 angle_p1;
  Vec2 angle_p2;
};

// Canvas -> menu zoom-out transition: a single stage that eases the live
// viewport out to fit the document (page for bounded/slideshow, content for
// infinite) while a framebuffer snapshot of each frame flies into the
// document's menu tile. One offscreen content render plus one scaled blit per
// frame keeps it cheap.
struct MenuEnterTransition {
  bool active = false;
  float t = 0.0f;  // unified progress 0..1
  bool do_zoom = false;
  Vec2 from_center{0.0f, 0.0f};
  float from_zoom = 1.0f;
  Vec2 fit_center{0.0f, 0.0f};
  float fit_zoom = 1.0f;
  Bitmap frame;  // offscreen content render, resized to the tile
};

// Gating flags for a left-button canvas press, shared by the per-mode
// press handlers so each mode only sees whether it may act.
struct PressGate {
  bool ui_consumed = false;
  bool off_limits = false;
};

// Application context: owns every piece of persistent UI/document state and
// the top-level loop. Formerly all function-local state in main(); grouping
// it here lets document operations, rendering and event handling live in
// focused methods/files instead of one giant function body.
struct App {
  static constexpr float kPanSpeed = 500.0f;
  static constexpr float kMenuTransitionDur = 0.45f;  // seconds, zoom + flight
  // Wacom barrel button index for pan/zoom/rotate gestures (SDL numbers
  // barrel-only buttons from 1: 1 = lower barrel, 2 = upper barrel; the tip
  // reports separately as pen down/up, not as a button).
  static constexpr int kPenLowerBarrel = 1;

  int Run();

  SdlHost host;
  SDL_Window* window = nullptr;
  int window_width = kWindowWidth;
  int window_height = kWindowHeight;

  Viewport viewport;
  InputManager input;
  LayerStack layer_stack;
  ColorManager color_manager;
  CommandExecutor executor;
  OpenDocument session;  // current .nano document (name + dirty flag)

  std::shared_ptr<Typeface> ui_typeface;

  float mouse_x = 0.0f;
  float mouse_y = 0.0f;
  Uint64 last_time = 0;

  StrokeRecorder stroke;
  ShapeRecorder shape;
  // Erase uses the same stroke recorder; select uses a rect recorder.
  ShapeRecorder select_rect;
  // Freehand lasso polygon in select mode (canvas coords).
  std::vector<Vec2> lasso_points;
  bool lasso_active = false;
  NodeEditorState node_editor;
  Tool previous_tool = Tool::kNone;

  TransformGizmoState gizmo;
  TextEditorState text_editor;
  RulerState ruler;

  SDL_Cursor* cursor_default = nullptr;
  SDL_Cursor* cursor_crosshair = nullptr;
  SDL_Cursor* cursor_ibeam = nullptr;

  GestureState gestures;
  bool was_in_menu = false;
  MenuEnterTransition menu_transition;
  Uint64 frame_now = 0;
  float frame_dt = 0.0f;

  bool Init();
  void ResizeRenderTargets(int w, int h);
  // Drop a floating raster selection, pasting its pixels at their current
  // position. No-op when no drag is in flight.
  void CommitFloat();
  // Page rect for bounded documents (and slideshows); empty otherwise.
  [[nodiscard]] Rect BoundedPage() const;
  // Clamp a canvas point into the page (no-op without one).
  [[nodiscard]] Vec2 ClampToPage(Vec2 p) const;
  // Erase anything a raster op left outside the page (no-op unless the doc
  // has a page; erasing can't create, so erase tools skip this).
  void ConstrainRaster(RasterLayer* raster);
  // Document content only (background + layers, no editing chrome or
  // screen-space UI). Shared by the canvas renderer and the menu-enter
  // transition, which replays it while zooming out.
  void RenderDocumentContent(Canvas& canvas);
  // Cancel in-progress gestures. A floating raster selection is dropped in
  // place (its placement is kept); document swaps commit first via
  // CommitFloat so this is a no-op there.
  void CancelGestures();
  // Full transient reset when swapping documents.
  void ResetForDocument();
  void NewDocument(DocType type, float page_w = 1920.0f,
                   float page_h = 1080.0f);
  [[nodiscard]] bool OpenPath(const std::string& path);
  void TickFrame();
  void PollEvents();
  void HandleEvent(SDL_Event& event);
  // Returns true when the event was fully consumed by text editing.
  bool HandleTextTyping(SDL_Event& event);
  void HandleSlideStep(SDL_Event& event);
  void HandleMouseMotion(SDL_Event& event);
  void UpdateToolMenuHover();
  void RecordStrokeMotion(SDL_Event& event);
  void RouteGestureMotion(SDL_Event& event);
  void UpdateNodeMotion(SDL_Event& event);
  void DragGizmo(SDL_Event& event);
  void MoveGizmoSelection(const Vec2& p, Selection& sel, Layer* active);
  void RotateGizmoSelection(const Vec2& p, Selection& sel, Layer* active);
  void ScaleGizmoSelection(const Vec2& p, Selection& sel, Layer* active);
  void HandleMouseWheel(SDL_Event& event);
  void HandleMouseButtonDown(SDL_Event& event);
  void HandleDrawPress(const SDL_Event& event, const PressGate& press);
  void HandleShapePress(const SDL_Event& event, const PressGate& press);
  void HandleErasePress(const SDL_Event& event, const PressGate& press);
  void HandleSelectPress(const SDL_Event& event, const PressGate& press);
  void HandleNodePress(const SDL_Event& event, const PressGate& press);
  void PickNodePath(const Vec2& p, VectorLayer* vector);
  void ApplyNodeTool(VectorLayer* vector, int idx);
  void EditToolNodeClick(const Vec2& p, VectorLayer* vector);
  void HandleFillPress(const SDL_Event& event, const PressGate& press);
  void HandleTransformPress(const SDL_Event& event, const PressGate& press);
  void HandleTextPress(const SDL_Event& event, const PressGate& press);
  void HandleRulerPress(const SDL_Event& event, const PressGate& press);
  void HandleMouseButtonUp(SDL_Event& event);
  void HandleLeftRelease();
  void CommitStrokeRelease();
  void CommitShapeRelease();
  void CommitSelectionRelease();
  void CommitNodeRelease();
  void HandlePenEvent(SDL_Event& event);
  void HandleWindowEvent(SDL_Event& event);
  void ProcessRequests();
  void ProcessCommandBar();
  void ProcessDocRequests();
  void ProcessSelectionRequests();
  void ProcessModeExits();
  void ProcessNavigation();
  void UpdateCursor();
  void RenderFrame();
  void RenderMenuTransition();
  void RenderMenu();
  void RenderEditingOverlays(Canvas& canvas);
  void RenderPaintPreviews(Canvas& canvas);
  void RenderSelectionOverlays(Canvas& canvas);
  void RenderEditorOverlays(Canvas& canvas);
  void RenderChrome(Canvas& canvas);
};

}  // namespace nanote
