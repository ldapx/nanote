#include "ui/ui_hit_test.h"
#include "ui/ui_renderer.h"

#include "ui/main_menu.h"
#include "ui/tool_menu.h"

#include <algorithm>
#include <vector>

namespace nanote {

bool IsPointInPaletteBounds(float x, float y, const InputManager& input,
                            const ColorManager& color_manager, int window_width,
                            int window_height) {
  if (input.IsPaletteBrowserOpen()) {
    return false;
  }
  const PaletteStripGeometry geo = ComputePaletteStripGeometry(
      input, color_manager, window_width, window_height);
  if (!geo.valid) {
    return false;
  }
  return x >= geo.start_x && x < geo.start_x + geo.width && y >= geo.start_y &&
         y < geo.start_y + geo.height;
}

bool HandleMouseClick(float mx, float my, int window_width, int window_height,
                      InputManager& input, LayerStack& layer_stack,
                      ColorManager& color_manager,
                      const std::shared_ptr<Typeface>& typeface) {
  // Full-window main menu is modal: it consumes every click.
  if (input.IsInMainMenu()) {
    return HandleMainMenuClick(mx, my, window_width, window_height, input);
  }

  // Double-press tool menu is modal: it consumes every click.
  if (input.IsToolMenuOpen()) {
    const Tool tool = input.GetToolMenuTool();
    const auto rows = BuildToolMenuRows(tool, input.GetToolSettings(tool));
    const ToolMenuLayout layout = ComputeToolMenuLayout(
        window_width, window_height, static_cast<int>(rows.size()));
    const int hit = ToolMenuHitRow(layout, mx, my);
    if (hit < 0) {
      // Clicked outside: dismiss without changing anything.
      input.CloseToolMenu();
      return true;
    }
    input.SetToolMenuRow(hit);
    if (rows[hit].adjustable) {
      const float t = ToolMenuSliderT(layout, hit, mx);
      // Clicking anywhere on the row jumps the slider; drag continues it.
      ApplyToolMenuRowT(input, tool, hit, t);
      input.SetToolMenuDragging(hit);
    }
    return true;
  }

  if (input.IsPaletteBrowserOpen()) {
    constexpr int kPanelPadding = 16;
    constexpr int kRowGap = 10;
    constexpr int kCollapsedCell = 12;
    constexpr int kExpandedCell = 18;
    constexpr int kCellGap = 4;
    constexpr int kHeaderHeight = 24;
    constexpr int kDeleteSize = 16;

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

    if (mx < panel_x || mx >= panel_x + panel_width || my < panel_y ||
        my >= panel_y + panel_height) {
      input.SetPaletteBrowserOpen(false);
      input.SetPaletteBrowserExpandedName("");
      return true;
    }

    int row_y = panel_y + kPanelPadding;
    for (size_t i = 0; i < palettes.size(); ++i) {
      const auto& named_palette = palettes[i];
      const Palette& palette = named_palette.palette;
      const bool expanded =
          input.GetPaletteBrowserExpandedName() == named_palette.name;
      const int row_height = row_heights[i];
      const int row_x = panel_x + kPanelPadding;
      const int row_width = panel_width - kPanelPadding * 2;
      const bool can_delete = named_palette.name != "Default";
      const int delete_x = row_x + row_width - kDeleteSize - 8;
      const int delete_y = row_y + 8;

      if (can_delete && mx >= delete_x && mx < delete_x + kDeleteSize &&
          my >= delete_y && my < delete_y + kDeleteSize) {
        if (color_manager.RemovePalette(static_cast<int>(i)) &&
            input.GetPaletteBrowserExpandedName() == named_palette.name) {
          input.SetPaletteBrowserExpandedName("");
        }
        return true;
      }

      if (mx >= row_x && mx < row_x + row_width && my >= row_y &&
          my < row_y + row_height) {
        if (!color_manager.LoadPalette(static_cast<int>(i))) {
          return true;
        }
        input.SetPaletteBrowserExpandedName(named_palette.name);

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
        const int swatch_y = expanded
                                 ? row_y + kHeaderHeight
                                 : row_y + (row_height - swatch_height) / 2;

        if (mx >= swatch_x && mx < swatch_x + swatch_width && my >= swatch_y &&
            my < swatch_y + swatch_height) {
          if (expanded) {
            const int tone =
                static_cast<int>(mx - swatch_x) / (cell_size + kCellGap);
            const int row =
                static_cast<int>(my - swatch_y) / (cell_size + kCellGap);
            if (tone >= 0 && tone < tone_count && row >= 0 && row < row_count) {
              color_manager.SetActivePaletteCell(row, tone);
            }
          } else {
            const int row =
                static_cast<int>(mx - swatch_x) / (cell_size + kCellGap);
            if (row >= 0 && row < row_count) {
              color_manager.SetActivePaletteCell(row, default_tone);
              input.SetDisplayedTone(default_tone);
            }
          }
        }
        return true;
      }

      row_y += row_height + kRowGap;
    }

    return true;
  }

  // Palette click.
  const PaletteStripGeometry geo = ComputePaletteStripGeometry(
      input, color_manager, window_width, window_height);
  if (!geo.valid) {
    return false;
  }

  const Palette& palette = color_manager.GetActivePalette();
  const int row_count = palette.GetRowCount();
  const int max_tone_count = palette.GetToneCount();

  const float t = input.GetPaletteExpansionFactor();
  const int full_palette_width = geo.full_width;

  if (mx >= geo.start_x && mx < geo.start_x + geo.width && my >= geo.start_y &&
      my < geo.start_y + geo.height) {
    const int row =
        static_cast<int>(my - geo.start_y) / (kPaletteBoxOuter + kPaletteGap);

    const int displayed_tone = input.GetDisplayedTone();
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

      if (alpha > 0.05f && mx >= cx && mx < cx + kPaletteBoxOuter) {
        if (row >= 0 && row < row_count) {
          color_manager.SetActivePaletteCell(row, tone);
          input.ClearPendingPaletteTone();
          return true;
        }
      }
    }
  }

  // Toolbar click. Boxes come from the shared layout helper so hit
  // areas always match what DrawToolbar renders.
  if (my >= kToolMargin && my < kToolbarHeight - kToolMargin) {
    for (const ToolbarItem& item :
         ComputeToolbarItems(input, typeface, window_width)) {
      if (mx < item.rect.left || mx >= item.rect.right) continue;
      switch (item.kind) {
        case ToolbarItemKind::kModeIndicator:
          input.SetLeaderPressed(true);
          return true;
        case ToolbarItemKind::kMode: {
          const Mode mode = item.mode;
          input.SetMode(mode);
          input.SetTool(DefaultToolForMode(mode));
          input.SetLeaderPressed(false);
          return true;
        }
        case ToolbarItemKind::kTool:
          input.PressToolKey(item.tool);
          return true;
      }
    }
  }
  // Layers click.
  constexpr int kLayerBoxHeight = kToolbarHeight - kToolMargin * 2;
  const int kLayerBoxWidth = static_cast<int>(window_width * 0.15f);
  constexpr int kLayerGap = kToolMargin;
  constexpr int kPanelX = kToolMargin;
  const auto& layers = layer_stack.GetLayers();
  const int status_bar_y = window_height - kCommandBarHeight - kStatusBarHeight;
  const int total_height =
      static_cast<int>(layers.size()) * (kLayerBoxHeight + kLayerGap) -
      kLayerGap;
  int ly = status_bar_y - total_height - 4;
  if (ly < kToolbarHeight + 4) {
    ly = kToolbarHeight + 4;
  }

  if (mx >= kPanelX && mx < kPanelX + kLayerBoxWidth) {
    for (int i = static_cast<int>(layers.size()) - 1; i >= 0; --i) {
      if (my >= ly && my < ly + kLayerBoxHeight) {
        layer_stack.SetActiveLayerIndex(i);
        return true;
      }
      ly += kLayerBoxHeight + kLayerGap;
    }
  }
  return false;
}

}  // namespace nanote
