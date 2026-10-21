#include "ui/ui_renderer.h"

#include "canvas/viewport.h"
#include "core/types.h"

#include "core/color.h"
#include "core/font.h"
#include "render/canvas.h"
#include "ui/tool_menu.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace nanote {

void DrawCheckerboardRect(Canvas* canvas, float zoom, float grid_size,
                          const Rect& area) {
  if (area.isEmpty() || grid_size <= 0.0f) return;
  const float left = area.left;
  const float right = area.right;
  const float top = area.top;
  const float bottom = area.bottom;

  float start_x = std::floor(left / grid_size) * grid_size;
  float start_y = std::floor(top / grid_size) * grid_size;

  Paint paint1;
  paint1.color = MakeARGB(255, 30, 30, 30);

  Paint paint2;
  paint2.color = MakeARGB(255, 45, 45, 45);

  int start_col = static_cast<int>(std::floor(start_x / grid_size));
  int start_row = static_cast<int>(std::floor(start_y / grid_size));
  int col = 0;
  for (float x = start_x; x < right; x += grid_size) {
    int row = 0;
    for (float y = start_y; y < bottom; y += grid_size) {
      const Rect rect = Rect::MakeXYWH(x, y, grid_size, grid_size);
      canvas->drawRect(rect, ((start_col + col + start_row + row) % 2 == 0)
                                 ? paint1
                                 : paint2);
      ++row;
    }
    ++col;
  }

  Paint axis_paint;
  axis_paint.color = MakeARGB(255, 80, 80, 80);
  axis_paint.strokeWidth = 1.0f / zoom;

  canvas->drawLine(left, 0, right, 0, axis_paint);
  canvas->drawLine(0, top, 0, bottom, axis_paint);
}

void DrawCheckerboard(Canvas* canvas, float zoom, float grid_size) {
  // Determine the visible area from the canvas transform. All four window
  // corners are mapped: with a rotated view the visible region is a
  // rotated quad, and two diagonal corners alone would under-cover it.
  Matrix inv;
  if (!canvas->getTotalMatrix().invert(&inv)) {
    inv.setIdentity();
  }
  const float w = static_cast<float>(canvas->targetWidth());
  const float h = static_cast<float>(canvas->targetHeight());
  float xs[4], ys[4];
  inv.mapXY(0.0f, 0.0f, &xs[0], &ys[0]);
  inv.mapXY(w, 0.0f, &xs[1], &ys[1]);
  inv.mapXY(0.0f, h, &xs[2], &ys[2]);
  inv.mapXY(w, h, &xs[3], &ys[3]);
  float l = xs[0], t = ys[0], r = xs[0], b = ys[0];
  for (int i = 1; i < 4; ++i) {
    l = std::min(l, xs[i]);
    t = std::min(t, ys[i]);
    r = std::max(r, xs[i]);
    b = std::max(b, ys[i]);
  }

  DrawCheckerboardRect(canvas, zoom, grid_size, Rect::MakeLTRB(l, t, r, b));
}

void DrawTriangle(Canvas* canvas) {
  Paint paint;
  paint.color = MakeARGB(255, 0, 255, 255);

  Path path;
  path.moveTo(0, -100);
  path.lineTo(86.6f, 50);
  path.lineTo(-86.6f, 50);
  path.close();

  canvas->drawPath(path, paint);
}

namespace {

Color ModeStripColor(Mode mode) {
  switch (mode) {
    case Mode::kNavigate:
      return MakeARGB(255, 160, 160, 160);
    case Mode::kDraw:
      return MakeARGB(255, 66, 133, 244);
    case Mode::kShape:
      return MakeARGB(255, 52, 168, 83);
    case Mode::kRuler:
      return MakeARGB(255, 250, 188, 4);
    case Mode::kErase:
      return MakeARGB(255, 234, 67, 53);
    case Mode::kSelect:
      return MakeARGB(255, 249, 115, 22);
    case Mode::kText:
      return MakeARGB(255, 168, 85, 247);
    case Mode::kFill:
      return MakeARGB(255, 34, 211, 238);
    case Mode::kTransform:
      return MakeARGB(255, 236, 72, 153);
    case Mode::kNode:
      return MakeARGB(255, 132, 204, 22);
    case Mode::kCount:
      return MakeARGB(255, 160, 160, 160);
  }
  return MakeARGB(255, 160, 160, 160);
}

}  // namespace

std::vector<ToolbarItem> ComputeToolbarItems(
    const InputManager& input, const std::shared_ptr<Typeface>& typeface,
    int window_width) {
  std::vector<ToolbarItem> items;
  const Font font(typeface, 12);
  const float box_height = kToolbarHeight - kToolMargin * 2;
  float x = kToolMargin;
  auto append = [&](ToolbarItem item) {
    const float text_width =
        font.measureText(item.label.c_str(), item.label.size());
    const float box_width = text_width + kToolPaddingX * 2;
    item.rect = Rect::MakeXYWH(x, kToolMargin, box_width, box_height);
    items.push_back(item);
    x += box_width + kToolMargin;
  };

  if (input.IsLeaderPressed()) {
    for (Mode mode : input.GetAllModes()) {
      ToolbarItem item;
      item.kind = ToolbarItemKind::kMode;
      item.label = "[" + std::string(1, input.GetModeKeybinding(mode)) + "] " +
                   input.GetModeDisplayName(mode);
      item.selected = (mode == input.GetMode());
      item.has_strip = true;
      item.strip_color = ModeStripColor(mode);
      item.mode = mode;
      append(item);
      if (x > window_width - kToolMargin) break;
    }
  } else {
    ToolbarItem indicator;
    indicator.kind = ToolbarItemKind::kModeIndicator;
    indicator.label = "[space] mode";
    indicator.has_strip = true;
    indicator.strip_color = ModeStripColor(input.GetMode());
    append(indicator);
    if (x <= window_width - kToolMargin) {
      for (Tool tool : input.GetToolsForCurrentMode()) {
        ToolbarItem item;
        item.kind = ToolbarItemKind::kTool;
        item.label = "[" + std::string(1, input.GetToolKeybinding(tool)) +
                     "] " + input.GetToolDisplayName(tool);
        item.selected = (tool == input.GetTool());
        item.tool = tool;
        append(item);
        if (x > window_width - kToolMargin) break;
      }
    }
  }
  return items;
}

void DrawToolbar(Canvas* canvas, const InputManager& input,
                 const std::shared_ptr<Typeface>& typeface, int window_width) {
  Paint tool_bg_paint;
  tool_bg_paint.color = MakeARGB(255, 0, 0, 0);

  Paint selected_tool_bg_paint;
  selected_tool_bg_paint.color = MakeARGB(255, 60, 60, 60);

  Font font(typeface, 12);
  Paint text_paint;
  text_paint.color = MakeARGB(255, 220, 220, 220);

  constexpr float kModeStripHeight = 3.0f;
  for (const ToolbarItem& item :
       ComputeToolbarItems(input, typeface, window_width)) {
    canvas->drawRect(item.rect,
                     item.selected ? selected_tool_bg_paint : tool_bg_paint);
    if (item.has_strip) {
      Paint strip_paint;
      strip_paint.color = item.strip_color;
      canvas->drawRect(Rect::MakeXYWH(item.rect.left, item.rect.top,
                                      item.rect.width(), kModeStripHeight),
                       strip_paint);
    }
    canvas->drawString(item.label.c_str(), item.rect.left + kToolPaddingX,
                       kToolbarHeight / 2.0f + 4, font, text_paint);
  }
}

void DrawCurrentColor(Canvas* canvas, const ColorManager& color_manager,
                      const InputManager& input,
                      const std::shared_ptr<Typeface>& typeface,
                      int window_width) {
  constexpr int kBoxOuterH = 24;
  constexpr int kBoxOuterW = 84;
  constexpr int kBorder = 2;
  constexpr int kSizeBoxW = 40;

  const int x = window_width - kBoxOuterW - kToolMargin;
  const int y = kToolMargin;

  // Tool pixel size, to the left of the color box.
  const int sx = x - kToolMargin - kSizeBoxW;
  Paint size_bg_paint;
  size_bg_paint.color = MakeARGB(255, 60, 60, 60);
  canvas->drawRect(Rect::MakeXYWH(sx, y, kSizeBoxW, kBoxOuterH), size_bg_paint);

  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.0fpx", input.GetToolSize());
  Font size_font(typeface, 12);
  Paint size_text_paint;
  size_text_paint.color = MakeARGB(255, 230, 230, 230);
  const float text_w = size_font.measureText(buf, std::strlen(buf));
  const float text_x = sx + (kSizeBoxW - text_w) / 2.0f;
  canvas->drawString(buf, text_x, y + kBoxOuterH / 2.0f + 4, size_font,
                     size_text_paint);

  Paint black_paint;
  black_paint.color = MakeARGB(255, 0, 0, 0);

  Paint white_paint;
  white_paint.color = MakeARGB(255, 255, 255, 255);

  Paint color_paint;
  color_paint.color = (color_manager.GetCurrentColor());

  canvas->drawRect(Rect::MakeXYWH(x, y, kBoxOuterW, kBoxOuterH), black_paint);
  canvas->drawRect(
      Rect::MakeXYWH(x + kBorder, y + kBorder, kBoxOuterW - kBorder * 2,
                     kBoxOuterH - kBorder * 2),
      white_paint);
  canvas->drawRect(
      Rect::MakeXYWH(x + kBorder * 2, y + kBorder * 2, kBoxOuterW - kBorder * 4,
                     kBoxOuterH - kBorder * 4),
      color_paint);
}

PaletteStripGeometry ComputePaletteStripGeometry(
    const InputManager& input, const ColorManager& color_manager,
    int window_width, int window_height) {
  PaletteStripGeometry geo;
  const Palette& palette = color_manager.GetActivePalette();
  const int row_count = palette.GetRowCount();
  const int max_tone_count = palette.GetToneCount();
  if (row_count <= 0 || max_tone_count <= 0) {
    return geo;
  }
  const float t = input.GetPaletteExpansionFactor();
  geo.full_width =
      max_tone_count * kPaletteBoxOuter + (max_tone_count - 1) * kPaletteGap;
  geo.width = kPaletteBoxOuter + (geo.full_width - kPaletteBoxOuter) * t;
  geo.height = row_count * kPaletteBoxOuter + (row_count - 1) * kPaletteGap;
  geo.start_x = window_width - kPaletteColumnX - geo.width;
  const int available_height =
      window_height - kToolbarHeight - kStatusBarHeight - kCommandBarHeight;
  geo.start_y = kToolbarHeight + (available_height - geo.height) / 2;
  geo.valid = true;
  return geo;
}

void DrawPalette(Canvas* canvas, const InputManager& input,
                 const ColorManager& color_manager,
                 const std::shared_ptr<Typeface>& typeface, int window_width,
                 int window_height) {
  constexpr int kBoxWhite = 24;
  constexpr int kBoxColor = 20;

  const PaletteStripGeometry geo = ComputePaletteStripGeometry(
      input, color_manager, window_width, window_height);
  if (!geo.valid) {
    return;
  }

  const Palette& palette = color_manager.GetActivePalette();
  const int row_count = palette.GetRowCount();
  const int max_tone_count = palette.GetToneCount();

  const float t = input.GetPaletteExpansionFactor();
  const int displayed_tone = input.GetDisplayedTone();

  const int full_palette_width = geo.full_width;
  const int start_y = geo.start_y;

  Paint black_paint;
  black_paint.color = MakeARGB(255, 0, 0, 0);
  Paint white_paint;
  white_paint.color = MakeARGB(255, 255, 255, 255);
  Paint active_outline_paint;
  active_outline_paint.color = MakeARGB(255, 255, 255, 255);
  active_outline_paint.fill = false;
  active_outline_paint.strokeWidth = 2.0f;

  Font font(typeface, 9);
  Paint text_paint;
  text_paint.color = MakeARGB(255, 0, 0, 0);
  Paint text_outline_paint;
  text_outline_paint.color = MakeARGB(255, 255, 255, 255);
  text_outline_paint.fill = false;
  text_outline_paint.strokeWidth = 2;

  const char kPaletteKeys[] = {'a', 's', 'd', 'f', 'g', 'h', 'j', 'k'};
  const Font number_font(typeface, 10);

  if (t > 0.05f) {
    text_paint.alpha = t;
    text_outline_paint.alpha = t;
    for (int tone = 0; tone < max_tone_count; ++tone) {
      const int draw_tone = max_tone_count - 1 - tone;
      const float target_x =
          (window_width - kPaletteColumnX - full_palette_width) +
          draw_tone * (kPaletteBoxOuter + kPaletteGap);
      const float retracted_x =
          window_width - kPaletteColumnX - kPaletteBoxOuter;
      const float cx = retracted_x + (target_x - retracted_x) * t;

      const float label_x = cx + kPaletteBoxOuter / 2 - 3;
      const float label_y = static_cast<float>(start_y - 6);
      const std::string label = std::to_string(tone + 1);
      canvas->drawString(label.c_str(), label_x, label_y, number_font,
                         text_outline_paint);
      canvas->drawString(label.c_str(), label_x, label_y, number_font,
                         text_paint);
    }
    text_paint.alpha = 1.0f;
    text_outline_paint.alpha = 1.0f;
  }

  for (int row = 0; row < row_count; ++row) {
    const int cy = start_y + row * (kPaletteBoxOuter + kPaletteGap);
    for (int tone = 0; tone < max_tone_count; ++tone) {
      const int draw_tone = max_tone_count - 1 - tone;
      const float target_x =
          (window_width - kPaletteColumnX - full_palette_width) +
          draw_tone * (kPaletteBoxOuter + kPaletteGap);
      const float retracted_x =
          window_width - kPaletteColumnX - kPaletteBoxOuter;

      float cx, alpha;
      if (tone == displayed_tone) {
        cx = retracted_x + (target_x - retracted_x) * t;
        alpha = 1.0f;
      } else {
        cx = retracted_x + (target_x - retracted_x) * t;
        alpha = t;
      }

      if (alpha <= 0.0f) continue;

      black_paint.alpha = alpha;
      white_paint.alpha = alpha;
      active_outline_paint.alpha = alpha;

      canvas->drawRect(
          Rect::MakeXYWH(cx, cy, kPaletteBoxOuter, kPaletteBoxOuter),
          black_paint);
      if (row == palette.GetActiveRow() && tone == palette.GetActiveTone()) {
        canvas->drawRect(Rect::MakeXYWH(cx - 1, cy - 1, kPaletteBoxOuter + 2,
                                        kPaletteBoxOuter + 2),
                         active_outline_paint);
      }

      const int white_offset = (kPaletteBoxOuter - kBoxWhite) / 2;
      canvas->drawRect(Rect::MakeXYWH(cx + white_offset, cy + white_offset,
                                      kBoxWhite, kBoxWhite),
                       white_paint);

      Paint color_paint;
      color_paint.color = (palette.GetColor(row, tone));
      color_paint.alpha = alpha;
      const int color_offset = (kPaletteBoxOuter - kBoxColor) / 2;
      canvas->drawRect(Rect::MakeXYWH(cx + color_offset, cy + color_offset,
                                      kBoxColor, kBoxColor),
                       color_paint);

      if (tone == displayed_tone &&
          row < static_cast<int>(sizeof(kPaletteKeys))) {
        const std::string label = std::string("[") + kPaletteKeys[row] + "]";
        const float text_width = font.measureText(label.c_str(), label.size());
        const float text_x = cx + (kPaletteBoxOuter - text_width) / 2.0f;
        const float text_y = cy + kPaletteBoxOuter / 2.0f + 3;

        text_outline_paint.alpha = 1.0f;
        text_paint.alpha = 1.0f;
        canvas->drawString(label.c_str(), text_x, text_y, font,
                           text_outline_paint);
        canvas->drawString(label.c_str(), text_x, text_y, font, text_paint);
      }
    }
  }
}

void DrawPaletteBrowser(Canvas* canvas, const InputManager& input,
                        const ColorManager& color_manager,
                        const std::shared_ptr<Typeface>& typeface,
                        int window_width, int window_height) {
  if (!input.IsPaletteBrowserOpen()) {
    return;
  }

  constexpr int kPanelPadding = 16;
  constexpr int kRowGap = 10;
  constexpr int kCollapsedCell = 12;
  constexpr int kExpandedCell = 18;
  constexpr int kCellGap = 4;
  constexpr int kHeaderHeight = 24;
  constexpr int kDeleteSize = 16;

  Paint scrim_paint;
  scrim_paint.color = MakeARGB(170, 10, 10, 10);
  canvas->drawRect(Rect::MakeXYWH(0, 0, window_width, window_height),
                   scrim_paint);

  const int panel_width = std::min(window_width - 80, 680);
  const auto& palettes = color_manager.GetPalettes();

  std::vector<int> row_heights;
  int total_rows_height = 0;
  for (const auto& named_palette : palettes) {
    const bool expanded =
        input.GetPaletteBrowserExpandedName() == named_palette.name;
    const int cell_size = expanded ? kExpandedCell : kCollapsedCell;
    const int row_count = named_palette.palette.GetRowCount();

    int swatch_height;
    if (expanded) {
      swatch_height = row_count * cell_size + (row_count - 1) * kCellGap;
    } else {
      // Collapsed: show default tone column as a horizontal row.
      swatch_height = cell_size;
    }

    const int row_height = expanded ? kHeaderHeight + swatch_height + 12
                                    : std::max(32, swatch_height + 12);
    row_heights.push_back(row_height);
    total_rows_height += row_height;
  }
  if (!row_heights.empty()) {
    total_rows_height += static_cast<int>(row_heights.size() - 1) * kRowGap;
  }

  const int panel_height = total_rows_height + kPanelPadding * 2;
  const int panel_x = (window_width - panel_width) / 2;
  const int panel_y = std::max(40, (window_height - panel_height) / 2);

  Paint panel_paint;
  panel_paint.color = MakeARGB(245, 28, 28, 28);
  canvas->drawRect(Rect::MakeXYWH(panel_x, panel_y, panel_width, panel_height),
                   panel_paint);

  Font title_font(typeface, 13);
  Font body_font(typeface, 12);
  Paint title_paint;
  title_paint.color = MakeARGB(255, 230, 230, 230);
  Paint row_paint;
  row_paint.color = MakeARGB(255, 45, 45, 45);
  Paint expanded_row_paint;
  expanded_row_paint.color = MakeARGB(255, 60, 60, 60);
  Paint text_paint;
  text_paint.color = MakeARGB(255, 220, 220, 220);
  Paint delete_paint;
  delete_paint.color = MakeARGB(255, 160, 70, 70);
  Paint delete_text_paint;
  delete_text_paint.color = kColorWhite;
  Paint cell_border_paint;
  cell_border_paint.color = kColorBlack;

  int row_y = panel_y + kPanelPadding;
  for (size_t i = 0; i < palettes.size(); ++i) {
    const auto& named_palette = palettes[i];
    const Palette& palette = named_palette.palette;
    const bool expanded =
        input.GetPaletteBrowserExpandedName() == named_palette.name;
    const int row_height = row_heights[i];
    const int row_x = panel_x + kPanelPadding;
    const int row_width = panel_width - kPanelPadding * 2;

    canvas->drawRect(Rect::MakeXYWH(row_x, row_y, row_width, row_height),
                     expanded ? expanded_row_paint : row_paint);

    const bool can_delete = named_palette.name != "Default";
    const int delete_x = row_x + row_width - kDeleteSize - 8;
    const int delete_y = row_y + 8;
    if (can_delete) {
      canvas->drawRect(
          Rect::MakeXYWH(delete_x, delete_y, kDeleteSize, kDeleteSize),
          delete_paint);
      canvas->drawString("x", delete_x + 4, delete_y + 12, body_font,
                         delete_text_paint);
    }

    canvas->drawString(named_palette.name.c_str(), row_x + 10, row_y + 18,
                       title_font, title_paint);

    const int cell_size = expanded ? kExpandedCell : kCollapsedCell;
    const int row_count = palette.GetRowCount();
    const int tone_count = palette.GetToneCount();
    const int default_tone = palette.GetDefaultTone();

    int swatch_width, swatch_height;
    if (expanded) {
      swatch_height = row_count * cell_size + (row_count - 1) * kCellGap;
      swatch_width = tone_count * cell_size + (tone_count - 1) * kCellGap;
    } else {
      swatch_height = cell_size;
      swatch_width = row_count * cell_size + (row_count - 1) * kCellGap;
    }

    const int swatch_x = delete_x - 12 - swatch_width;
    const int swatch_y = expanded ? row_y + kHeaderHeight
                                  : row_y + (row_height - swatch_height) / 2;

    if (expanded) {
      for (int tone = 0; tone < tone_count; ++tone) {
        const std::string label = std::to_string(tone + 1);
        canvas->drawString(label.c_str(),
                           swatch_x + tone * (cell_size + kCellGap) + 4,
                           row_y + 18, body_font, text_paint);
      }

      for (int row = 0; row < row_count; ++row) {
        for (int tone = 0; tone < tone_count; ++tone) {
          const int cell_x = swatch_x + tone * (cell_size + kCellGap);
          const int cell_y = swatch_y + row * (cell_size + kCellGap);
          canvas->drawRect(Rect::MakeXYWH(cell_x - 1, cell_y - 1, cell_size + 2,
                                          cell_size + 2),
                           cell_border_paint);
          Paint color_paint;
          color_paint.color = (palette.GetColor(row, tone));
          canvas->drawRect(Rect::MakeXYWH(cell_x, cell_y, cell_size, cell_size),
                           color_paint);
        }
      }
    } else {
      // Collapsed: draw default tone column as a horizontal row.
      for (int row = 0; row < row_count; ++row) {
        const int cell_x = swatch_x + row * (cell_size + kCellGap);
        const int cell_y = swatch_y;
        canvas->drawRect(Rect::MakeXYWH(cell_x - 1, cell_y - 1, cell_size + 2,
                                        cell_size + 2),
                         cell_border_paint);
        Paint color_paint;
        color_paint.color = (palette.GetColor(row, default_tone));
        canvas->drawRect(Rect::MakeXYWH(cell_x, cell_y, cell_size, cell_size),
                         color_paint);
      }
    }

    row_y += row_height + kRowGap;
  }
}

void DrawLayersPanel(Canvas* canvas, const LayerStack& layer_stack,
                     const std::shared_ptr<Typeface>& typeface,
                     int window_width, int window_height) {
  constexpr int kLayerBoxHeight = kToolbarHeight - kToolMargin * 2;
  const int kLayerBoxWidth = static_cast<int>(window_width * 0.15f);
  constexpr int kLayerGap = kToolMargin;
  constexpr int kPanelX = kToolMargin;

  Font font(typeface, 12);
  Paint bg_paint;
  bg_paint.color = MakeARGB(255, 0, 0, 0);

  Paint text_paint;
  text_paint.color = MakeARGB(255, 220, 220, 220);

  Paint active_bg_paint;
  active_bg_paint.color = MakeARGB(255, 60, 60, 60);

  Paint type_badge_bg_paint;
  type_badge_bg_paint.color = MakeARGB(255, 80, 80, 80);

  Paint type_badge_text_paint;
  type_badge_text_paint.color = MakeARGB(255, 220, 220, 220);

  const auto& layers = layer_stack.GetLayers();
  const int active_index = layer_stack.GetActiveLayerIndex();

  const int status_bar_y = window_height - kCommandBarHeight - kStatusBarHeight;
  const int total_height =
      static_cast<int>(layers.size()) * (kLayerBoxHeight + kLayerGap) -
      kLayerGap;
  int y = status_bar_y - total_height - 4;
  if (y < kToolbarHeight + 4) {
    y = kToolbarHeight + 4;
  }

  for (int i = static_cast<int>(layers.size()) - 1; i >= 0; --i) {
    const auto& layer = layers[i];
    const std::string prefix =
        "[" + std::string(1, layer_stack.GetLayerKeybinding(i)) + "] ";
    const std::string full_name = layer->name.empty() ? "?" : layer->name;
    const std::string label = prefix + full_name;

    // Measure and truncate if needed.
    const float max_text_width = kLayerBoxWidth - kToolPaddingX * 2;
    std::string display_label = label;
    if (font.measureText(display_label.c_str(), display_label.size()) >
        max_text_width) {
      // Truncate with ellipsis.
      const std::string ellipsis = "...";
      size_t len = full_name.size();
      while (len > 0) {
        const std::string candidate =
            prefix + full_name.substr(0, len) + ellipsis;
        if (font.measureText(candidate.c_str(), candidate.size()) <=
            max_text_width) {
          display_label = candidate;
          break;
        }
        --len;
      }
    }

    canvas->drawRect(
        Rect::MakeXYWH(kPanelX, y, kLayerBoxWidth, kLayerBoxHeight),
        (i == active_index) ? active_bg_paint : bg_paint);

    canvas->drawString(display_label.c_str(), kPanelX + kToolPaddingX,
                       y + kLayerBoxHeight / 2.0f + 4, font, text_paint);

    // Type badge (R / V) on the right side.
    constexpr int kBadgeSize = 14;
    const int badge_x = kPanelX + kLayerBoxWidth - kBadgeSize - 4;
    const int badge_y = y + (kLayerBoxHeight - kBadgeSize) / 2;
    const std::string type_label =
        (layer->type == LayerType::kRaster) ? "R" : "V";
    canvas->drawRect(Rect::MakeXYWH(badge_x, badge_y, kBadgeSize, kBadgeSize),
                     type_badge_bg_paint);
    Font badge_font(typeface, 9);
    const float type_width =
        badge_font.measureText(type_label.c_str(), type_label.size());
    canvas->drawString(
        type_label.c_str(), badge_x + (kBadgeSize - type_width) / 2.0f,
        badge_y + kBadgeSize / 2.0f + 3, badge_font, type_badge_text_paint);

    y += kLayerBoxHeight + kLayerGap;
  }
}

void DrawStatusBar(Canvas* canvas, const InputManager& input,
                   const Vec2& mouse_canvas,
                   const std::shared_ptr<Typeface>& typeface, int window_width,
                   int window_height, const std::string& hover_info) {
  const int bar_y = window_height - kCommandBarHeight - kStatusBarHeight;

  Paint bg_paint;
  bg_paint.color = MakeARGB(255, 35, 35, 35);
  canvas->drawRect(Rect::MakeXYWH(0, bar_y, window_width, kStatusBarHeight),
                   bg_paint);

  Font font(typeface, 12);
  Paint text_paint;
  text_paint.color = MakeARGB(255, 220, 220, 220);

  const std::string mode_text = input.GetModeName();
  canvas->drawString(mode_text.c_str(), 10, bar_y + 17, font, text_paint);

  const std::string center_text = hover_info.empty() ? "[scratch]" : hover_info;
  const float center_width =
      font.measureText(center_text.c_str(), center_text.size());
  canvas->drawString(center_text.c_str(), (window_width - center_width) / 2.0f,
                     bar_y + 17, font, text_paint);

  const std::string pos_text =
      std::to_string(static_cast<int>(mouse_canvas.x)) + ", " +
      std::to_string(static_cast<int>(mouse_canvas.y));
  const float pos_width = font.measureText(pos_text.c_str(), pos_text.size());
  canvas->drawString(pos_text.c_str(), window_width - pos_width - 10,
                     bar_y + 17, font, text_paint);
}

void DrawCommandMenu(Canvas* canvas, const InputManager& input,
                     const std::shared_ptr<Typeface>& typeface,
                     int window_width, int window_height) {
  const int menu_y = window_height - kCommandBarHeight - kCommandMenuHeight;

  // Full solid background so short lists don't leak canvas through.
  Paint menu_bg;
  menu_bg.color = MakeARGB(255, 45, 45, 45);
  canvas->drawRect(Rect::MakeXYWH(0, menu_y, window_width, kCommandMenuHeight),
                   menu_bg);

  Font font(typeface, 12);
  Paint text_paint;
  text_paint.color = MakeARGB(255, 200, 200, 200);

  Paint highlight_text;
  highlight_text.color = MakeARGB(255, 255, 255, 255);

  Paint even_bg;
  even_bg.color = MakeARGB(255, 45, 45, 45);

  Paint odd_bg;
  odd_bg.color = MakeARGB(255, 55, 55, 55);

  Paint selected_bg;
  selected_bg.color = MakeARGB(255, 70, 70, 70);

  const auto& completions = input.GetCompletions();
  const int selected = input.GetCompletionIndex();

  std::vector<std::string> lines;
  if (!completions.empty()) {
    for (const auto& completion : completions) {
      lines.push_back(completion.display_text);
    }
  } else {
    lines = {
        "mode <mode>        - switch mode",
        "tool <tool>        - switch tool",
        "layer new <t>      - create new layer",
        "layer delete <i>   - delete layer",
        "layer <i>          - set active layer",
        "color <value>      - set current color",
        "palette new <name> <rows> [tones]",
        "move <x> <y>       - pan to coordinates",
        "zoom <level>       - set zoom level",
    };
  }

  // Scroll so the selected item stays visible.
  constexpr int kVisibleItems = kCommandMenuHeight / kCommandMenuItemHeight;
  int start_index = 0;
  if (selected >= 0 && static_cast<int>(lines.size()) > kVisibleItems) {
    start_index = std::max(0, selected - kVisibleItems / 2);
    start_index =
        std::min(start_index, static_cast<int>(lines.size()) - kVisibleItems);
  }

  float y = menu_y + 18;
  for (int i = start_index; i < static_cast<int>(lines.size()) &&
                            y <= menu_y + kCommandMenuHeight - 2;
       ++i) {
    const bool is_selected = (i == selected);
    const Paint& row_bg =
        is_selected ? selected_bg : ((i % 2 == 0) ? even_bg : odd_bg);
    canvas->drawRect(
        Rect::MakeXYWH(0, y - 14, window_width, kCommandMenuItemHeight),
        row_bg);
    if (is_selected) {
      canvas->drawString(lines[i].c_str(), 12, y, font, highlight_text);
    } else {
      canvas->drawString(lines[i].c_str(), 12, y, font, text_paint);
    }
    y += kCommandMenuItemHeight;
  }
}

void DrawCommandBar(Canvas* canvas, const InputManager& input,
                    const std::shared_ptr<Typeface>& typeface, int window_width,
                    int window_height) {
  const int bar_y = window_height - kCommandBarHeight;

  Paint bg_paint;
  bg_paint.color = MakeARGB(255, 20, 20, 20);
  canvas->drawRect(Rect::MakeXYWH(0, bar_y, window_width, kCommandBarHeight),
                   bg_paint);

  if (!input.IsCommandModeActive()) {
    return;
  }

  Font font(typeface, 13);
  Paint text_paint;
  text_paint.color = MakeARGB(255, 200, 200, 200);

  const std::string display = ":" + input.GetCommandBuffer();
  canvas->drawString(display.c_str(), 10, bar_y + 19, font, text_paint);

  const float prefix_width = font.measureText(":", 1);
  const float buffer_width = font.measureText(input.GetCommandBuffer().c_str(),
                                              input.GetCommandBuffer().size());
  const float cursor_x = 10 + prefix_width + buffer_width;

  Paint cursor_paint;
  cursor_paint.color = MakeARGB(255, 200, 200, 200);
  canvas->drawRect(Rect::MakeXYWH(cursor_x, bar_y + 4, 8, 18), cursor_paint);

  // Inline completion preview: show remainder of first match in dim text.
  const auto& completions = input.GetCompletions();
  const std::string& buffer = input.GetCommandBuffer();
  if (!completions.empty() && !buffer.empty()) {
    const std::string& first = completions[0].display_text;
    if (first.size() > buffer.size() &&
        first.substr(0, buffer.size()) == buffer) {
      const std::string suffix = first.substr(buffer.size());
      Paint preview_paint;
      preview_paint.color = MakeARGB(255, 100, 100, 100);
      canvas->drawString(suffix.c_str(), cursor_x, bar_y + 19, font,
                         preview_paint);
    }
  }
}

void DrawTransformGizmo(Canvas* canvas, const Rect& bbox, float zoom) {
  if (bbox.isEmpty()) return;

  const float handle = 8.0f / zoom;

  Paint bbox_paint;
  bbox_paint.color = MakeARGB(255, 255, 220, 80);
  bbox_paint.fill = false;
  bbox_paint.strokeWidth = 2.0f / zoom;

  Paint handle_fill;
  handle_fill.color = MakeARGB(255, 255, 220, 80);
  handle_fill.fill = true;

  Paint handle_stroke;
  handle_stroke.color = MakeARGB(255, 30, 30, 30);
  handle_stroke.fill = false;
  handle_stroke.strokeWidth = 1.0f / zoom;

  Paint rotate_fill;
  rotate_fill.color = MakeARGB(255, 80, 200, 255);
  rotate_fill.fill = true;

  Paint rotate_stroke;
  rotate_stroke.color = MakeARGB(255, 30, 30, 30);
  rotate_stroke.fill = false;
  rotate_stroke.strokeWidth = 1.0f / zoom;

  // Draw bounding box.
  canvas->drawRect(bbox, bbox_paint);

  // Corner handles.
  const Vec2 corners[4] = {
      {bbox.left, bbox.top},
      {bbox.right, bbox.top},
      {bbox.right, bbox.bottom},
      {bbox.left, bbox.bottom},
  };
  for (const auto& c : corners) {
    Rect r = Rect::MakeXYWH(c.x - handle / 2, c.y - handle / 2, handle, handle);
    canvas->drawRect(r, handle_fill);
    canvas->drawRect(r, handle_stroke);
  }

  // Rotation handle above top center.
  float rot_x = bbox.centerX();
  float rot_y = bbox.top - 20.0f / zoom;
  canvas->drawLine(bbox.centerX(), bbox.top, rot_x, rot_y, bbox_paint);
  canvas->drawCircle(rot_x, rot_y, handle / 2, rotate_fill);
  canvas->drawCircle(rot_x, rot_y, handle / 2, rotate_stroke);
}

int HitGizmoHandle(const Rect& bbox, const Vec2& p, float zoom) {
  if (bbox.isEmpty() || zoom <= 0.0f) return -1;
  const float half = 4.0f / zoom + 3.0f / zoom;  // drawn half-size + padding
  const float rot_x = bbox.centerX();
  const float rot_y = bbox.top - 20.0f / zoom;
  if (std::fabs(p.x - rot_x) <= half && std::fabs(p.y - rot_y) <= half) {
    return 4;
  }
  const Vec2 corners[4] = {
      {bbox.left, bbox.top},
      {bbox.right, bbox.top},
      {bbox.right, bbox.bottom},
      {bbox.left, bbox.bottom},
  };
  for (int h = 0; h < 4; ++h) {
    if (std::fabs(p.x - corners[h].x) <= half &&
        std::fabs(p.y - corners[h].y) <= half) {
      return h;
    }
  }
  return -1;
}

void DrawRulerOverlay(Canvas* canvas, Tool tool, bool measuring,
                      const Vec2& start, const Vec2& current, int angle_stage,
                      const Vec2& angle_p0, const Vec2& angle_p1,
                      const Vec2& angle_p2, float zoom,
                      const std::shared_ptr<Typeface>& typeface) {
  Paint line_paint;
  line_paint.color = MakeARGB(255, 255, 220, 80);
  line_paint.fill = false;
  line_paint.strokeWidth = 1.5f / zoom;

  Paint guide_paint;
  guide_paint.color = MakeARGB(180, 100, 200, 255);
  guide_paint.fill = false;
  guide_paint.strokeWidth = 1.0f / zoom;

  Paint text_paint;
  text_paint.color = MakeARGB(255, 255, 220, 80);

  Font font(typeface, 14.0f / zoom);

  if (tool == Tool::kDistance && measuring) {
    canvas->drawLine(start.x, start.y, current.x, current.y, line_paint);
    float dx = current.x - start.x;
    float dy = current.y - start.y;
    float dist = std::hypot(dx, dy);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.1f", dist);
    float mid_x = (start.x + current.x) * 0.5f;
    float mid_y = (start.y + current.y) * 0.5f;
    canvas->drawString(buf, mid_x, mid_y - 4.0f / zoom, font, text_paint);
  }

  if (tool == Tool::kAngle) {
    if (angle_stage >= 1) {
      canvas->drawCircle(angle_p0.x, angle_p0.y, 4.0f / zoom, line_paint);
    }
    if (angle_stage >= 2) {
      canvas->drawLine(angle_p0.x, angle_p0.y, angle_p1.x, angle_p1.y,
                       line_paint);
      canvas->drawCircle(angle_p1.x, angle_p1.y, 4.0f / zoom, line_paint);
    }
    if (angle_stage >= 3) {
      canvas->drawLine(angle_p1.x, angle_p1.y, angle_p2.x, angle_p2.y,
                       line_paint);
      canvas->drawCircle(angle_p2.x, angle_p2.y, 4.0f / zoom, line_paint);
      float a1 = std::atan2(angle_p1.y - angle_p0.y, angle_p1.x - angle_p0.x);
      float a2 = std::atan2(angle_p2.y - angle_p1.y, angle_p2.x - angle_p1.x);
      float deg = std::abs((a2 - a1) * 180.0f / 3.14159265f);
      if (deg > 180.0f) deg = 360.0f - deg;
      char buf[64];
      std::snprintf(buf, sizeof(buf), "%.1f°", deg);
      canvas->drawString(buf, angle_p1.x + 6.0f / zoom,
                         angle_p1.y - 6.0f / zoom, font, text_paint);
    }
  }

  if (tool == Tool::kPerpendicular) {
    // Draw axis-aligned crosshair at current cursor position.
    float len = 40.0f / zoom;
    canvas->drawLine(current.x - len, current.y, current.x + len, current.y,
                     guide_paint);
    canvas->drawLine(current.x, current.y - len, current.x, current.y + len,
                     guide_paint);
  }

  if (tool == Tool::kTangent) {
    // Draw diagonal crosshair at current cursor position.
    float len = 40.0f / zoom;
    canvas->drawLine(current.x - len, current.y - len, current.x + len,
                     current.y + len, guide_paint);
    canvas->drawLine(current.x + len, current.y - len, current.x - len,
                     current.y + len, guide_paint);
  }
}

void DrawToolMenu(Canvas* canvas, const InputManager& input,
                  const std::shared_ptr<Typeface>& typeface, int window_width,
                  int window_height) {
  if (!input.IsToolMenuOpen()) {
    return;
  }
  const Tool tool = input.GetToolMenuTool();
  const ToolSettings& settings = input.GetToolSettings(tool);
  const std::vector<ToolMenuRow> rows = BuildToolMenuRows(tool, settings);
  const ToolMenuLayout layout = ComputeToolMenuLayout(
      window_width, window_height, static_cast<int>(rows.size()));

  Paint scrim = Paint::Fill(MakeARGB(120, 10, 10, 10));
  canvas->drawRect(Rect::MakeXYWH(0, 0, window_width, window_height), scrim);

  Paint panel_paint = Paint::Fill(MakeARGB(245, 28, 28, 28));
  canvas->drawRect(layout.panel, panel_paint);

  Font title_font(typeface, 14);
  Font body_font(typeface, 12);
  Paint title_paint = Paint::Fill(MakeARGB(255, 230, 230, 230));
  Paint text_paint = Paint::Fill(MakeARGB(255, 220, 220, 220));
  Paint dim_paint = Paint::Fill(MakeARGB(255, 140, 140, 140));
  Paint row_paint = Paint::Fill(MakeARGB(255, 45, 45, 45));
  Paint active_paint = Paint::Fill(MakeARGB(255, 62, 62, 62));
  Paint track_paint = Paint::Fill(MakeARGB(255, 80, 80, 80));
  Paint knob_paint = Paint::Fill(MakeARGB(255, 220, 220, 220));

  const float title_y = layout.panel.top + 14.0f + 12.0f;
  const std::string title = input.GetToolDisplayName(tool);
  canvas->drawString(title.c_str(), layout.panel.left + 14.0f, title_y,
                     title_font, title_paint);
  char key_hint[16];
  std::snprintf(key_hint, sizeof(key_hint), "[%c]",
                input.GetToolKeybinding(tool));
  const float hint_w = body_font.measureText(key_hint, std::strlen(key_hint));
  canvas->drawString(key_hint, layout.panel.right - 14.0f - hint_w, title_y,
                     body_font, dim_paint);

  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    const Rect& row = layout.rows[i];
    canvas->drawRect(row,
                     (i == input.GetToolMenuRow()) ? active_paint : row_paint);
    const float text_y = row.top + 22.0f;
    if (rows[i].adjustable) {
      canvas->drawString(rows[i].label.c_str(), row.left + 8.0f, text_y,
                         body_font, text_paint);
      const Rect& track = layout.sliders[i];
      canvas->drawRect(track, track_paint);
      const float t = ToolMenuRowT(tool, settings, i);
      const float knob_x = track.left + t * track.width();
      const float knob_y = (track.top + track.bottom) * 0.5f;
      Paint knob = knob_paint;
      canvas->drawCircle(knob_x, knob_y, 7.0f, knob);
      Paint knob_ring = Paint::Stroke(MakeARGB(255, 30, 30, 30), 1.0f);
      canvas->drawCircle(knob_x, knob_y, 7.0f, knob_ring);
      canvas->drawString(rows[i].value_text.c_str(), track.right + 8.0f, text_y,
                         body_font, text_paint);
    } else {
      canvas->drawString(rows[i].label.c_str(), row.left + 8.0f, text_y,
                         body_font, dim_paint);
    }
  }

  const float footer_y = layout.panel.bottom - 10.0f;
  canvas->drawString(
      "Esc close   \xe2\x86\x90/\xe2\x86\x91 adjust   "
      "\xe2\x86\x91/\xe2\x86\x93 select",
      layout.panel.left + 14.0f, footer_y, body_font, dim_paint);
}

}  // namespace nanote
