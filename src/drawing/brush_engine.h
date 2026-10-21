#pragma once

// Krita-inspired raster brush engine.
//
// Design notes (see plugins/paintops in Krita):
// - Pixel brush: procedural round tip stamped along the stroke with a
//   spacing derived from the dab diameter (KisDistanceInformation).
// - Softness: smooth falloff between a solid core and the dab edge
//   (simplified KisCircleMaskGenerator).
// - Opacity/flow: per-dab alpha multiplies the tip mask; overlapping dabs
//   build up via source-over, like Krita's flow.
// - Airbrush/spray: larger spacing with scattered particles per dab
//   (KisSprayRandomDistributions: uniform angle, sqrt-distributed radius).
// - Marker: wide soft tip with low per-dab alpha for translucent buildup.
//
// The engine stamps directly into the premultiplied BLImage pixels so both
// commit (RasterLayer bitmap) and any raw BLImage target share one path.

#include "core/color.h"
#include "core/geometry.h"
#include "core/types.h"
#include "input/input_manager.h"

#include <cstdint>
#include <random>
#include <vector>

namespace nanote {

class RasterLayer;
class Canvas;

struct SprayParams {
  bool enabled = false;
  int count = 24;       // particles per dab
  float spread = 1.0f;  // particle spread relative to dab radius
  float particle_radius = 1.5f;
  float particle_opacity = 0.9f;
};

struct DabParams {
  float radius = 1.5f;    // dab radius in canvas px
  float hardness = 0.8f;  // 0..1 (1 = hard edge)
  float opacity = 1.0f;   // per-dab alpha 0..1 (Krita "flow")
  float spacing = 0.15f;  // dab spacing as fraction of diameter
  SprayParams spray;
};

// Per-tool presets. Size is the UI tool size (diameter in px).
[[nodiscard]] DabParams ToolPreset(Tool tool, float size);
// Preset overlaid with the tool's saved menu settings.
[[nodiscard]] DabParams ResolveToolParams(Tool tool,
                                          const ToolSettings& settings);

// Spacing-walked dab centers for a polyline (Krita distance stepping).
// Always emits at least the first point, so single clicks paint one dab.
[[nodiscard]] std::vector<Vec2> ComputeDabPositions(
    const std::vector<Vec2>& points, float spacing_px);

// Deterministic spray particles for one dab (unit: canvas px offsets).
[[nodiscard]] std::vector<Vec2> SprayParticles(Vec2 center, float radius,
                                               const SprayParams& spray,
                                               std::mt19937& rng);

// Commit paths (pixel-accurate, stamped into the layer bitmap).
void PaintStrokeDabs(RasterLayer* layer, const std::vector<Vec2>& points,
                     const DabParams& params, Color color, uint32_t seed);
void EraseStrokeDabs(RasterLayer* layer, const std::vector<Vec2>& points,
                     float radius, float hardness, float spacing_frac,
                     uint32_t seed);
void ErasePointDab(RasterLayer* layer, float x, float y, float radius);

// Fill tools.
void GradientFillRaster(RasterLayer* layer, const Rect& rect, Color from,
                        Color to, bool horizontal);
void PatternFillRaster(RasterLayer* layer, const Rect& rect, Color color);

// Ephemeral preview path (vector circles approximating the same dabs).
void PreviewStrokeDabs(Canvas* canvas, const std::vector<Vec2>& points,
                       const DabParams& params, Color color, uint32_t seed,
                       bool erase);

}  // namespace nanote
