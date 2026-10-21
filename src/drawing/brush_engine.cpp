#include "drawing/brush_engine.h"

#include "layer/layer.h"
#include "render/canvas.h"

#include <algorithm>
#include <cmath>

namespace nanote {

namespace {

// Smoothstep helper.
float SmoothStep(float e0, float e1, float x) {
  float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

// Procedural round tip (simplified KisCircleMaskGenerator):
// solid core out to `hardness`, smooth ramp to the dab edge.
float TipAlpha(float dx, float dy, float radius, float hardness) {
  if (radius <= 0.0f) return 0.0f;
  const float d = std::hypot(dx, dy) / radius;
  if (d >= 1.0f) return 0.0f;
  const float core = std::clamp(hardness, 0.0f, 1.0f) * 0.95f;
  return 1.0f - SmoothStep(core, 1.0f, d);
}

// Raw premultiplied pixel access (one lock per stroke, not per pixel).
struct RawSurface {
  uint32_t* pixels = nullptr;
  int stride = 0;  // in pixels
  int w = 0;
  int h = 0;
  int ox = 0;  // canvas coordinate of bitmap (0,0)
  int oy = 0;
};

bool LockSurface(Bitmap& bmp, int ox, int oy, RawSurface* out) {
  BLImageData data;
  if (bmp.image().make_mutable(&data) != BL_SUCCESS) return false;
  out->pixels = static_cast<uint32_t*>(data.pixel_data);
  out->stride = static_cast<int>(data.stride / 4);
  out->w = bmp.width();
  out->h = bmp.height();
  out->ox = ox;
  out->oy = oy;
  return true;
}

// Source-over of a non-premultiplied src color with effective alpha a.
void BlendSrcOver(uint32_t* dst, uint8_t r, uint8_t g, uint8_t b, float a) {
  if (a <= 0.0f) return;
  if (a > 1.0f) a = 1.0f;
  const uint32_t d = *dst;
  const uint8_t da = static_cast<uint8_t>((d >> 24) & 0xFF);
  const uint8_t dr = static_cast<uint8_t>((d >> 16) & 0xFF);
  const uint8_t dg = static_cast<uint8_t>((d >> 8) & 0xFF);
  const uint8_t db = static_cast<uint8_t>(d & 0xFF);

  const float sa = a;
  const float sr = r * a;
  const float sg = g * a;
  const float sb = b * a;
  const float ia = 1.0f - sa;
  const uint8_t oa =
      static_cast<uint8_t>(std::min(255.0f, sa * 255.0f + da * ia));
  const uint8_t orr = static_cast<uint8_t>(std::min(255.0f, sr + dr * ia));
  const uint8_t og = static_cast<uint8_t>(std::min(255.0f, sg + dg * ia));
  const uint8_t ob = static_cast<uint8_t>(std::min(255.0f, sb + db * ia));
  *dst = (static_cast<uint32_t>(oa) << 24) |
         (static_cast<uint32_t>(orr) << 16) | (static_cast<uint32_t>(og) << 8) |
         ob;
}

// Destination-out (erase to transparent).
void BlendDstOut(uint32_t* dst, float a) {
  if (a <= 0.0f) return;
  if (a > 1.0f) a = 1.0f;
  const uint32_t d = *dst;
  const float ia = 1.0f - a;
  const uint8_t oa = static_cast<uint8_t>(((d >> 24) & 0xFF) * ia);
  const uint8_t orr = static_cast<uint8_t>(((d >> 16) & 0xFF) * ia);
  const uint8_t og = static_cast<uint8_t>(((d >> 8) & 0xFF) * ia);
  const uint8_t ob = static_cast<uint8_t>((d & 0xFF) * ia);
  *dst = (static_cast<uint32_t>(oa) << 24) |
         (static_cast<uint32_t>(orr) << 16) | (static_cast<uint32_t>(og) << 8) |
         ob;
}

void StampDab(RawSurface* s, float cx, float cy, float radius, float hardness,
              float opacity, uint8_t r, uint8_t g, uint8_t b, float src_a) {
  if (radius <= 0.0f || opacity <= 0.0f) return;
  const int x0 = static_cast<int>(std::floor(cx - radius - s->ox));
  const int y0 = static_cast<int>(std::floor(cy - radius - s->oy));
  const int x1 = static_cast<int>(std::ceil(cx + radius - s->ox));
  const int y1 = static_cast<int>(std::ceil(cy + radius - s->oy));
  for (int y = std::max(0, y0); y <= std::min(s->h - 1, y1); ++y) {
    for (int x = std::max(0, x0); x <= std::min(s->w - 1, x1); ++x) {
      const float a =
          TipAlpha(x + s->ox - cx, y + s->oy - cy, radius, hardness) * opacity *
          src_a;
      if (a > 1.0f / 255.0f) {
        BlendSrcOver(&s->pixels[y * s->stride + x], r, g, b, a);
      }
    }
  }
}

void StampEraseDab(RawSurface* s, float cx, float cy, float radius,
                   float hardness, float opacity) {
  if (radius <= 0.0f || opacity <= 0.0f) return;
  const int x0 = static_cast<int>(std::floor(cx - radius - s->ox));
  const int y0 = static_cast<int>(std::floor(cy - radius - s->oy));
  const int x1 = static_cast<int>(std::ceil(cx + radius - s->ox));
  const int y1 = static_cast<int>(std::ceil(cy + radius - s->oy));
  for (int y = std::max(0, y0); y <= std::min(s->h - 1, y1); ++y) {
    for (int x = std::max(0, x0); x <= std::min(s->w - 1, x1); ++x) {
      const float a =
          TipAlpha(x + s->ox - cx, y + s->oy - cy, radius, hardness) * opacity;
      if (a > 1.0f / 255.0f) {
        BlendDstOut(&s->pixels[y * s->stride + x], a);
      }
    }
  }
}

}  // namespace

DabParams ToolPreset(Tool tool, float size) {
  const float diameter = std::max(1.0f, size);
  DabParams p;
  p.radius = diameter * 0.5f;
  switch (tool) {
    case Tool::kPencil:
      p.hardness = 1.0f;
      p.opacity = 1.0f;
      p.spacing = 0.15f;
      break;
    case Tool::kPen:
      p.hardness = 0.85f;
      p.opacity = 1.0f;
      p.spacing = 0.12f;
      break;
    case Tool::kBrush:
      p.hardness = 0.35f;
      p.opacity = 0.4f;  // flow buildup over overlapping dabs
      p.spacing = 0.1f;
      break;
    case Tool::kMarker:
      p.hardness = 0.25f;
      p.opacity = 0.18f;
      p.spacing = 0.08f;
      break;
    case Tool::kSpray: {
      p.hardness = 1.0f;
      p.opacity = 1.0f;
      p.spacing = 0.5f;
      p.spray.enabled = true;
      p.spray.spread = 1.0f;
      p.spray.particle_radius = std::max(1.0f, diameter * 0.06f);
      p.spray.particle_opacity = 0.9f;
      // Density-proportional count (Krita coverage mode), clamped.
      p.spray.count = std::clamp(
          static_cast<int>(3.14159265f * p.radius * p.radius * 0.12f), 4, 400);
      break;
    }
    default:
      p.hardness = 0.85f;
      p.opacity = 1.0f;
      p.spacing = 0.12f;
      break;
  }
  return p;
}

DabParams ResolveToolParams(Tool tool, const ToolSettings& s) {
  DabParams p = ToolPreset(tool, s.size);
  p.radius = std::max(0.5f, s.size * 0.5f);
  p.hardness = std::clamp(s.hardness, 0.0f, 1.0f);
  p.opacity = std::clamp(s.flow, 0.01f, 1.0f);
  p.spacing = std::clamp(s.spacing, 0.01f, 2.0f);
  if (p.spray.enabled) {
    if (s.spray_count >= 0.0f) {
      p.spray.count =
          std::clamp(static_cast<int>(std::round(s.spray_count)), 0, 2000);
    }
    p.spray.spread = std::clamp(s.spray_spread, 0.05f, 5.0f);
    if (s.spray_particle >= 0.0f) {
      p.spray.particle_radius = std::max(0.5f, s.spray_particle);
    }
  }
  return p;
}

std::vector<Vec2> ComputeDabPositions(const std::vector<Vec2>& points,
                                      float spacing_px) {
  std::vector<Vec2> dabs;
  if (points.empty()) return dabs;
  dabs.reserve(points.size());
  dabs.push_back(points[0]);
  const float spacing = std::max(0.5f, spacing_px);
  float acc = 0.0f;
  Vec2 cur = points[0];
  for (size_t i = 1; i < points.size(); ++i) {
    Vec2 target = points[i];
    float seg = std::hypot(target.x - cur.x, target.y - cur.y);
    if (seg <= 0.0f) continue;
    while (acc + seg >= spacing) {
      const float t = (spacing - acc) / seg;
      cur = {cur.x + (target.x - cur.x) * t, cur.y + (target.y - cur.y) * t};
      dabs.push_back(cur);
      seg = std::hypot(target.x - cur.x, target.y - cur.y);
      acc = 0.0f;
      if (seg <= 0.0f) break;
    }
    acc += seg;
    cur = target;
  }
  return dabs;
}

std::vector<Vec2> SprayParticles(Vec2 center, float radius,
                                 const SprayParams& spray, std::mt19937& rng) {
  std::vector<Vec2> out;
  if (!spray.enabled || spray.count <= 0) return out;
  out.reserve(static_cast<size_t>(spray.count));
  std::uniform_real_distribution<float> unit(0.0f, 1.0f);
  constexpr float kPi = 3.14159265f;
  const float r = std::max(0.5f, radius * spray.spread);
  for (int i = 0; i < spray.count; ++i) {
    // Uniform angle + sqrt-distributed radius = uniform disc.
    const float theta = unit(rng) * 2.0f * kPi;
    const float rho = std::sqrt(unit(rng)) * r;
    out.push_back(
        {center.x + std::cos(theta) * rho, center.y + std::sin(theta) * rho});
  }
  return out;
}

void PaintStrokeDabs(RasterLayer* layer, const std::vector<Vec2>& points,
                     const DabParams& params, Color color, uint32_t seed) {
  if (layer == nullptr || points.empty()) return;
  for (const auto& p : points) layer->EnsureContains(p.x, p.y);
  const float spacing_px = params.spacing * params.radius * 2.0f;
  const std::vector<Vec2> dabs = ComputeDabPositions(points, spacing_px);

  RawSurface s;
  if (!LockSurface(layer->bitmap, layer->offset_x, layer->offset_y, &s)) {
    return;
  }
  std::mt19937 rng(seed);
  const uint8_t r = GetR(color);
  const uint8_t g = GetG(color);
  const uint8_t b = GetB(color);
  const float src_a = GetAf(color);
  for (const Vec2& d : dabs) {
    if (params.spray.enabled) {
      for (const Vec2& pt :
           SprayParticles(d, params.radius, params.spray, rng)) {
        StampDab(&s, pt.x, pt.y, params.spray.particle_radius, 1.0f,
                 params.spray.particle_opacity, r, g, b, src_a);
      }
    } else {
      StampDab(&s, d.x, d.y, params.radius, params.hardness, params.opacity, r,
               g, b, src_a);
    }
  }
}

void EraseStrokeDabs(RasterLayer* layer, const std::vector<Vec2>& points,
                     float radius, float hardness, float spacing_frac,
                     uint32_t seed) {
  if (layer == nullptr || points.empty() || radius <= 0.0f) return;
  (void)seed;
  const float spacing_px = std::max(0.15f, spacing_frac) * radius * 2.0f;
  const std::vector<Vec2> dabs = ComputeDabPositions(points, spacing_px);
  RawSurface s;
  if (!LockSurface(layer->bitmap, layer->offset_x, layer->offset_y, &s)) {
    return;
  }
  for (const Vec2& d : dabs) {
    StampEraseDab(&s, d.x, d.y, radius, hardness, 1.0f);
  }
}

void ErasePointDab(RasterLayer* layer, float x, float y, float radius) {
  if (layer == nullptr || radius <= 0.0f) return;
  layer->EnsureContains(x, y);
  RawSurface s;
  if (!LockSurface(layer->bitmap, layer->offset_x, layer->offset_y, &s)) {
    return;
  }
  StampEraseDab(&s, x, y, radius, 0.7f, 1.0f);
}

void GradientFillRaster(RasterLayer* layer, const Rect& rect, Color from,
                        Color to, bool horizontal) {
  if (layer == nullptr || rect.isEmpty()) return;
  layer->EnsureContains(rect.left, rect.top);
  layer->EnsureContains(rect.right, rect.bottom);
  RawSurface s;
  if (!LockSurface(layer->bitmap, layer->offset_x, layer->offset_y, &s)) {
    return;
  }
  const int x0 = std::max(0, static_cast<int>(std::floor(rect.left)) - s.ox);
  const int y0 = std::max(0, static_cast<int>(std::floor(rect.top)) - s.oy);
  const int x1 =
      std::min(s.w - 1, static_cast<int>(std::ceil(rect.right)) - s.ox - 1);
  const int y1 =
      std::min(s.h - 1, static_cast<int>(std::ceil(rect.bottom)) - s.oy - 1);
  const float span = horizontal ? rect.width() : rect.height();
  if (span <= 0.0f) return;
  const float fr = GetR(from), fg = GetG(from), fb = GetB(from),
              fa = GetA(from);
  const float tr = GetR(to), tg = GetG(to), tb = GetB(to), ta = GetA(to);
  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      const float t = horizontal ? (x + s.ox - rect.left) / span
                                 : (y + s.oy - rect.top) / span;
      const float tc = std::clamp(t, 0.0f, 1.0f);
      const uint8_t r = static_cast<uint8_t>(fr + (tr - fr) * tc);
      const uint8_t g = static_cast<uint8_t>(fg + (tg - fg) * tc);
      const uint8_t b = static_cast<uint8_t>(fb + (tb - fb) * tc);
      const float a = (fa + (ta - fa) * tc) / 255.0f;
      BlendSrcOver(&s.pixels[y * s.stride + x], r, g, b, a);
    }
  }
}

void PatternFillRaster(RasterLayer* layer, const Rect& rect, Color color) {
  if (layer == nullptr || rect.isEmpty()) return;
  layer->EnsureContains(rect.left, rect.top);
  layer->EnsureContains(rect.right, rect.bottom);
  RawSurface s;
  if (!LockSurface(layer->bitmap, layer->offset_x, layer->offset_y, &s)) {
    return;
  }
  // 8px tile: dots on a grid + faint diagonal. Alpha-modulated overlay.
  constexpr int kTile = 8;
  const uint8_t r = GetR(color);
  const uint8_t g = GetG(color);
  const uint8_t b = GetB(color);
  const float src_a = GetAf(color);
  const int x0 = std::max(0, static_cast<int>(std::floor(rect.left)) - s.ox);
  const int y0 = std::max(0, static_cast<int>(std::floor(rect.top)) - s.oy);
  const int x1 =
      std::min(s.w - 1, static_cast<int>(std::ceil(rect.right)) - s.ox - 1);
  const int y1 =
      std::min(s.h - 1, static_cast<int>(std::ceil(rect.bottom)) - s.oy - 1);
  for (int y = y0; y <= y1; ++y) {
    for (int x = x0; x <= x1; ++x) {
      if (!rect.contains(static_cast<float>(x + s.ox),
                         static_cast<float>(y + s.oy))) {
        continue;
      }
      const int tx = (x - x0) % kTile;
      const int ty = (y - y0) % kTile;
      const float dd = std::hypot(static_cast<float>(tx) - 2.0f,
                                  static_cast<float>(ty) - 2.0f);
      float a = 0.0f;
      if (dd <= 1.6f) {
        a = 0.85f;  // dot
      } else if (tx == ty) {
        a = 0.18f;  // faint diagonal
      }
      if (a > 0.0f) {
        BlendSrcOver(&s.pixels[y * s.stride + x], r, g, b, a * src_a);
      }
    }
  }
}

void PreviewStrokeDabs(Canvas* canvas, const std::vector<Vec2>& points,
                       const DabParams& params, Color color, uint32_t seed,
                       bool erase) {
  if (canvas == nullptr || points.empty()) return;
  const float spacing_px = params.spacing * params.radius * 2.0f;
  const std::vector<Vec2> dabs = ComputeDabPositions(points, spacing_px);
  std::mt19937 rng(seed);
  if (params.spray.enabled) {
    Paint dot;
    if (erase) {
      dot.clear = true;
    } else {
      dot = Paint::Fill(color);
      dot.alpha = params.spray.particle_opacity * GetAf(color);
    }
    for (const Vec2& d : dabs) {
      for (const Vec2& pt :
           SprayParticles(d, params.radius, params.spray, rng)) {
        canvas->drawCircle(pt.x, pt.y, params.spray.particle_radius, dot);
      }
    }
    return;
  }
  Paint p;
  if (erase) {
    p.clear = true;
  } else {
    p = Paint::Fill(color);
    p.alpha = params.opacity * GetAf(color);
  }
  for (const Vec2& d : dabs) {
    canvas->drawCircle(d.x, d.y, params.radius, p);
  }
}

}  // namespace nanote
