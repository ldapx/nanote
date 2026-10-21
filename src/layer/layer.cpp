#include "layer/layer.h"

#include "render/canvas.h"

#include <algorithm>
#include <cmath>

namespace nanote {

RasterLayer::RasterLayer(int width, int height, const std::string& layer_name) {
  name = layer_name;
  type = LayerType::kRaster;
  bitmap.allocN32Pixels(width, height);
  bitmap.eraseColor(kColorTransparent);
}

void RasterLayer::Render(Canvas* canvas) const {
  if (!visible) return;
  canvas->drawImage(bitmap, static_cast<float>(offset_x),
                    static_cast<float>(offset_y), opacity);
}

std::unique_ptr<Layer> RasterLayer::Clone() const {
  auto clone = std::make_unique<RasterLayer>();
  clone->name = name;
  clone->type = type;
  clone->visible = visible;
  clone->locked = locked;
  clone->opacity = opacity;
  clone->offset_x = offset_x;
  clone->offset_y = offset_y;
  clone->bitmap = bitmap;
  return clone;
}

void RasterLayer::EnsureContains(float x, float y) {
  int ix = static_cast<int>(std::floor(x));
  int iy = static_cast<int>(std::floor(y));

  if (ix >= offset_x && ix < offset_x + bitmap.width() && iy >= offset_y &&
      iy < offset_y + bitmap.height()) {
    return;
  }

  int new_x = std::min(offset_x, ix - 100);
  int new_y = std::min(offset_y, iy - 100);
  int right = std::max(offset_x + bitmap.width(), ix + 101);
  int bottom = std::max(offset_y + bitmap.height(), iy + 101);
  int new_w = right - new_x;
  int new_h = bottom - new_y;

  Bitmap new_bitmap;
  new_bitmap.allocN32Pixels(new_w, new_h);
  new_bitmap.eraseColor(kColorTransparent);

  if (!bitmap.empty()) {
    Canvas tmp(new_bitmap);
    tmp.drawImage(bitmap, static_cast<float>(offset_x - new_x),
                  static_cast<float>(offset_y - new_y));
  }

  bitmap = new_bitmap;
  offset_x = new_x;
  offset_y = new_y;
}

// ... PathObject implementation ...

void PathObject::Render(Canvas* canvas) const {
  if (GetA(fill_color) > 0) {
    canvas->drawPath(path, Paint::Fill(fill_color));
  }
  if (GetA(stroke_color) > 0) {
    canvas->drawPath(path, Paint::Stroke(stroke_color, stroke_width));
  }
}

namespace {

float DistSqToSegment(float px, float py, float x1, float y1, float x2,
                      float y2) {
  float dx = x2 - x1;
  float dy = y2 - y1;
  float len_sq = dx * dx + dy * dy;
  if (len_sq == 0) {
    return (px - x1) * (px - x1) + (py - y1) * (py - y1);
  }
  float t = std::max(
      0.0f, std::min(1.0f, ((px - x1) * dx + (py - y1) * dy) / len_sq));
  float proj_x = x1 + t * dx;
  float proj_y = y1 + t * dy;
  return (px - proj_x) * (px - proj_x) + (py - proj_y) * (py - proj_y);
}

}  // namespace

bool PathObject::HitTest(float x, float y, float tolerance) const {
  Rect bounds = GetBounds();
  bounds.outset(stroke_width + tolerance, stroke_width + tolerance);
  if (!bounds.contains(x, y)) {
    return false;
  }
  if (path.contains(x, y)) {
    return true;
  }

  float max_dist = stroke_width * 0.5f + tolerance;
  float max_dist_sq = max_dist * max_dist;

  float prev_x = 0, prev_y = 0;
  bool has_prev = false;
  for (const auto& c : path.cmds()) {
    switch (c.verb) {
      case 0:  // move
        prev_x = c.pts[0];
        prev_y = c.pts[1];
        has_prev = true;
        break;
      case 1:  // line
        if (has_prev && DistSqToSegment(x, y, prev_x, prev_y, c.pts[0],
                                        c.pts[1]) <= max_dist_sq)
          return true;
        prev_x = c.pts[0];
        prev_y = c.pts[1];
        has_prev = true;
        break;
      case 2:  // quad (approx as line to end)
        if (has_prev && DistSqToSegment(x, y, prev_x, prev_y, c.pts[2],
                                        c.pts[3]) <= max_dist_sq)
          return true;
        prev_x = c.pts[2];
        prev_y = c.pts[3];
        has_prev = true;
        break;
      case 3:  // cubic (approx as line to end)
        if (has_prev && DistSqToSegment(x, y, prev_x, prev_y, c.pts[4],
                                        c.pts[5]) <= max_dist_sq)
          return true;
        prev_x = c.pts[4];
        prev_y = c.pts[5];
        has_prev = true;
        break;
      case 4:
        break;
    }
  }
  return false;
}

std::unique_ptr<VectorObject> PathObject::Clone() const {
  auto clone = std::make_unique<PathObject>();
  clone->stroke_color = stroke_color;
  clone->fill_color = fill_color;
  clone->stroke_width = stroke_width;
  clone->path = path;
  return clone;
}

Rect PathObject::GetBounds() const {
  return path.getBounds();
}

// ... BrushObject implementation ...

void BrushObject::Render(Canvas* canvas) const {
  if (points.empty()) return;
  Paint p = Paint::Stroke(color, width);
  if (points.size() == 1) {
    canvas->drawCircle(points[0].x, points[0].y, width * 0.5f,
                       Paint::Fill(color));
    return;
  }
  for (size_t i = 1; i < points.size(); ++i) {
    canvas->drawLine(points[i - 1].x, points[i - 1].y, points[i].x, points[i].y,
                     p);
  }
  // Round joints: draw filled circles at interior points.
  Paint fill = Paint::Fill(color);
  for (size_t i = 1; i + 1 < points.size(); ++i) {
    canvas->drawCircle(points[i].x, points[i].y, width * 0.5f, fill);
  }
}

bool BrushObject::HitTest(float x, float y, float tolerance) const {
  float max_dist = width * 0.5f + tolerance;
  float max_dist_sq = max_dist * max_dist;
  for (size_t i = 1; i < points.size(); ++i) {
    if (DistSqToSegment(x, y, points[i - 1].x, points[i - 1].y, points[i].x,
                        points[i].y) <= max_dist_sq)
      return true;
  }
  if (points.size() == 1) {
    float dx = x - points[0].x;
    float dy = y - points[0].y;
    return dx * dx + dy * dy <= max_dist_sq;
  }
  return false;
}

std::unique_ptr<VectorObject> BrushObject::Clone() const {
  auto clone = std::make_unique<BrushObject>();
  clone->stroke_color = stroke_color;
  clone->fill_color = fill_color;
  clone->stroke_width = stroke_width;
  clone->points = points;
  clone->color = color;
  clone->width = width;
  return clone;
}

Rect BrushObject::GetBounds() const {
  if (points.empty()) return Rect::MakeEmpty();
  float x0 = points[0].x, y0 = points[0].y, x1 = x0, y1 = y0;
  for (const auto& p : points) {
    x0 = std::min(x0, p.x);
    y0 = std::min(y0, p.y);
    x1 = std::max(x1, p.x);
    y1 = std::max(y1, p.y);
  }
  const float r = width * 0.5f;
  return Rect::MakeLTRB(x0 - r, y0 - r, x1 + r, y1 + r);
}

// ... TextObject implementation ...

void TextObject::Render(Canvas* canvas) const {
  if (text.empty()) return;
  Font font(typeface, font_size);
  canvas->drawString(text.c_str(), x, y, font, Paint::Fill(stroke_color));
}

bool TextObject::HitTest(float px, float py, float tolerance) const {
  Rect bounds = GetBounds();
  bounds.outset(tolerance, tolerance);
  return bounds.contains(px, py);
}

std::unique_ptr<VectorObject> TextObject::Clone() const {
  auto clone = std::make_unique<TextObject>();
  clone->stroke_color = stroke_color;
  clone->fill_color = fill_color;
  clone->stroke_width = stroke_width;
  clone->text = text;
  clone->x = x;
  clone->y = y;
  clone->font_size = font_size;
  clone->typeface = typeface;
  return clone;
}

Rect TextObject::GetBounds() const {
  if (text.empty()) return Rect::MakeXYWH(x, y, 0, 0);
  Font font(typeface, font_size);
  float w = font.measureText(text.c_str(), text.size());
  // Blend2D draws text with baseline at y; approximate bounds.
  return Rect::MakeXYWH(x, y - font_size, w, font_size);
}

// ... GroupObject implementation ...

void GroupObject::Render(Canvas* canvas) const {
  for (const auto& child : children) {
    child->Render(canvas);
  }
}

bool GroupObject::HitTest(float x, float y, float tolerance) const {
  for (int i = static_cast<int>(children.size()) - 1; i >= 0; --i) {
    if (children[i]->HitTest(x, y, tolerance)) {
      return true;
    }
  }
  return false;
}

std::unique_ptr<VectorObject> GroupObject::Clone() const {
  auto clone = std::make_unique<GroupObject>();
  clone->stroke_color = stroke_color;
  clone->fill_color = fill_color;
  clone->stroke_width = stroke_width;
  for (const auto& child : children) {
    clone->children.push_back(child->Clone());
  }
  return clone;
}

Rect GroupObject::GetBounds() const {
  if (children.empty()) return Rect::MakeEmpty();
  Rect bounds = children[0]->GetBounds();
  for (size_t i = 1; i < children.size(); ++i) {
    bounds.join(children[i]->GetBounds());
  }
  return bounds;
}

// ... VectorLayer implementation ...

VectorLayer::VectorLayer(const std::string& layer_name) {
  name = layer_name;
  type = LayerType::kVector;
}

void VectorLayer::Render(Canvas* canvas) const {
  if (opacity >= 0.999f) {
    for (const auto& obj : objects) {
      obj->Render(canvas);
    }
    return;
  }
  // Opaque path can't modulate per-object alpha afterwards, so render
  // into a temp bitmap and composite it with the layer opacity.
  Rect bounds = Rect::MakeEmpty();
  bool any = false;
  for (const auto& obj : objects) {
    const Rect b = obj->GetBounds();
    if (b.isEmpty()) continue;
    if (!any) {
      bounds = b;
      any = true;
    } else {
      bounds.join(b);
    }
  }
  if (!any) return;
  const int ox = static_cast<int>(std::floor(bounds.left));
  const int oy = static_cast<int>(std::floor(bounds.top));
  const int w = static_cast<int>(std::ceil(bounds.right)) - ox;
  const int h = static_cast<int>(std::ceil(bounds.bottom)) - oy;
  if (w <= 0 || h <= 0) return;
  Bitmap tmp;
  tmp.allocN32Pixels(w, h);
  tmp.eraseColor(kColorTransparent);
  {
    Canvas offscreen(tmp);
    Matrix shift;
    shift.setTranslate(static_cast<float>(-ox), static_cast<float>(-oy));
    offscreen.concat(canvas->getTotalMatrix());
    offscreen.concat(shift);
    for (const auto& obj : objects) {
      obj->Render(&offscreen);
    }
  }
  canvas->drawImage(tmp, static_cast<float>(ox), static_cast<float>(oy),
                    opacity);
}

std::unique_ptr<Layer> VectorLayer::Clone() const {
  auto clone = std::make_unique<VectorLayer>();
  clone->name = name;
  clone->type = type;
  clone->visible = visible;
  clone->locked = locked;
  clone->opacity = opacity;
  for (const auto& obj : objects) {
    clone->objects.push_back(obj->Clone());
  }
  return clone;
}

const VectorObject* VectorLayer::HitTest(float x, float y,
                                         float tolerance) const {
  for (int i = static_cast<int>(objects.size()) - 1; i >= 0; --i) {
    if (objects[i]->HitTest(x, y, tolerance)) {
      return objects[i].get();
    }
  }
  return nullptr;
}

// ... LayerStack implementation ...

LayerStack::LayerStack() {
  AddRasterLayer("Background", 1000, 1000);
}

void LayerStack::AddRasterLayer(const std::string& name, int width,
                                int height) {
  layers_.push_back(std::make_unique<RasterLayer>(width, height, name));
  active_layer_index_ = static_cast<int>(layers_.size()) - 1;
}

void LayerStack::AddVectorLayer(const std::string& name) {
  layers_.push_back(std::make_unique<VectorLayer>(name));
  active_layer_index_ = static_cast<int>(layers_.size()) - 1;
}

void LayerStack::DeleteLayer(int index) {
  if (index >= 0 && index < static_cast<int>(layers_.size())) {
    layers_.erase(layers_.begin() + index);
    if (layers_.empty()) {
      active_layer_index_ = 0;
    } else {
      active_layer_index_ = std::clamp(active_layer_index_, 0,
                                       static_cast<int>(layers_.size()) - 1);
    }
  }
}

namespace {

Rect LayerBounds(const Layer* layer) {
  if (layer == nullptr) return Rect::MakeEmpty();
  if (layer->type == LayerType::kRaster) {
    const auto* raster = static_cast<const RasterLayer*>(layer);
    if (raster->bitmap.empty()) return Rect::MakeEmpty();
    return Rect::MakeXYWH(static_cast<float>(raster->offset_x),
                          static_cast<float>(raster->offset_y),
                          static_cast<float>(raster->bitmap.width()),
                          static_cast<float>(raster->bitmap.height()));
  }
  const auto* vector = static_cast<const VectorLayer*>(layer);
  if (vector->objects.empty()) return Rect::MakeEmpty();
  Rect bounds = vector->objects[0]->GetBounds();
  for (size_t i = 1; i < vector->objects.size(); ++i) {
    bounds.join(vector->objects[i]->GetBounds());
  }
  return bounds;
}

}  // namespace

void LayerStack::Flatten() {
  Rect bounds = Rect::MakeEmpty();
  bool any = false;
  for (const auto& layer : layers_) {
    if (!layer->visible) continue;
    const Rect b = LayerBounds(layer.get());
    if (b.isEmpty()) continue;
    if (!any) {
      bounds = b;
      any = true;
    } else {
      bounds.join(b);
    }
  }
  if (!any) {
    return;
  }
  const int ox = static_cast<int>(std::floor(bounds.left));
  const int oy = static_cast<int>(std::floor(bounds.top));
  const int w = static_cast<int>(std::ceil(bounds.right)) - ox;
  const int h = static_cast<int>(std::ceil(bounds.bottom)) - oy;
  if (w <= 0 || h <= 0) {
    return;
  }
  Bitmap bitmap;
  bitmap.allocN32Pixels(w, h);
  bitmap.eraseColor(kColorTransparent);
  {
    Canvas canvas(bitmap);
    canvas.translate(static_cast<float>(-ox), static_cast<float>(-oy));
    for (const auto& layer : layers_) {
      if (layer->visible) {
        layer->Render(&canvas);
      }
    }
  }
  layers_.clear();
  auto flat = std::make_unique<RasterLayer>();
  flat->name = "Flattened";
  flat->bitmap = bitmap;
  flat->offset_x = ox;
  flat->offset_y = oy;
  layers_.push_back(std::move(flat));
  active_layer_index_ = 0;
  selection_.Clear();
}

void LayerStack::SetActiveLayerIndex(int index) {
  if (index >= 0 && index < static_cast<int>(layers_.size())) {
    active_layer_index_ = index;
  }
}

void LayerStack::Clear() {
  layers_.clear();
  active_layer_index_ = 0;
  selection_.Clear();
}

char LayerStack::GetLayerKeybinding(int index) const {
  static const char* kKeys = "zxcvbnm";
  int n = static_cast<int>(layers_.size());
  int key_idx = n - 1 - index;
  if (key_idx >= 0 && key_idx < 7) {
    return kKeys[key_idx];
  }
  return '?';
}

}  // namespace nanote
