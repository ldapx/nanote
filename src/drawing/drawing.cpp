#include "drawing/drawing.h"

#include "drawing/brush_engine.h"
#include "render/canvas.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>

namespace nanote {

void CommitBrushStroke(VectorLayer* layer, const std::vector<Vec2>& points,
                       Color color, float width) {
  if (points.empty() || layer == nullptr) return;
  auto obj = std::make_unique<BrushObject>();
  obj->points = points;
  obj->color = color;
  obj->stroke_color = color;
  obj->width = width;
  layer->objects.push_back(std::move(obj));
}

void CommitRasterStroke(RasterLayer* layer, const std::vector<Vec2>& points,
                        Color color, const DabParams& params, uint32_t seed) {
  if (points.empty() || layer == nullptr) {
    return;
  }
  PaintStrokeDabs(layer, points, params, color, seed);
}

void CommitVectorStroke(VectorLayer* layer, const std::vector<Vec2>& points,
                        Color stroke_color, float width) {
  if (points.size() < 2 || layer == nullptr) {
    return;
  }
  auto obj = std::make_unique<PathObject>();
  obj->path.moveTo(points[0].x, points[0].y);
  for (size_t i = 1; i < points.size(); ++i) {
    obj->path.lineTo(points[i].x, points[i].y);
  }
  obj->stroke_color = stroke_color;
  obj->stroke_width = width;
  obj->fill_color = kColorTransparent;
  layer->objects.push_back(std::move(obj));
}

Path BuildShapePath(Tool tool, float ax, float ay, float cx, float cy,
                    bool shift) {
  Path path;
  switch (tool) {
    case Tool::kLine: {
      path.moveTo(ax, ay);
      path.lineTo(cx, cy);
      break;
    }
    case Tool::kRectangle: {
      if (shift) {
        float dx = cx - ax;
        float dy = cy - ay;
        float side = std::max(std::abs(dx), std::abs(dy));
        float sx = (dx >= 0) ? side : -side;
        float sy = (dy >= 0) ? side : -side;
        path.addRect(Rect::MakeLTRB(ax, ay, ax + sx, ay + sy));
      } else {
        path.addRect(Rect::MakeLTRB(ax, ay, cx, cy));
      }
      break;
    }
    case Tool::kCircle: {
      float r = std::hypot(cx - ax, cy - ay);
      path.addCircle(ax, ay, r, PathDirection::kCW);
      break;
    }
    case Tool::kEllipse: {
      if (shift) {
        float mx = (ax + cx) / 2.0f;
        float my = (ay + cy) / 2.0f;
        float r = std::max(std::abs(cx - ax), std::abs(cy - ay)) / 2.0f;
        path.addCircle(mx, my, r, PathDirection::kCW);
      } else {
        float mx = (ax + cx) / 2.0f;
        float my = (ay + cy) / 2.0f;
        float rx = std::abs(cx - ax) / 2.0f;
        float ry = std::abs(cy - ay) / 2.0f;
        path.addOval(Rect::MakeXYWH(mx - rx, my - ry, rx * 2, ry * 2),
                     PathDirection::kCW);
      }
      break;
    }
    case Tool::kPolygon: {
      constexpr int kSides = 5;
      float r = std::hypot(cx - ax, cy - ay);
      float a0 = std::atan2(cy - ay, cx - ax);
      path.moveTo(ax + r * std::cos(a0), ay + r * std::sin(a0));
      for (int i = 1; i <= kSides; ++i) {
        float a = a0 + i * 2.0f * 3.14159265f / kSides;
        path.lineTo(ax + r * std::cos(a), ay + r * std::sin(a));
      }
      path.close();
      break;
    }
    case Tool::kStar: {
      constexpr int kPoints = 5;
      float r_out = std::hypot(cx - ax, cy - ay);
      float r_in = r_out * 0.4f;
      float a0 = std::atan2(cy - ay, cx - ax);
      path.moveTo(ax + r_out * std::cos(a0), ay + r_out * std::sin(a0));
      for (int i = 1; i <= kPoints * 2; ++i) {
        float a = a0 + i * 3.14159265f / kPoints;
        float r = (i % 2 == 0) ? r_out : r_in;
        path.lineTo(ax + r * std::cos(a), ay + r * std::sin(a));
      }
      path.close();
      break;
    }
    case Tool::kArc: {
      float r = std::hypot(cx - ax, cy - ay);
      float a0 = 0.0f;
      float a1 = std::atan2(cy - ay, cx - ax);
      if (a1 < a0) a1 += 2.0f * 3.14159265f;
      path.arcTo(Rect::MakeLTRB(ax - r, ay - r, ax + r, ay + r),
                 a0 * 180.0f / 3.14159265f, (a1 - a0) * 180.0f / 3.14159265f,
                 false);
      break;
    }
    case Tool::kBezier: {
      float dx = cx - ax;
      float dy = cy - ay;
      path.moveTo(ax, ay);
      path.cubicTo(ax + dx * 0.3f - dy * 0.3f, ay + dy * 0.3f + dx * 0.3f,
                   cx - dx * 0.3f - dy * 0.3f, cy - dy * 0.3f + dx * 0.3f, cx,
                   cy);
      break;
    }
    default:
      break;
  }
  return path;
}

void CommitShapeRaster(RasterLayer* layer, const Path& path, Color stroke_color,
                       Color fill_color, float stroke_width) {
  if (layer == nullptr) {
    return;
  }
  Rect bounds = path.getBounds();
  layer->EnsureContains(bounds.left, bounds.top);
  layer->EnsureContains(bounds.right, bounds.bottom);

  Canvas bitmap_canvas(layer->bitmap);
  bitmap_canvas.translate(-static_cast<float>(layer->offset_x),
                          -static_cast<float>(layer->offset_y));

  if (GetA(fill_color) > 0) {
    bitmap_canvas.drawPath(path, Paint::Fill(fill_color));
  }
  if (GetA(stroke_color) > 0) {
    bitmap_canvas.drawPath(path, Paint::Stroke(stroke_color, stroke_width));
  }
}

void CommitShapeVector(VectorLayer* layer, const Path& path, Color stroke_color,
                       Color fill_color, float stroke_width) {
  if (layer == nullptr) {
    return;
  }
  auto obj = std::make_unique<PathObject>();
  obj->path = path;
  obj->stroke_color = stroke_color;
  obj->stroke_width = stroke_width;
  obj->fill_color = fill_color;
  layer->objects.push_back(std::move(obj));
}

// ------------------------------------------------------------------
// Erase helpers
// ------------------------------------------------------------------

void EraseRasterPoint(RasterLayer* layer, float x, float y, float width) {
  if (layer == nullptr) return;
  ErasePointDab(layer, x, y, std::max(1.0f, width) * 0.5f);
}

void EraseVectorPoint(VectorLayer* layer, float x, float y, float tolerance) {
  if (layer == nullptr) return;
  const float tol = std::max(1.0f, tolerance);
  for (int i = static_cast<int>(layer->objects.size()) - 1; i >= 0; --i) {
    if (layer->objects[i]->HitTest(x, y, tol)) {
      layer->objects.erase(layer->objects.begin() + i);
      break;
    }
  }
}

void EraseVectorStroke(VectorLayer* layer, const std::vector<Vec2>& points) {
  if (points.size() < 2 || layer == nullptr) {
    return;
  }
  Path stroke_path;
  stroke_path.moveTo(points[0].x, points[0].y);
  for (size_t i = 1; i < points.size(); ++i) {
    stroke_path.lineTo(points[i].x, points[i].y);
  }
  Rect stroke_bounds = stroke_path.getBounds();
  stroke_bounds.outset(10, 10);

  for (int i = static_cast<int>(layer->objects.size()) - 1; i >= 0; --i) {
    const auto& obj = layer->objects[i];
    Rect obj_bounds = obj->GetBounds();
    if (obj_bounds.intersects(stroke_bounds)) {
      layer->objects.erase(layer->objects.begin() + i);
    }
  }
}

void EraseRasterArea(RasterLayer* layer, const Rect& rect) {
  if (layer == nullptr || rect.isEmpty()) {
    return;
  }
  Canvas bitmap_canvas(layer->bitmap);
  Paint erase;
  erase.clear = true;
  bitmap_canvas.drawRect(
      Rect::MakeLTRB(rect.left - layer->offset_x, rect.top - layer->offset_y,
                     rect.right - layer->offset_x,
                     rect.bottom - layer->offset_y),
      erase);
}

void EraseVectorArea(VectorLayer* layer, const Rect& rect) {
  if (layer == nullptr || rect.isEmpty()) {
    return;
  }
  for (int i = static_cast<int>(layer->objects.size()) - 1; i >= 0; --i) {
    Rect obj_bounds = layer->objects[i]->GetBounds();
    if (rect.contains(obj_bounds)) {
      layer->objects.erase(layer->objects.begin() + i);
    }
  }
}

// Erases everything outside the page rect (canvas coords). Keeps bounded
// documents pixel-clean: dabs, floods and transforms can only leave marks
// inside the page.
void ConstrainRasterToPage(RasterLayer* layer, const Rect& page) {
  if (layer == nullptr || layer->bitmap.empty() || page.isEmpty()) {
    return;
  }
  const int ox = layer->offset_x;
  const int oy = layer->offset_y;
  const int w = layer->bitmap.width();
  const int h = layer->bitmap.height();
  const int lx = std::clamp(static_cast<int>(std::floor(page.left)) - ox, 0, w);
  const int ty = std::clamp(static_cast<int>(std::floor(page.top)) - oy, 0, h);
  const int rx = std::clamp(static_cast<int>(std::ceil(page.right)) - ox, 0, w);
  const int by =
      std::clamp(static_cast<int>(std::ceil(page.bottom)) - oy, 0, h);
  layer->bitmap.eraseRect(kColorTransparent, 0, 0, lx, h);
  layer->bitmap.eraseRect(kColorTransparent, rx, 0, w - rx, h);
  layer->bitmap.eraseRect(kColorTransparent, 0, 0, w, ty);
  layer->bitmap.eraseRect(kColorTransparent, 0, by, w, h - by);
}

// ------------------------------------------------------------------
// Select helpers
// ------------------------------------------------------------------

void SelectVectorInRect(VectorLayer* layer, const Rect& rect,
                        Selection& selection) {
  if (layer == nullptr || rect.isEmpty()) {
    return;
  }
  selection.Clear();
  selection.canvas_rect = rect;
  for (int i = 0; i < static_cast<int>(layer->objects.size()); ++i) {
    Rect obj_bounds = layer->objects[i]->GetBounds();
    if (rect.contains(obj_bounds)) {
      selection.object_indices.push_back(i);
    }
  }
  selection.active = !selection.object_indices.empty();
}

namespace {

// Even-odd point-in-polygon test.
bool PointInPolygon(float x, float y, const std::vector<Vec2>& poly) {
  bool inside = false;
  const size_t n = poly.size();
  for (size_t i = 0, j = n - 1; i < n; j = i++) {
    const float xi = poly[i].x, yi = poly[i].y;
    const float xj = poly[j].x, yj = poly[j].y;
    if ((yi > y) != (yj > y)) {
      const float t = (y - yi) / (yj - yi);
      if (x < xi + t * (xj - xi)) inside = !inside;
    }
  }
  return inside;
}

// Proper segment-segment intersection (touching endpoints don't count).
bool SegmentsCross(float ax, float ay, float bx, float by, float cx, float cy,
                   float dx, float dy) {
  const float d1 = (dx - cx) * (ay - cy) - (dy - cy) * (ax - cx);
  const float d2 = (dx - cx) * (by - cy) - (dy - cy) * (bx - cx);
  const float d3 = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
  const float d4 = (bx - ax) * (dy - ay) - (by - ay) * (dx - ax);
  return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) &&
         ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0));
}

}  // namespace

void SelectVectorInPolygon(VectorLayer* layer, const std::vector<Vec2>& polygon,
                           Selection& selection) {
  selection.Clear();
  if (layer == nullptr || polygon.size() < 3) {
    return;
  }
  float x0 = polygon[0].x, y0 = polygon[0].y;
  float x1 = x0, y1 = y0;
  for (const auto& p : polygon) {
    x0 = std::min(x0, p.x);
    y0 = std::min(y0, p.y);
    x1 = std::max(x1, p.x);
    y1 = std::max(y1, p.y);
  }
  const Rect lasso_rect = Rect::MakeLTRB(x0, y0, x1, y1);
  selection.canvas_rect = lasso_rect;
  const size_t n = polygon.size();
  for (int i = 0; i < static_cast<int>(layer->objects.size()); ++i) {
    Rect b = layer->objects[i]->GetBounds();
    Rect expanded = b;
    expanded.outset(0.5f, 0.5f);
    if (!expanded.intersects(lasso_rect)) continue;
    bool hit = PointInPolygon(b.centerX(), b.centerY(), polygon);
    if (!hit) {
      const Vec2 corners[4] = {{b.left, b.top},
                               {b.right, b.top},
                               {b.right, b.bottom},
                               {b.left, b.bottom}};
      for (const auto& c : corners) {
        if (PointInPolygon(c.x, c.y, polygon)) {
          hit = true;
          break;
        }
      }
    }
    if (!hit) {
      // Large object containing the whole lasso.
      for (const auto& p : polygon) {
        if (b.contains(p.x, p.y)) {
          hit = true;
          break;
        }
      }
    }
    if (!hit) {
      // Edge crossings (lasso slicing through the bounds).
      const Vec2 edges[4][2] = {
          {{b.left, b.top}, {b.right, b.top}},
          {{b.right, b.top}, {b.right, b.bottom}},
          {{b.right, b.bottom}, {b.left, b.bottom}},
          {{b.left, b.bottom}, {b.left, b.top}},
      };
      for (size_t k = 0; k < n && !hit; ++k) {
        const Vec2& a = polygon[k];
        const Vec2& c = polygon[(k + 1) % n];
        for (const auto& e : edges) {
          if (SegmentsCross(a.x, a.y, c.x, c.y, e[0].x, e[0].y, e[1].x,
                            e[1].y)) {
            hit = true;
            break;
          }
        }
      }
    }
    if (hit) selection.object_indices.push_back(i);
  }
  selection.active = !selection.object_indices.empty();
}

void SelectAllOnLayer(Layer* active, Selection& selection) {
  selection.Clear();
  if (active == nullptr) return;
  if (active->type == LayerType::kRaster) {
    const auto* raster = static_cast<const RasterLayer*>(active);
    if (raster->bitmap.empty()) return;
    selection.canvas_rect =
        Rect::MakeXYWH(static_cast<float>(raster->offset_x),
                       static_cast<float>(raster->offset_y),
                       static_cast<float>(raster->bitmap.width()),
                       static_cast<float>(raster->bitmap.height()));
    selection.active = true;
  } else {
    const auto* vector = static_cast<const VectorLayer*>(active);
    if (vector->objects.empty()) return;
    for (int i = 0; i < static_cast<int>(vector->objects.size()); ++i) {
      selection.object_indices.push_back(i);
    }
    selection.canvas_rect = vector->objects.front()->GetBounds();
    for (const auto& obj : vector->objects) {
      selection.canvas_rect.join(obj->GetBounds());
    }
    selection.active = !selection.canvas_rect.isEmpty();
  }
}

// ------------------------------------------------------------------
// Selection highlight
// ------------------------------------------------------------------

void DrawSelectionHighlight(Canvas* canvas, const Selection& selection) {
  if (!selection.active) {
    return;
  }
  Paint fill_paint = Paint::Fill(MakeARGB(51, 100, 150, 255));
  Paint stroke_paint = Paint::Stroke(MakeARGB(200, 100, 150, 255), 1.0f);
  // NOTE: Selection highlight draws a solid outline (no dash support in current
  // stroke options). marching-ants effect has no direct equivalent in
  // BLStrokeOptions.
  canvas->drawRect(selection.canvas_rect, fill_paint);
  canvas->drawRect(selection.canvas_rect, stroke_paint);
}

// ------------------------------------------------------------------
// Fill helpers
// ------------------------------------------------------------------

static bool ColorsMatch(Color a, Color b, int tolerance) {
  return std::abs(static_cast<int>(GetR(a)) - static_cast<int>(GetR(b))) <=
             tolerance &&
         std::abs(static_cast<int>(GetG(a)) - static_cast<int>(GetG(b))) <=
             tolerance &&
         std::abs(static_cast<int>(GetB(a)) - static_cast<int>(GetB(b))) <=
             tolerance &&
         std::abs(static_cast<int>(GetA(a)) - static_cast<int>(GetA(b))) <=
             tolerance;
}

void FloodFillRaster(RasterLayer* layer, float x, float y, Color color,
                     int tolerance) {
  if (layer == nullptr) return;
  int ix = static_cast<int>(std::floor(x)) - layer->offset_x;
  int iy = static_cast<int>(std::floor(y)) - layer->offset_y;
  if (ix < 0 || ix >= layer->bitmap.width() || iy < 0 ||
      iy >= layer->bitmap.height()) {
    return;
  }

  Color target = layer->bitmap.getColor(ix, iy);
  if (target == color) return;

  std::vector<std::pair<int, int>> stack;
  stack.reserve(1024);
  stack.emplace_back(ix, iy);

  while (!stack.empty()) {
    auto [cx, cy] = stack.back();
    stack.pop_back();
    if (cx < 0 || cx >= layer->bitmap.width() || cy < 0 ||
        cy >= layer->bitmap.height()) {
      continue;
    }
    if (!ColorsMatch(layer->bitmap.getColor(cx, cy), target, tolerance))
      continue;
    layer->bitmap.setColor(cx, cy, color);
    stack.emplace_back(cx + 1, cy);
    stack.emplace_back(cx - 1, cy);
    stack.emplace_back(cx, cy + 1);
    stack.emplace_back(cx, cy - 1);
  }
}

bool FloodRegionBounds(RasterLayer* layer, float x, float y, int tolerance,
                       Rect* out) {
  if (layer == nullptr || out == nullptr) return false;
  const int w = layer->bitmap.width();
  const int h = layer->bitmap.height();
  if (w <= 0 || h <= 0) return false;
  const int64_t pixels = static_cast<int64_t>(w) * h;
  if (pixels > 32 * 1024 * 1024) return false;  // guard huge bitmaps
  const int ix = static_cast<int>(std::floor(x)) - layer->offset_x;
  const int iy = static_cast<int>(std::floor(y)) - layer->offset_y;
  if (ix < 0 || ix >= w || iy < 0 || iy >= h) return false;

  const Color target = layer->bitmap.getColor(ix, iy);
  std::vector<uint8_t> seen(static_cast<size_t>(pixels), 0);
  std::vector<int> stack;
  stack.reserve(1024);
  stack.push_back(iy * w + ix);
  seen[iy * w + ix] = 1;
  int x0 = ix, y0 = iy, x1 = ix, y1 = iy;
  size_t budget = 8000000;  // cap explored pixels so huge regions can't hang
  while (!stack.empty() && budget-- > 0) {
    const int cur = stack.back();
    stack.pop_back();
    const int cx = cur % w;
    const int cy = cur / w;
    if (!ColorsMatch(layer->bitmap.getColor(cx, cy), target, tolerance)) {
      continue;
    }
    x0 = std::min(x0, cx);
    y0 = std::min(y0, cy);
    x1 = std::max(x1, cx);
    y1 = std::max(y1, cy);
    if (cx + 1 < w && !seen[cur + 1]) {
      seen[cur + 1] = 1;
      stack.push_back(cur + 1);
    }
    if (cx - 1 >= 0 && !seen[cur - 1]) {
      seen[cur - 1] = 1;
      stack.push_back(cur - 1);
    }
    if (cy + 1 < h && !seen[cur + w]) {
      seen[cur + w] = 1;
      stack.push_back(cur + w);
    }
    if (cy - 1 >= 0 && !seen[cur - w]) {
      seen[cur - w] = 1;
      stack.push_back(cur - w);
    }
  }
  *out = Rect::MakeLTRB(static_cast<float>(x0 + layer->offset_x),
                        static_cast<float>(y0 + layer->offset_y),
                        static_cast<float>(x1 + 1 + layer->offset_x),
                        static_cast<float>(y1 + 1 + layer->offset_y));
  return true;
}

namespace {

// Tight bounds of the non-transparent pixels of a raster layer, in canvas
// coordinates. Returns false when the layer is empty or fully transparent
// so thumbnails fit actual marks instead of the full bitmap extents.
bool TightRasterBounds(const RasterLayer& raster, Rect* out) {
  if (raster.bitmap.empty()) return false;
  const BLImage& img = raster.bitmap.image();
  BLImageData data;
  if (img.get_data(&data) != BL_SUCCESS) {
    return false;
  }
  if (data.format != BL_FORMAT_PRGB32 || data.pixel_data == nullptr) {
    return false;
  }
  const int w = img.width();
  const int h = img.height();
  if (w <= 0 || h <= 0) return false;
  // Bound the scan cost on huge bitmaps; expand the result by the stride
  // so unsampled edge pixels stay inside.
  const int64_t pixels = static_cast<int64_t>(w) * h;
  int step = 1;
  if (pixels > 8 * 1024 * 1024) {
    step = 4;
  } else if (pixels > 2 * 1024 * 1024) {
    step = 2;
  }
  const uint32_t* base = static_cast<const uint32_t*>(data.pixel_data);
  const int stride = static_cast<int>(data.stride / 4);
  int x0 = w, y0 = h, x1 = -1, y1 = -1;
  for (int y = 0; y < h; y += step) {
    const uint32_t* row = base + static_cast<int64_t>(y) * stride;
    for (int x = 0; x < w; x += step) {
      if ((row[x] >> 24) != 0) {
        if (x < x0) x0 = x;
        if (x > x1) x1 = x;
        if (y < y0) y0 = y;
        if (y > y1) y1 = y;
      }
    }
  }
  if (x1 < x0) return false;  // fully transparent
  x0 = std::max(0, x0 - (step - 1));
  y0 = std::max(0, y0 - (step - 1));
  x1 = std::min(w - 1, x1 + (step - 1));
  y1 = std::min(h - 1, y1 + (step - 1));
  *out = Rect::MakeXYWH(static_cast<float>(raster.offset_x + x0),
                        static_cast<float>(raster.offset_y + y0),
                        static_cast<float>(x1 - x0 + 1),
                        static_cast<float>(y1 - y0 + 1));
  return true;
}

}  // namespace

Rect DocContentBounds(const LayerStack& stack) {
  Rect bounds = Rect::MakeEmpty();
  for (const auto& layer : stack.GetLayers()) {
    if (layer == nullptr || !layer->visible) continue;
    if (layer->type == LayerType::kRaster) {
      const auto* raster = static_cast<const RasterLayer*>(layer.get());
      Rect tight = Rect::MakeEmpty();
      if (TightRasterBounds(*raster, &tight)) bounds.join(tight);
    } else {
      const auto* vector = static_cast<const VectorLayer*>(layer.get());
      for (const auto& obj : vector->objects) {
        if (obj == nullptr) continue;
        bounds.join(obj->GetBounds());
      }
    }
  }
  return bounds;
}

namespace {

void TransformVectorObject(std::unique_ptr<VectorObject>& obj,
                           const Matrix& matrix) {
  if (obj == nullptr) return;
  if (auto* brush = dynamic_cast<BrushObject*>(obj.get())) {
    for (auto& p : brush->points) {
      float ox = 0, oy = 0;
      matrix.mapXY(p.x, p.y, &ox, &oy);
      p.x = ox;
      p.y = oy;
    }
  } else if (auto* path = dynamic_cast<PathObject*>(obj.get())) {
    path->path.transform(matrix);
  } else if (auto* text = dynamic_cast<TextObject*>(obj.get())) {
    float ox = 0, oy = 0;
    matrix.mapXY(text->x, text->y, &ox, &oy);
    text->x = ox;
    text->y = oy;
  } else if (auto* group = dynamic_cast<GroupObject*>(obj.get())) {
    for (auto& child : group->children) {
      TransformVectorObject(child, matrix);
    }
  }
}

}  // namespace

void FillVectorObject(std::unique_ptr<VectorObject>& obj, Color color) {
  if (obj == nullptr) {
    SDL_Log("[FILL] FillVectorObject called with null");
    return;
  }
  if (auto* path = dynamic_cast<PathObject*>(obj.get())) {
    SDL_Log("[FILL] PathObject fill_color set to 0x%08X", color);
    path->fill_color = color;
  } else if (auto* group = dynamic_cast<GroupObject*>(obj.get())) {
    SDL_Log("[FILL] GroupObject with %zu children", group->children.size());
    for (auto& child : group->children) {
      FillVectorObject(child, color);
    }
  } else if (auto* brush = dynamic_cast<BrushObject*>(obj.get())) {
    SDL_Log(
        "[FILL] BrushObject fill ignored (stroke "
        "only)");
    brush->fill_color = color;
  } else {
    SDL_Log("[FILL] unknown object type");
  }
}

// ------------------------------------------------------------------
// Transform helpers
// ------------------------------------------------------------------

// ------------------------------------------------------------------
// Transform helpers
// ------------------------------------------------------------------

namespace {

// Raw pixel copy (exact replace, premultiplication
// preserved). All coords are bitmap-local; regions
// are clipped to both bitmaps.
void CopyPixels(Bitmap* dst, int dx, int dy, const Bitmap& src, int sx, int sy,
                int w, int h) {
  if (dst == nullptr || dst->empty() || src.empty() || w <= 0 || h <= 0) {
    return;
  }
  if (sx < 0) {
    w += sx;
    dx -= sx;
    sx = 0;
  }
  if (sy < 0) {
    h += sy;
    dy -= sy;
    sy = 0;
  }
  if (dx < 0) {
    w += dx;
    sx -= dx;
    dx = 0;
  }
  if (dy < 0) {
    h += dy;
    sy -= dy;
    dy = 0;
  }
  w = std::min({w, dst->width() - dx, src.width() - sx});
  h = std::min({h, dst->height() - dy, src.height() - sy});
  if (w <= 0 || h <= 0) return;
  BLImageData dst_data, src_data;
  if (dst->image().make_mutable(&dst_data) != BL_SUCCESS) return;
  if (src.image().get_data(&src_data) != BL_SUCCESS) return;
  uint32_t* d = static_cast<uint32_t*>(dst_data.pixel_data);
  const uint32_t* s = static_cast<const uint32_t*>(src_data.pixel_data);
  const int ds = static_cast<int>(dst_data.stride / 4);
  const int ss = static_cast<int>(src_data.stride / 4);
  for (int y = 0; y < h; ++y) {
    std::memcpy(&d[(dy + y) * ds + dx], &s[(sy + y) * ss + sx],
                static_cast<size_t>(w) * 4);
  }
}

}  // namespace

void RasterFloatBegin(RasterLayer* layer, const Rect& rect, RasterFloat* fl) {
  if (layer == nullptr || fl == nullptr) return;
  *fl = RasterFloat();
  if (rect.isEmpty() || layer->bitmap.empty()) return;
  const int ox = layer->offset_x;
  const int oy = layer->offset_y;
  const int bw = layer->bitmap.width();
  const int bh = layer->bitmap.height();
  int ix0 = std::max(ox, static_cast<int>(std::floor(rect.left)));
  int iy0 = std::max(oy, static_cast<int>(std::floor(rect.top)));
  int ix1 = std::min(ox + bw, static_cast<int>(std::ceil(rect.right)));
  int iy1 = std::min(oy + bh, static_cast<int>(std::ceil(rect.bottom)));
  const int w = ix1 - ix0;
  const int h = iy1 - iy0;
  if (w <= 0 || h <= 0) return;
  Bitmap cut;
  cut.allocN32Pixels(w, h);
  CopyPixels(&cut, 0, 0, layer->bitmap, ix0 - ox, iy0 - oy, w, h);
  layer->bitmap.eraseRect(kColorTransparent, ix0 - ox, iy0 - oy, w, h);
  fl->orig = cut;
  fl->view = cut;
  fl->x = static_cast<float>(ix0);
  fl->y = static_cast<float>(iy0);
  fl->home = Rect::MakeLTRB(static_cast<float>(ix0), static_cast<float>(iy0),
                            static_cast<float>(ix1), static_cast<float>(iy1));
  fl->active = true;
}

void RasterFloatMove(RasterFloat* fl, float dx, float dy) {
  if (fl == nullptr || !fl->active) return;
  fl->x += dx;
  fl->y += dy;
}

void RasterFloatScale(RasterFloat* fl, float cx, float cy, float sx, float sy) {
  if (fl == nullptr || !fl->active) return;
  if (sx == 0.0f || sy == 0.0f) return;
  const float ax = std::abs(sx);
  const float ay = std::abs(sy);
  const int nw =
      std::max(1, static_cast<int>(std::round(fl->view.width() * ax)));
  const int nh =
      std::max(1, static_cast<int>(std::round(fl->view.height() * ay)));
  Bitmap scaled;
  scaled.allocN32Pixels(nw, nh);
  scaled.eraseColor(kColorTransparent);
  {
    Canvas canvas(scaled);
    canvas.scale(nw / static_cast<float>(fl->view.width()),
                 nh / static_cast<float>(fl->view.height()));
    canvas.drawImage(fl->view, 0, 0);
  }
  fl->view = scaled;
  fl->x = cx + (fl->x - cx) * sx;
  fl->y = cy + (fl->y - cy) * sy;
}

void RasterFloatRotate(RasterFloat* fl, float cx, float cy, float degrees) {
  if (fl == nullptr || !fl->active) return;
  const float rad = degrees * 3.14159265f / 180.0f;
  const float s = std::sin(std::abs(rad));
  const float c = std::cos(std::abs(rad));
  const int old_w = fl->view.width();
  const int old_h = fl->view.height();
  const int new_w =
      std::max(1, static_cast<int>(std::round(old_w * c + old_h * s)));
  const int new_h =
      std::max(1, static_cast<int>(std::round(old_w * s + old_h * c)));
  Bitmap rotated;
  rotated.allocN32Pixels(new_w, new_h);
  rotated.eraseColor(kColorTransparent);
  {
    Canvas canvas(rotated);
    canvas.translate(new_w / 2.0f, new_h / 2.0f);
    canvas.rotate(degrees);
    canvas.translate(-old_w / 2.0f, -old_h / 2.0f);
    canvas.drawImage(fl->view, 0, 0);
  }
  fl->view = rotated;
  fl->x = cx - new_w / 2.0f;
  fl->y = cy - new_h / 2.0f;
}

void RasterFloatCommit(RasterLayer* layer, const RasterFloat& fl) {
  if (layer == nullptr || !fl.active || fl.view.empty()) return;
  layer->EnsureContains(fl.x, fl.y);
  layer->EnsureContains(fl.x + fl.view.width(), fl.y + fl.view.height());
  Canvas canvas(layer->bitmap);
  canvas.drawImage(fl.view, fl.x - static_cast<float>(layer->offset_x),
                   fl.y - static_cast<float>(layer->offset_y));
}

void RasterFloatRestore(RasterLayer* layer, const RasterFloat& fl) {
  if (layer == nullptr || !fl.active || fl.orig.empty()) return;
  layer->EnsureContains(fl.home.left, fl.home.top);
  layer->EnsureContains(fl.home.right, fl.home.bottom);
  Canvas canvas(layer->bitmap);
  canvas.drawImage(fl.orig, fl.home.left - static_cast<float>(layer->offset_x),
                   fl.home.top - static_cast<float>(layer->offset_y));
}

void RasterFloatFlipH(RasterFloat* fl, float cx) {
  if (fl == nullptr || !fl->active || fl->view.empty()) return;
  Bitmap mirrored;
  mirrored.allocN32Pixels(fl->view.width(), fl->view.height());
  mirrored.eraseColor(kColorTransparent);
  {
    Canvas canvas(mirrored);
    // Mirror: device i samples source (w-1-i). With
    // the NN corner+0.5 sampling in drawImage, that
    // needs T(w-1)*S(-1); T(w) would drop a column
    // and leave a transparent edge.
    canvas.translate(static_cast<float>(fl->view.width()) - 1, 0);
    canvas.scale(-1, 1);
    canvas.drawImage(fl->view, 0, 0);
  }
  fl->view = mirrored;
  fl->x = 2 * cx - fl->x - static_cast<float>(fl->view.width());
}

void RasterFloatFlipV(RasterFloat* fl, float cy) {
  if (fl == nullptr || !fl->active || fl->view.empty()) return;
  Bitmap mirrored;
  mirrored.allocN32Pixels(fl->view.width(), fl->view.height());
  mirrored.eraseColor(kColorTransparent);
  {
    Canvas canvas(mirrored);
    canvas.translate(0, static_cast<float>(fl->view.height()) - 1);
    canvas.scale(1, -1);
    canvas.drawImage(fl->view, 0, 0);
  }
  fl->view = mirrored;
  fl->y = 2 * cy - fl->y - static_cast<float>(fl->view.height());
}

void RasterFloatScaleAbs(RasterFloat* fl, float cx, float cy, float sx,
                         float sy) {
  if (fl == nullptr || !fl->active || fl->orig.empty()) return;
  if (sx == 0.0f || sy == 0.0f) return;
  const int nw = std::max(
      1, static_cast<int>(std::round(fl->orig.width() * std::abs(sx))));
  const int nh = std::max(
      1, static_cast<int>(std::round(fl->orig.height() * std::abs(sy))));
  Bitmap scaled;
  scaled.allocN32Pixels(nw, nh);
  scaled.eraseColor(kColorTransparent);
  {
    Canvas canvas(scaled);
    canvas.scale(nw / static_cast<float>(fl->orig.width()),
                 nh / static_cast<float>(fl->orig.height()));
    canvas.drawImage(fl->orig, 0, 0);
  }
  fl->view = scaled;
  fl->x = cx + (fl->home.left - cx) * sx;
  fl->y = cy + (fl->home.top - cy) * sy;
}

void RasterFloatRotateAbs(RasterFloat* fl, float cx, float cy, float degrees) {
  if (fl == nullptr || !fl->active || fl->orig.empty()) return;
  const float rad = degrees * 3.14159265f / 180.0f;
  const float s = std::sin(std::abs(rad));
  const float c = std::cos(std::abs(rad));
  const int old_w = fl->orig.width();
  const int old_h = fl->orig.height();
  const int new_w =
      std::max(1, static_cast<int>(std::round(old_w * c + old_h * s)));
  const int new_h =
      std::max(1, static_cast<int>(std::round(old_w * s + old_h * c)));
  Bitmap rotated;
  rotated.allocN32Pixels(new_w, new_h);
  rotated.eraseColor(kColorTransparent);
  {
    Canvas canvas(rotated);
    canvas.translate(new_w / 2.0f, new_h / 2.0f);
    canvas.rotate(degrees);
    canvas.translate(-old_w / 2.0f, -old_h / 2.0f);
    canvas.drawImage(fl->orig, 0, 0);
  }
  fl->view = rotated;
  fl->x = cx - new_w / 2.0f;
  fl->y = cy - new_h / 2.0f;
}

void TransformSelectionMove(Selection& selection, Layer* active, float dx,
                            float dy) {
  if (!selection.active || active == nullptr) return;
  if (active->type == LayerType::kRaster) {
    auto* raster = static_cast<RasterLayer*>(active);
    RasterFloat fl;
    RasterFloatBegin(raster, selection.canvas_rect, &fl);
    if (fl.active) {
      RasterFloatMove(&fl, dx, dy);
      RasterFloatCommit(raster, fl);
    }
    selection.canvas_rect.offset(dx, dy);
  } else if (active->type == LayerType::kVector) {
    auto* vector = static_cast<VectorLayer*>(active);
    Matrix m;
    m.setTranslate(dx, dy);
    for (int idx : selection.object_indices) {
      if (idx >= 0 && idx < static_cast<int>(vector->objects.size())) {
        TransformVectorObject(vector->objects[idx], m);
      }
    }
    selection.canvas_rect.offset(dx, dy);
  }
}

void TransformSelectionScale(Selection& selection, Layer* active, float cx,
                             float cy, float sx, float sy) {
  if (!selection.active || active == nullptr) return;
  // Scale about the pivot (cx,cy): T(pivot) * S *
  // T(-pivot).
  Matrix m;
  m.setTranslate(cx, cy);
  m.postScale(sx, sy);
  m.postTranslate(-cx, -cy);
  if (active->type == LayerType::kRaster) {
    auto* raster = static_cast<RasterLayer*>(active);
    RasterFloat fl;
    RasterFloatBegin(raster, selection.canvas_rect, &fl);
    if (fl.active) {
      RasterFloatScale(&fl, cx, cy, sx, sy);
      RasterFloatCommit(raster, fl);
      selection.canvas_rect =
          Rect::MakeXYWH(fl.x, fl.y, static_cast<float>(fl.view.width()),
                         static_cast<float>(fl.view.height()));
    }
  } else if (active->type == LayerType::kVector) {
    auto* vector = static_cast<VectorLayer*>(active);
    for (int idx : selection.object_indices) {
      if (idx >= 0 && idx < static_cast<int>(vector->objects.size())) {
        TransformVectorObject(vector->objects[idx], m);
      }
    }
    if (!selection.object_indices.empty()) {
      selection.canvas_rect =
          vector->objects[selection.object_indices[0]]->GetBounds();
      for (int idx : selection.object_indices) {
        selection.canvas_rect.join(vector->objects[idx]->GetBounds());
      }
    }
  }
}

void TransformSelectionRotate(Selection& selection, Layer* active, float cx,
                              float cy, float degrees) {
  if (!selection.active || active == nullptr) return;
  Matrix m;
  m.setRotate(degrees, cx, cy);
  if (active->type == LayerType::kRaster) {
    auto* raster = static_cast<RasterLayer*>(active);
    RasterFloat fl;
    RasterFloatBegin(raster, selection.canvas_rect, &fl);
    if (fl.active) {
      RasterFloatRotate(&fl, cx, cy, degrees);
      RasterFloatCommit(raster, fl);
      selection.canvas_rect =
          Rect::MakeXYWH(fl.x, fl.y, static_cast<float>(fl.view.width()),
                         static_cast<float>(fl.view.height()));
    }
  } else if (active->type == LayerType::kVector) {
    auto* vector = static_cast<VectorLayer*>(active);
    for (int idx : selection.object_indices) {
      if (idx >= 0 && idx < static_cast<int>(vector->objects.size())) {
        TransformVectorObject(vector->objects[idx], m);
      }
    }
    if (!selection.object_indices.empty()) {
      selection.canvas_rect =
          vector->objects[selection.object_indices[0]]->GetBounds();
      for (int idx : selection.object_indices) {
        selection.canvas_rect.join(vector->objects[idx]->GetBounds());
      }
    }
  }
}

void TransformSelectionFlipH(Selection& selection, Layer* active, float cx) {
  if (!selection.active || active == nullptr) return;
  Matrix m;
  m.setTranslate(cx, 0);
  m.postScale(-1, 1);
  m.postTranslate(-cx, 0);
  if (active->type == LayerType::kRaster) {
    auto* raster = static_cast<RasterLayer*>(active);
    RasterFloat fl;
    RasterFloatBegin(raster, selection.canvas_rect, &fl);
    if (fl.active) {
      RasterFloatFlipH(&fl, cx);
      RasterFloatCommit(raster, fl);
      selection.canvas_rect =
          Rect::MakeXYWH(fl.x, fl.y, static_cast<float>(fl.view.width()),
                         static_cast<float>(fl.view.height()));
    }
  } else if (active->type == LayerType::kVector) {
    auto* vector = static_cast<VectorLayer*>(active);
    for (int idx : selection.object_indices) {
      if (idx >= 0 && idx < static_cast<int>(vector->objects.size())) {
        TransformVectorObject(vector->objects[idx], m);
      }
    }
    if (!selection.object_indices.empty()) {
      selection.canvas_rect =
          vector->objects[selection.object_indices[0]]->GetBounds();
      for (int idx : selection.object_indices) {
        selection.canvas_rect.join(vector->objects[idx]->GetBounds());
      }
    }
  }
}

void TransformSelectionFlipV(Selection& selection, Layer* active, float cy) {
  if (!selection.active || active == nullptr) return;
  Matrix m;
  m.setTranslate(0, cy);
  m.postScale(1, -1);
  m.postTranslate(0, -cy);
  if (active->type == LayerType::kRaster) {
    auto* raster = static_cast<RasterLayer*>(active);
    RasterFloat fl;
    RasterFloatBegin(raster, selection.canvas_rect, &fl);
    if (fl.active) {
      RasterFloatFlipV(&fl, cy);
      RasterFloatCommit(raster, fl);
      selection.canvas_rect =
          Rect::MakeXYWH(fl.x, fl.y, static_cast<float>(fl.view.width()),
                         static_cast<float>(fl.view.height()));
    }
  } else if (active->type == LayerType::kVector) {
    auto* vector = static_cast<VectorLayer*>(active);
    for (int idx : selection.object_indices) {
      if (idx >= 0 && idx < static_cast<int>(vector->objects.size())) {
        TransformVectorObject(vector->objects[idx], m);
      }
    }
    if (!selection.object_indices.empty()) {
      selection.canvas_rect =
          vector->objects[selection.object_indices[0]]->GetBounds();
      for (int idx : selection.object_indices) {
        selection.canvas_rect.join(vector->objects[idx]->GetBounds());
      }
    }
  }
}

void GroupSelection(VectorLayer* layer, Selection& selection) {
  if (layer == nullptr || selection.object_indices.size() < 2) return;
  auto group = std::make_unique<GroupObject>();
  std::vector<int> indices = selection.object_indices;
  std::sort(indices.begin(), indices.end(), std::greater<int>());
  for (int idx : indices) {
    if (idx >= 0 && idx < static_cast<int>(layer->objects.size())) {
      group->children.push_back(std::move(layer->objects[idx]));
      layer->objects.erase(layer->objects.begin() + idx);
    }
  }
  int insert_pos = indices.back();
  layer->objects.insert(layer->objects.begin() + insert_pos, std::move(group));
  selection.object_indices = {insert_pos};
  selection.canvas_rect = layer->objects[insert_pos]->GetBounds();
}

void UngroupSelection(VectorLayer* layer, Selection& selection) {
  if (layer == nullptr || selection.object_indices.empty()) return;
  std::vector<int> new_indices;
  std::vector<int> indices = selection.object_indices;
  std::sort(indices.begin(), indices.end(), std::greater<int>());
  for (int idx : indices) {
    if (idx >= 0 && idx < static_cast<int>(layer->objects.size())) {
      if (auto* group = dynamic_cast<GroupObject*>(layer->objects[idx].get())) {
        int insert_pos = idx;
        for (auto it = group->children.rbegin(); it != group->children.rend();
             ++it) {
          layer->objects.insert(layer->objects.begin() + insert_pos,
                                std::move(*it));
          new_indices.push_back(insert_pos);
        }
        layer->objects.erase(layer->objects.begin() + idx +
                             static_cast<int>(group->children.size()));
      }
    }
  }
  if (!new_indices.empty()) {
    selection.object_indices = new_indices;
    selection.canvas_rect = layer->objects[new_indices[0]]->GetBounds();
    for (int idx : new_indices) {
      selection.canvas_rect.join(layer->objects[idx]->GetBounds());
    }
  }
}

void AlignSelection(Selection& selection, Layer* active,
                    const std::string& edge) {
  if (!selection.active || active->type != LayerType::kVector) return;
  auto* vector = static_cast<VectorLayer*>(active);
  if (selection.object_indices.empty()) return;
  Rect total = vector->objects[selection.object_indices[0]]->GetBounds();
  for (int idx : selection.object_indices) {
    total.join(vector->objects[idx]->GetBounds());
  }
  for (int idx : selection.object_indices) {
    Rect b = vector->objects[idx]->GetBounds();
    float dx = 0, dy = 0;
    if (edge == "left") {
      dx = total.left - b.left;
    } else if (edge == "right") {
      dx = total.right - b.right;
    } else if (edge == "center") {
      dx = total.centerX() - b.centerX();
    } else if (edge == "top") {
      dy = total.top - b.top;
    } else if (edge == "bottom") {
      dy = total.bottom - b.bottom;
    } else if (edge == "middle") {
      dy = total.centerY() - b.centerY();
    }
    Matrix m;
    m.setTranslate(dx, dy);
    TransformVectorObject(vector->objects[idx], m);
  }
  selection.canvas_rect = total;
}

void DistributeSelection(Selection& selection, Layer* active,
                         const std::string& axis) {
  if (!selection.active || active->type != LayerType::kVector) return;
  auto* vector = static_cast<VectorLayer*>(active);
  if (selection.object_indices.size() < 3) return;
  struct Item {
    int idx;
    float pos;
    float size;
  };
  std::vector<Item> items;
  for (int idx : selection.object_indices) {
    Rect b = vector->objects[idx]->GetBounds();
    if (axis == "h") {
      items.push_back({idx, b.centerX(), b.width()});
    } else {
      items.push_back({idx, b.centerY(), b.height()});
    }
  }
  std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
    return a.pos < b.pos;
  });
  float start = items.front().pos;
  float end = items.back().pos;
  float step = (end - start) / static_cast<float>(items.size() - 1);
  for (size_t i = 0; i < items.size(); ++i) {
    float target = start + step * static_cast<float>(i);
    float dx = 0, dy = 0;
    if (axis == "h") {
      dx = target - items[i].pos;
    } else {
      dy = target - items[i].pos;
    }
    Matrix m;
    m.setTranslate(dx, dy);
    TransformVectorObject(vector->objects[items[i].idx], m);
  }
  selection.canvas_rect =
      vector->objects[selection.object_indices[0]]->GetBounds();
  for (int idx : selection.object_indices) {
    selection.canvas_rect.join(vector->objects[idx]->GetBounds());
  }
}

void CommitTextRaster(RasterLayer* layer, const std::string& text, float x,
                      float y, float font_size, Color color,
                      const std::shared_ptr<Typeface>& typeface) {
  if (text.empty() || layer == nullptr) return;

  Font font(typeface, font_size);
  float w = font.measureText(text.c_str(), text.size());
  Rect text_bounds = Rect::MakeXYWH(x, y - font_size, w, font_size);

  layer->EnsureContains(text_bounds.left, text_bounds.top);
  layer->EnsureContains(text_bounds.right, text_bounds.bottom);

  Canvas canvas(layer->bitmap);
  canvas.translate(-static_cast<float>(layer->offset_x),
                   -static_cast<float>(layer->offset_y));
  canvas.drawString(text.c_str(), x, y, font, Paint::Fill(color));
}

void CommitEditorText(Layer* active, const std::string& text, float x, float y,
                      float font_size, int object_index, Color color,
                      const std::shared_ptr<Typeface>& typeface, Rect page) {
  if (active == nullptr) {
    return;
  }
  if (active->type == LayerType::kVector) {
    auto* vector = static_cast<VectorLayer*>(active);
    if (object_index >= 0 &&
        object_index < static_cast<int>(vector->objects.size())) {
      if (auto* text_obj =
              dynamic_cast<TextObject*>(vector->objects[object_index].get())) {
        text_obj->text = text;
        text_obj->x = x;
        text_obj->y = y;
        text_obj->font_size = font_size;
        text_obj->stroke_color = color;
      }
    } else {
      auto obj = std::make_unique<TextObject>();
      obj->text = text;
      obj->x = x;
      obj->y = y;
      obj->font_size = font_size;
      obj->stroke_color = color;
      obj->typeface = typeface;
      vector->objects.push_back(std::move(obj));
    }
  } else if (active->type == LayerType::kRaster) {
    auto* raster = static_cast<RasterLayer*>(active);
    CommitTextRaster(raster, text, x, y, font_size, color, typeface);
    ConstrainRasterToPage(raster, page);
  }
}

}  // namespace nanote
