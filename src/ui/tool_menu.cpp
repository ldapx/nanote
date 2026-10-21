#include "ui/tool_menu.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace nanote {

namespace {

constexpr float kPanelWidth = 360.0f;
constexpr float kPadding = 14.0f;
constexpr float kTitleHeight = 36.0f;
constexpr float kRowHeight = 34.0f;
constexpr float kFooterHeight = 30.0f;
constexpr float kLabelWidth = 150.0f;
constexpr float kValueWidth = 70.0f;

float GetSetting(const ToolSettings& s, ToolSettingId id) {
  switch (id) {
    case ToolSettingId::kSize:
      return s.size;
    case ToolSettingId::kHardness:
      return s.hardness;
    case ToolSettingId::kFlow:
      return s.flow;
    case ToolSettingId::kSpacing:
      return s.spacing;
    case ToolSettingId::kSprayCount:
      return s.spray_count;
    case ToolSettingId::kSpraySpread:
      return s.spray_spread;
    case ToolSettingId::kSprayParticle:
      return s.spray_particle;
    case ToolSettingId::kTolerance:
      return s.tolerance;
    case ToolSettingId::kStrokeWidth:
      return s.stroke_width;
    case ToolSettingId::kFontSize:
      return s.font_size;
  }
  return 0.0f;
}

std::string FormatValue(ToolSettingId id, float v) {
  char buf[48];
  switch (id) {
    case ToolSettingId::kSize:
    case ToolSettingId::kStrokeWidth:
    case ToolSettingId::kFontSize:
      std::snprintf(buf, sizeof(buf), "%.1f px", v);
      return buf;
    case ToolSettingId::kSprayParticle:
      if (v <= 0.0f) return "Auto";
      std::snprintf(buf, sizeof(buf), "%.1f px", v);
      return buf;
    case ToolSettingId::kHardness:
    case ToolSettingId::kFlow:
    case ToolSettingId::kSpacing:
    case ToolSettingId::kSpraySpread:
      std::snprintf(buf, sizeof(buf), "%d%%",
                    static_cast<int>(std::round(v * 100.0f)));
      return buf;
    case ToolSettingId::kSprayCount:
      if (v <= 0.5f) return "Auto";
      std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::round(v)));
      return buf;
    case ToolSettingId::kTolerance:
      std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::round(v)));
      return buf;
  }
  return "";
}

void AddSlider(std::vector<ToolMenuRow>& rows, const char* label,
               ToolSettingId id, const ToolSettings& s, float lo, float hi,
               float step) {
  ToolMenuRow r;
  r.label = label;
  r.id = id;
  r.adjustable = true;
  r.min = lo;
  r.max = hi;
  r.step = step;
  r.value_text = FormatValue(id, GetSetting(s, id));
  rows.push_back(r);
}

void AddInfo(std::vector<ToolMenuRow>& rows, const char* text) {
  ToolMenuRow r;
  r.label = text;
  r.adjustable = false;
  rows.push_back(r);
}

}  // namespace

std::vector<ToolMenuRow> BuildToolMenuRows(Tool tool,
                                           const ToolSettings& settings) {
  std::vector<ToolMenuRow> rows;
  switch (tool) {
    case Tool::kPencil:
    case Tool::kPen:
    case Tool::kBrush:
    case Tool::kMarker:
      AddSlider(rows, "Size", ToolSettingId::kSize, settings, 1.0f, 60.0f,
                0.5f);
      AddSlider(rows, "Hardness", ToolSettingId::kHardness, settings, 0.0f,
                1.0f, 0.05f);
      AddSlider(rows, "Flow", ToolSettingId::kFlow, settings, 0.05f, 1.0f,
                0.05f);
      AddSlider(rows, "Spacing", ToolSettingId::kSpacing, settings, 0.05f, 1.0f,
                0.05f);
      break;
    case Tool::kSpray:
      AddSlider(rows, "Size", ToolSettingId::kSize, settings, 2.0f, 120.0f,
                1.0f);
      AddSlider(rows, "Particles", ToolSettingId::kSprayCount, settings, 0.0f,
                400.0f, 1.0f);
      AddSlider(rows, "Spread", ToolSettingId::kSpraySpread, settings, 0.2f,
                3.0f, 0.1f);
      AddSlider(rows, "Dot size", ToolSettingId::kSprayParticle, settings, 0.0f,
                8.0f, 0.5f);
      break;
    case Tool::kStroke:
      AddSlider(rows, "Size", ToolSettingId::kSize, settings, 1.0f, 100.0f,
                1.0f);
      AddSlider(rows, "Hardness", ToolSettingId::kHardness, settings, 0.0f,
                1.0f, 0.05f);
      AddSlider(rows, "Spacing", ToolSettingId::kSpacing, settings, 0.05f, 1.0f,
                0.05f);
      break;
    case Tool::kPoint:
      AddSlider(rows, "Size", ToolSettingId::kSize, settings, 1.0f, 100.0f,
                1.0f);
      break;
    case Tool::kLine:
    case Tool::kRectangle:
    case Tool::kCircle:
    case Tool::kEllipse:
    case Tool::kPolygon:
    case Tool::kStar:
    case Tool::kArc:
    case Tool::kBezier:
      AddSlider(rows, "Stroke width", ToolSettingId::kStrokeWidth, settings,
                0.5f, 32.0f, 0.5f);
      break;
    case Tool::kSingleLine:
    case Tool::kParagraph:
    case Tool::kPathText:
      AddSlider(rows, "Font size", ToolSettingId::kFontSize, settings, 4.0f,
                96.0f, 1.0f);
      break;
    case Tool::kSolid:
      AddSlider(rows, "Tolerance", ToolSettingId::kTolerance, settings, 0.0f,
                128.0f, 1.0f);
      break;
    case Tool::kGradient:
    case Tool::kPattern:
      AddSlider(rows, "Area size", ToolSettingId::kSize, settings, 1.0f, 200.0f,
                1.0f);
      break;
    default:
      AddInfo(rows, "No adjustable settings");
      break;
  }
  return rows;
}

bool ToolMenuRowAdjustable(Tool tool, int row) {
  // Rebuilt without settings: adjustability doesn't depend on values.
  ToolSettings defaults;
  const auto rows = BuildToolMenuRows(tool, defaults);
  return row >= 0 && row < static_cast<int>(rows.size()) &&
         rows[row].adjustable;
}

int ToolMenuRowCount(Tool tool) {
  ToolSettings defaults;
  return static_cast<int>(BuildToolMenuRows(tool, defaults).size());
}

float ToolMenuRowT(Tool tool, const ToolSettings& settings, int row) {
  const auto rows = BuildToolMenuRows(tool, settings);
  if (row < 0 || row >= static_cast<int>(rows.size())) return 0.0f;
  const auto& r = rows[row];
  if (!r.adjustable || r.max <= r.min) return 0.0f;
  return std::clamp((GetSetting(settings, r.id) - r.min) / (r.max - r.min),
                    0.0f, 1.0f);
}

float ApplyToolMenuRowT(InputManager& input, Tool tool, int row, float t) {
  const ToolSettings& cur = input.GetToolSettings(tool);
  const auto rows = BuildToolMenuRows(tool, cur);
  if (row < 0 || row >= static_cast<int>(rows.size())) return 0.0f;
  const auto& r = rows[row];
  if (!r.adjustable) return 0.0f;
  float v = r.min + std::clamp(t, 0.0f, 1.0f) * (r.max - r.min);
  v = r.min + std::round((v - r.min) / r.step) * r.step;
  v = std::clamp(v, r.min, r.max);
  // Snap spray "auto" zone to exactly 0.
  if ((r.id == ToolSettingId::kSprayCount ||
       r.id == ToolSettingId::kSprayParticle) &&
      v < r.step * 0.5f) {
    v = 0.0f;
  }
  input.SetToolSetting(tool, r.id, v);
  return v;
}

void StepToolMenuRow(InputManager& input, Tool tool, int row, int dir) {
  const ToolSettings& cur = input.GetToolSettings(tool);
  const auto rows = BuildToolMenuRows(tool, cur);
  if (row < 0 || row >= static_cast<int>(rows.size())) return;
  const auto& r = rows[row];
  if (!r.adjustable) return;
  float v = GetSetting(cur, r.id) + dir * r.step;
  v = std::clamp(v, r.min, r.max);
  input.SetToolSetting(tool, r.id, v);
}

ToolMenuLayout ComputeToolMenuLayout(int window_w, int window_h,
                                     int row_count) {
  ToolMenuLayout layout;
  const float panel_h = kPadding * 2 + kTitleHeight + row_count * kRowHeight +
                        10.0f + kFooterHeight;
  layout.panel =
      Rect::MakeXYWH((window_w - kPanelWidth) * 0.5f,
                     (window_h - panel_h) * 0.5f, kPanelWidth, panel_h);
  for (int i = 0; i < row_count; ++i) {
    const float ry =
        layout.panel.top + kPadding + kTitleHeight + i * kRowHeight;
    Rect row = Rect::MakeXYWH(layout.panel.left + kPadding, ry,
                              kPanelWidth - kPadding * 2, kRowHeight);
    layout.rows.push_back(row);
    const float tx0 = row.left + kLabelWidth;
    const float tx1 = row.right - kValueWidth;
    layout.sliders.push_back(
        Rect::MakeLTRB(tx0, row.top + kRowHeight * 0.5f - 3.0f, tx1,
                       row.top + kRowHeight * 0.5f + 3.0f));
  }
  return layout;
}

int ToolMenuHitRow(const ToolMenuLayout& layout, float mx, float my) {
  for (int i = 0; i < static_cast<int>(layout.rows.size()); ++i) {
    if (layout.rows[i].contains(mx, my)) return i;
  }
  return -1;
}

float ToolMenuSliderT(const ToolMenuLayout& layout, int row, float mx) {
  if (row < 0 || row >= static_cast<int>(layout.sliders.size())) return 0.0f;
  const Rect& s = layout.sliders[row];
  if (s.width() <= 0.0f) return 0.0f;
  return std::clamp((mx - s.left) / s.width(), 0.0f, 1.0f);
}

}  // namespace nanote
