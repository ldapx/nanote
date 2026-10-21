#include "app/app.h"

#include "doc/document.h"

#include <algorithm>

namespace nanote {

// Drop a floating raster selection, pasting its pixels at their current
// position. No-op when no drag is in flight.
void App::CommitFloat() {
  if (!gizmo.raster_float.active) return;
  const auto& layers = layer_stack.GetLayers();
  if (gizmo.float_layer >= 0 &&
      gizmo.float_layer < static_cast<int>(layers.size()) &&
      layers[gizmo.float_layer]->type == LayerType::kRaster) {
    auto* raster = static_cast<RasterLayer*>(layers[gizmo.float_layer].get());
    RasterFloatCommit(raster, gizmo.raster_float);
    if (session.props.has_page()) {
      ConstrainRasterToPage(raster, BoundedPageRect(session.props));
    }
    session.dirty = true;
  }
  gizmo.raster_float = RasterFloat();
  gizmo.float_layer = -1;
}

// Page rect for bounded documents (and slideshows); empty otherwise.
Rect App::BoundedPage() const {
  if (!session.props.has_page()) {
    return Rect::MakeEmpty();
  }
  return BoundedPageRect(session.props);
}

// Clamp a canvas point into the page (no-op without one).
Vec2 App::ClampToPage(Vec2 p) const {
  const Rect page = BoundedPage();
  if (!page.isEmpty()) {
    p.x = std::clamp(p.x, page.left, page.right);
    p.y = std::clamp(p.y, page.top, page.bottom);
  }
  return p;
}

// Erase anything a raster op left outside the page (no-op unless the doc has
// a page; erasing can't create, so erase tools skip this).
void App::ConstrainRaster(RasterLayer* raster) {
  if (raster != nullptr && session.props.has_page()) {
    ConstrainRasterToPage(raster, BoundedPageRect(session.props));
  }
}

// Cancel in-progress gestures. A floating raster selection is dropped in
// place (its placement is kept); document swaps commit first via CommitFloat
// so this is a no-op there.
void App::CancelGestures() {
  stroke.is_drawing = false;
  stroke.points.clear();
  shape.is_active = false;
  select_rect.is_active = false;
  node_editor.dragging = false;
  CommitFloat();
  gizmo.active = false;
  gizmo.dragging = false;
  gizmo.handle = -1;
  lasso_points.clear();
  lasso_active = false;
  ruler.measuring = false;
  ruler.angle_stage = 0;
  gestures.CancelAll(window);
  input.CloseToolMenu();
  input.SetToolMenuDragging(-1);
}

// Full transient reset when swapping documents.
void App::ResetForDocument() {
  CancelGestures();
  node_editor.Clear();
  text_editor.active = false;
  text_editor.text.clear();
  text_editor.original_text.clear();
  text_editor.object_index = -1;
  layer_stack.GetSelection().Clear();
}

void App::NewDocument(DocType type, float page_w, float page_h) {
  CommitFloat();  // drop any in-flight pixels
                  // before swapping stacks
  layer_stack = LayerStack();
  viewport = Viewport();
  viewport.window_width_ = window_width;
  viewport.window_height_ = window_height;
  session.name = UniqueUntitledName();
  session.dirty = false;
  session.props = DocProps();
  session.props.type = type;
  if (type == DocType::kBounded || type == DocType::kSlideshow) {
    session.props.page_w = std::clamp(page_w, 1.0f, 16384.0f);
    session.props.page_h = std::clamp(page_h, 1.0f, 16384.0f);
    // Frame the page on open.
    viewport.center_.x = 0.0f;
    viewport.center_.y = 0.0f;
    viewport.SetZoom(std::min(window_width / session.props.page_w,
                              window_height / session.props.page_h));
  }
  if (type == DocType::kSlideshow) {
    session.props.slides.push_back(
        {"Slide 1", viewport.center_.x, viewport.center_.y, viewport.zoom_});
    session.props.current_slide = 0;
  }
  ResetForDocument();
}

bool App::OpenPath(const std::string& path) {
  LayerStack loaded;
  Viewport view = viewport;
  DocProps props;
  if (!LoadDocument(path, loaded, view, ui_typeface, props)) {
    return false;
  }
  CommitFloat();  // drop any in-flight pixels
                  // before swapping stacks
  layer_stack = std::move(loaded);
  viewport = view;
  session.name = DocNameFromPath(path);
  session.props = props;
  session.dirty = false;
  ResetForDocument();
  return true;
}

}  // namespace nanote
