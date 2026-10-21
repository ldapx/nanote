#pragma once

#include "core/geometry.h"

#include <vector>

namespace nanote {

class VectorObject;
class VectorLayer;

struct Selection {
  bool active = false;
  Rect canvas_rect = Rect::MakeEmpty();
  // For vector layers: indices of selected objects.
  std::vector<int> object_indices;

  void Clear() {
    active = false;
    canvas_rect = Rect::MakeEmpty();
    object_indices.clear();
  }
};

}  // namespace nanote
