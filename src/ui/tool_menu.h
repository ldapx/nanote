#pragma once

// Double-press tool settings menu: data-driven rows per tool plus the
// shared centered-panel layout used by both drawing and hit-testing.

#include "core/geometry.h"
#include "input/input_manager.h"

#include <string>
#include <vector>

namespace nanote {

struct ToolMenuRow {
  std::string label;
  ToolSettingId id = ToolSettingId::kSize;  // kSize sentinel unused for info
  bool adjustable = true;
  float min = 0.0f;
  float max = 1.0f;
  float step = 0.05f;
  std::string value_text;
};

// Rows for a tool, with current values formatted. Info-only tools get a
// single non-adjustable row.
[[nodiscard]] std::vector<ToolMenuRow> BuildToolMenuRows(
    Tool tool, const ToolSettings& settings);
[[nodiscard]] bool ToolMenuRowAdjustable(Tool tool, int row);
[[nodiscard]] int ToolMenuRowCount(Tool tool);
// Normalized [0,1] slider position for an adjustable row.
[[nodiscard]] float ToolMenuRowT(Tool tool, const ToolSettings& settings,
                                 int row);
// Apply a normalized slider position; returns the snapped value.
float ApplyToolMenuRowT(InputManager& input, Tool tool, int row, float t);
// Step the focused row by dir (-1/+1).
void StepToolMenuRow(InputManager& input, Tool tool, int row, int dir);

struct ToolMenuLayout {
  Rect panel;
  std::vector<Rect> rows;
  std::vector<Rect> sliders;  // track rect per row (empty for info rows)
};

[[nodiscard]] ToolMenuLayout ComputeToolMenuLayout(int window_w, int window_h,
                                                   int row_count);
// Row index under (mx,my), or -1. Slider t in [0,1] when over a track.
[[nodiscard]] int ToolMenuHitRow(const ToolMenuLayout& layout, float mx,
                                 float my);
[[nodiscard]] float ToolMenuSliderT(const ToolMenuLayout& layout, int row,
                                    float mx);

}  // namespace nanote
