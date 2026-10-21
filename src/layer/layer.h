#pragma once

#include "core/bitmap.h"
#include "core/color.h"
#include "core/font.h"
#include "core/geometry.h"
#include "core/path.h"
#include "core/types.h"

#include "core/selection.h"

#include <memory>
#include <string>
#include <vector>

namespace nanote {

enum class LayerType { kRaster, kVector };

class Canvas;

class Layer {
 public:
  virtual ~Layer() = default;

  virtual void Render(Canvas* canvas) const = 0;
  virtual std::unique_ptr<Layer> Clone() const = 0;

  std::string name;
  LayerType type = LayerType::kRaster;
  bool visible = true;
  bool locked = false;
  float opacity = 1.0f;
};

class RasterLayer : public Layer {
 public:
  RasterLayer() = default;
  RasterLayer(int width, int height, const std::string& layer_name);

  void Render(Canvas* canvas) const override;
  std::unique_ptr<Layer> Clone() const override;

  // Ensure the bitmap can contain the given canvas-space point.
  // Expands and re-centers the bitmap if necessary.
  void EnsureContains(float x, float y);

  Bitmap bitmap;
  // Canvas coordinate of the top-left corner of the bitmap.
  int offset_x = 0;
  int offset_y = 0;
};

// ------------------------------------------------------------------
// Vector objects
// ------------------------------------------------------------------

class VectorObject {
 public:
  virtual ~VectorObject() = default;

  virtual void Render(Canvas* canvas) const = 0;
  virtual bool HitTest(float x, float y, float tolerance) const = 0;
  virtual std::unique_ptr<VectorObject> Clone() const = 0;
  virtual Rect GetBounds() const = 0;

  Color stroke_color = kColorBlack;
  Color fill_color = kColorTransparent;
  float stroke_width = 2.0f;
};

class PathObject : public VectorObject {
 public:
  Path path;

  void Render(Canvas* canvas) const override;
  bool HitTest(float x, float y, float tolerance) const override;
  std::unique_ptr<VectorObject> Clone() const override;
  Rect GetBounds() const override;
};

// Freehand brush stroke stored as a polyline.
// Stored as a polyline with uniform width; rendered with round caps.
class BrushObject : public VectorObject {
 public:
  std::vector<Vec2> points;
  Color color = kColorBlack;
  float width = 3.0f;

  void Render(Canvas* canvas) const override;
  bool HitTest(float x, float y, float tolerance) const override;
  std::unique_ptr<VectorObject> Clone() const override;
  Rect GetBounds() const override;
};

class TextObject : public VectorObject {
 public:
  std::string text;
  float x = 0.0f;
  float y = 0.0f;
  float font_size = 16.0f;
  std::shared_ptr<Typeface> typeface;

  void Render(Canvas* canvas) const override;
  bool HitTest(float px, float py, float tolerance) const override;
  std::unique_ptr<VectorObject> Clone() const override;
  Rect GetBounds() const override;
};

class GroupObject : public VectorObject {
 public:
  std::vector<std::unique_ptr<VectorObject>> children;

  void Render(Canvas* canvas) const override;
  bool HitTest(float x, float y, float tolerance) const override;
  std::unique_ptr<VectorObject> Clone() const override;
  Rect GetBounds() const override;
};

// ------------------------------------------------------------------
// Vector layer
// ------------------------------------------------------------------

class VectorLayer : public Layer {
 public:
  VectorLayer() = default;
  explicit VectorLayer(const std::string& layer_name);

  void Render(Canvas* canvas) const override;
  std::unique_ptr<Layer> Clone() const override;

  // Returns the top-most hit object or nullptr.
  [[nodiscard]] const VectorObject* HitTest(float x, float y,
                                            float tolerance = 4.0f) const;

  std::vector<std::unique_ptr<VectorObject>> objects;
};

class LayerStack {
 public:
  LayerStack();

  void AddRasterLayer(const std::string& name, int width, int height);
  void AddVectorLayer(const std::string& name);
  void DeleteLayer(int index);
  void Clear();
  // Merge all visible layers into a single raster layer.
  void Flatten();

  // Replaces the whole stack (history restore). Active index is clamped.
  void ReplaceAll(std::vector<std::unique_ptr<Layer>> layers, int active);

  [[nodiscard]] const std::vector<std::unique_ptr<Layer>>& GetLayers() const {
    return layers_;
  }
  [[nodiscard]] std::vector<std::unique_ptr<Layer>>& GetLayers() {
    return layers_;
  }

  [[nodiscard]] int GetActiveLayerIndex() const {
    return active_layer_index_;
  }
  void SetActiveLayerIndex(int index);

  [[nodiscard]] const Layer* GetActiveLayer() const {
    if (active_layer_index_ >= 0 &&
        active_layer_index_ < static_cast<int>(layers_.size())) {
      return layers_[active_layer_index_].get();
    }
    return nullptr;
  }

  [[nodiscard]] char GetLayerKeybinding(int index) const;

  Selection& GetSelection() {
    return selection_;
  }
  [[nodiscard]] const Selection& GetSelection() const {
    return selection_;
  }

 private:
  std::vector<std::unique_ptr<Layer>> layers_;
  int active_layer_index_ = 0;
  Selection selection_;
};

}  // namespace nanote
