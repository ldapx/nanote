#pragma once

// Full-window main menu: searchable grid of saved .nano document previews
// plus a "+ New document" tile. Row 0 is always New; rows 1..N map onto the
// filtered doc list (search substring + doc-type filter). The + New tile
// and Ctrl+N open a type chooser overlay instead of creating directly.
// Layout is shared between drawing and hit-testing.

#include <memory>
#include <string>
#include <vector>

#include "core/geometry.h"
#include "doc/document.h"
#include "input/input_manager.h"

namespace nanote {

class Canvas;
class Typeface;
class InputManager;

struct MainMenuCell {
  Rect rect;    // whole tile
  Rect thumb;   // preview area inside the tile
  Rect del;     // × delete button (empty for the New tile)
  int row = 0;  // global row index (0 = New, 1..N = filtered docs)
};

struct MainMenuLayout {
  Rect panel;
  Rect search_bar;
  Rect browse_button;  // "..." native file picker, right of the search bar
  Rect filter_bar;
  Rect chips[4];  // All, Infinite, Bounded, Slideshow
  std::vector<MainMenuCell> cells;
  int first_visible = 0;  // global row index of cells[0]
  int total_rows = 0;     // New + filtered docs
  int cols = 1;
};

// Modal new-document type chooser (Infinite / Bounded / Slideshow).
struct DocTypeChoiceLayout {
  Rect panel;
  Rect rows[3];
};

// Modal bounded-canvas size chooser (5 presets + custom typed size).
struct DocSizeChoiceLayout {
  Rect panel;
  Rect rows[6];
};

[[nodiscard]] MainMenuLayout ComputeMainMenuLayout(int window_w, int window_h,
                                                   int total_rows,
                                                   int selected);
[[nodiscard]] int MainMenuHitRow(const MainMenuLayout& layout, float mx,
                                 float my);
[[nodiscard]] bool MainMenuHitSearch(const MainMenuLayout& layout, float mx,
                                     float my);
// "..." browse-button hit (native .nano picker), or false.
[[nodiscard]] bool MainMenuHitBrowse(const MainMenuLayout& layout, float mx,
                                     float my);
// × delete-button hit: filtered row index, or -1.
[[nodiscard]] int MainMenuHitDeleteRow(const MainMenuLayout& layout, float mx,
                                       float my);
// Filter chip index hit (0..3) or -1.
[[nodiscard]] int MainMenuHitFilter(const MainMenuLayout& layout, float mx,
                                    float my);
[[nodiscard]] DocTypeChoiceLayout ComputeDocTypeChoiceLayout(int window_w,
                                                             int window_h);
// Chooser option row hit (0..2), or -1 for a miss.
[[nodiscard]] int DocTypeChoiceHitRow(const DocTypeChoiceLayout& layout,
                                      float mx, float my);
[[nodiscard]] DocSizeChoiceLayout ComputeDocSizeChoiceLayout(int window_w,
                                                             int window_h);
// Size preset row hit (0..4) or custom row (5); -1 for a miss.
[[nodiscard]] int DocSizeChoiceHitRow(const DocSizeChoiceLayout& layout,
                                      float mx, float my);
[[nodiscard]] std::string FormatDocSize(uint64_t bytes);
[[nodiscard]] std::string FormatDocDate(uint64_t unix_mtime);

void DrawMainMenu(Canvas* canvas, const InputManager& input,
                  const std::shared_ptr<Typeface>& typeface, int window_width,
                  int window_height, const std::string& open_name,
                  bool open_dirty, double time_seconds = 0.0);
// Returns true when the click was consumed (search bar focus, filter chip,
// tile select + open/new request already recorded on input, chooser
// interaction). Clicks outside the panel are also consumed silently since
// the menu is modal.
bool HandleMainMenuClick(float mx, float my, int window_width,
                         int window_height, InputManager& input);

}  // namespace nanote
