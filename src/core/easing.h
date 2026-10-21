#pragma once

#include "core/geometry.h"

#include <algorithm>

namespace nanote {

inline float EaseOutCubic(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  const float u = 1.0f - t;
  return 1.0f - u * u * u;
}

inline float EaseInOutCubic(float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  if (t < 0.5f) return 4.0f * t * t * t;
  const float u = 1.0f - t;
  return 1.0f - 4.0f * u * u * u;
}

inline Rect LerpRect(const Rect& a, const Rect& b, float t) {
  t = std::clamp(t, 0.0f, 1.0f);
  return Rect::MakeLTRB(
      a.left + (b.left - a.left) * t, a.top + (b.top - a.top) * t,
      a.right + (b.right - a.right) * t, a.bottom + (b.bottom - a.bottom) * t);
}

}  // namespace nanote
