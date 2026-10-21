#pragma once

#include "core/color.h"
#include "core/font.h"
#include "core/geometry.h"
#include "core/path.h"
#include "core/types.h"
#include "drawing/brush_engine.h"
#include "drawing/node_editor.h"
#include "input/input_manager.h"
#include "layer/layer.h"

#include <memory>
#include <vector>

namespace nanote {

class Canvas;

struct StrokeRecorder {
  bool is_drawing = false;
  std::vector<Vec2> points;
  uint64_t start_time_ms = 0;
  uint32_t seed = 0;  // deterministic RNG seed (spray particles)
};

struct ShapeRecorder {
  bool is_active = false;
  Vec2 anchor;
  Vec2 current;
  Tool tool = Tool::kNone;
};

[[nodiscard]] Path BuildShapePath(Tool tool, float ax, float ay, float cx,
                                  float cy, bool shift);

void CommitRasterStroke(RasterLayer* layer, const std::vector<Vec2>& points,
                        Color color, const DabParams& params, uint32_t seed);

void CommitVectorStroke(VectorLayer* layer, const std::vector<Vec2>& points,
                        Color stroke_color, float width);

void CommitBrushStroke(VectorLayer* layer, const std::vector<Vec2>& points,
                       Color color, float width);

void CommitShapeRaster(RasterLayer* layer, const Path& path, Color stroke_color,
                       Color fill_color, float stroke_width = 2.0f);

void CommitShapeVector(VectorLayer* layer, const Path& path, Color stroke_color,
                       Color fill_color, float stroke_width = 2.0f);

void CommitTextRaster(RasterLayer* layer, const std::string& text, float x,
                      float y, float font_size, Color color,
                      const std::shared_ptr<Typeface>& typeface);

// Commits pending text-editor content to a layer: updates the edited
// TextObject (or appends a new one) on vector layers, stamps pixels on
// raster layers. Shared by Enter-commit, click-away commit and mode-exit
// commit.
void CommitEditorText(Layer* active, const std::string& text, float x, float y,
                      float font_size, int object_index, Color color,
                      const std::shared_ptr<Typeface>& typeface, Rect page);

// ------------------------------------------------------------------
// Erase helpers
// ------------------------------------------------------------------

void EraseRasterPoint(RasterLayer* layer, float x, float y, float width);

void EraseVectorStroke(VectorLayer* layer, const std::vector<Vec2>& points);
void EraseVectorPoint(VectorLayer* layer, float x, float y, float tolerance);

void EraseRasterArea(RasterLayer* layer, const Rect& rect);

void EraseVectorArea(VectorLayer* layer, const Rect& rect);

// Erases every pixel outside the page rect (bounded canvas only).
void ConstrainRasterToPage(RasterLayer* layer, const Rect& page);

// Page rectangle in canvas coordinates (centered on the origin).
[[nodiscard]] inline Rect BoundedPageRect(const DocProps& props) {
  return Rect::MakeXYWH(-props.page_w * 0.5f, -props.page_h * 0.5f,
                        props.page_w, props.page_h);
}

// ------------------------------------------------------------------
// Select helpers
// ------------------------------------------------------------------

void SelectVectorInRect(VectorLayer* layer, const Rect& rect,
                        Selection& selection);
// Freehand lasso: selects objects whose bounds touch the polygon interior.
// canvas_rect is set to the polygon bbox.
void SelectVectorInPolygon(VectorLayer* layer, const std::vector<Vec2>& polygon,
                           Selection& selection);
// Select everything on the active layer (all vector objects, or the full
// raster bitmap rect).
void SelectAllOnLayer(Layer* active, Selection& selection);

// ------------------------------------------------------------------
// Selection highlight
// ------------------------------------------------------------------

void DrawSelectionHighlight(Canvas* canvas, const Selection& selection);

// ------------------------------------------------------------------
// Fill helpers
// ------------------------------------------------------------------

void FloodFillRaster(RasterLayer* layer, float x, float y, Color color,
                     int tolerance = 24);
// Bounding box of the contiguous region around (x,y) whose pixels match
// within tolerance (magic select). Returns false when the seed is outside
// the bitmap; region size is capped so huge fills can't hang the UI.
bool FloodRegionBounds(RasterLayer* layer, float x, float y, int tolerance,
                       Rect* out);
void FillVectorObject(std::unique_ptr<VectorObject>& obj, Color color);

// Tight union of everything visible on the stack (non-transparent raster
// pixels plus vector object bounds), in canvas coordinates. Empty when the
// document has no visible marks; used for thumbnail framing and the
// menu-enter zoom-out target.
[[nodiscard]] Rect DocContentBounds(const LayerStack& stack);

// ------------------------------------------------------------------
// Transform helpers
// ------------------------------------------------------------------

// Floating raster selection: pixels lifted out of the layer so a transform
// drag (or a one-shot command) moves only the selected region instead of
// the whole bitmap. Begin cuts the pixels (leaving a transparent hole);
// the view is transformed during the drag and drawn floating above the
// layer; Commit pastes it at its current position, Restore puts the
// original pixels back where they were lifted from (cancel).
struct RasterFloat {
  bool active = false;
  Bitmap orig;         // lifted pixels, never mutated during the drag
  Bitmap view;         // transformed buffer (render + commit source)
  float x = 0, y = 0;  // current top-left, canvas coords
  Rect home;           // lifted rect, canvas coords
};
void RasterFloatBegin(RasterLayer* layer, const Rect& rect, RasterFloat* fl);
void RasterFloatMove(RasterFloat* fl, float dx, float dy);
void RasterFloatScale(RasterFloat* fl, float cx, float cy, float sx, float sy);
void RasterFloatRotate(RasterFloat* fl, float cx, float cy, float degrees);
void RasterFloatFlipH(RasterFloat* fl, float cx);
void RasterFloatFlipV(RasterFloat* fl, float cy);
// Absolute variants: transform the ORIGINAL lifted pixels by the total
// factors, positioning relative to the home rect. Used for drags so
// repeated motion events don't compound resampling error.
void RasterFloatScaleAbs(RasterFloat* fl, float cx, float cy, float sx,
                         float sy);
void RasterFloatRotateAbs(RasterFloat* fl, float cx, float cy, float degrees);
void RasterFloatCommit(RasterLayer* layer, const RasterFloat& fl);
void RasterFloatRestore(RasterLayer* layer, const RasterFloat& fl);

void TransformSelectionMove(Selection& selection, Layer* active, float dx,
                            float dy);
void TransformSelectionScale(Selection& selection, Layer* active, float cx,
                             float cy, float sx, float sy);
void TransformSelectionRotate(Selection& selection, Layer* active, float cx,
                              float cy, float degrees);
void TransformSelectionFlipH(Selection& selection, Layer* active, float cx);
void TransformSelectionFlipV(Selection& selection, Layer* active, float cy);

void GroupSelection(VectorLayer* layer, Selection& selection);
void UngroupSelection(VectorLayer* layer, Selection& selection);
void AlignSelection(Selection& selection, Layer* active,
                    const std::string& edge);
void DistributeSelection(Selection& selection, Layer* active,
                         const std::string& axis);

}  // namespace nanote
