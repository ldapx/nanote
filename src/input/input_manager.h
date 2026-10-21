#pragma once

#include <SDL3/SDL.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "doc/document.h"

namespace nanote {

enum class Mode {
  kNavigate,
  kDraw,
  kShape,
  kRuler,
  kErase,
  kSelect,
  kText,
  kFill,
  kTransform,
  kNode,

  kCount
};

enum class Tool {
  kNone,

  // Navigate
  kPan,

  // Draw
  kPencil,
  kPen,
  kBrush,
  kMarker,
  kSpray,

  // Shape
  kLine,
  kRectangle,
  kCircle,
  kEllipse,
  kPolygon,
  kStar,
  kArc,
  kBezier,

  // Ruler
  kDistance,
  kAngle,
  kPerpendicular,
  kTangent,
  kGridTool,

  // Erase
  kStroke,
  kArea,
  kPoint,

  // Select
  kSingle,
  kLasso,
  kMagic,
  kAll,

  // Text
  kSingleLine,
  kParagraph,
  kPathText,

  // Fill
  kSolid,
  kGradient,
  kPattern,

  // Transform
  kMove,
  kScale,
  kRotate,
  kSkew,
  kMirror,

  // Node
  kEdit,
  kSmooth,
  kCorner,
  kBreak,
  kJoin,

  kCount
};

// Default tool selected when entering a mode (leader menu, :mode command,
// toolbar clicks). kCount (not a real mode) maps to kNone.
[[nodiscard]] inline Tool DefaultToolForMode(Mode mode) {
  switch (mode) {
    case Mode::kNavigate:
      return Tool::kPan;
    case Mode::kDraw:
      return Tool::kPencil;
    case Mode::kShape:
      return Tool::kLine;
    case Mode::kRuler:
      return Tool::kDistance;
    case Mode::kErase:
      return Tool::kStroke;
    case Mode::kSelect:
      return Tool::kSingle;
    case Mode::kText:
      return Tool::kSingleLine;
    case Mode::kFill:
      return Tool::kSolid;
    case Mode::kTransform:
      return Tool::kMove;
    case Mode::kNode:
      return Tool::kEdit;
    case Mode::kCount:
      return Tool::kNone;
  }
  return Tool::kNone;
}

struct CommandCompletion {
  std::string insert_text;
  std::string display_text;

  bool operator==(const CommandCompletion& other) const = default;
};

// Adjustable per-tool settings (edited via the double-press tool menu).
enum class ToolSettingId {
  kSize,           // brush diameter / erase size / region scale (px)
  kHardness,       // dab tip hardness 0..1
  kFlow,           // per-dab opacity 0..1
  kSpacing,        // dab spacing as fraction of diameter
  kSprayCount,     // spray particles per dab (0 = auto density)
  kSpraySpread,    // particle spread relative to dab radius
  kSprayParticle,  // particle radius px (0 = auto)
  kTolerance,      // flood-fill tolerance
  kStrokeWidth,    // shape stroke width (px)
  kFontSize,       // text font size (px)
};

struct ToolSettings {
  float size = 3.0f;
  float hardness = 0.8f;
  float flow = 1.0f;
  float spacing = 0.15f;
  float spray_count = -1.0f;  // <0 = auto density
  float spray_spread = 1.0f;
  float spray_particle = -1.0f;  // <0 = auto from size
  float tolerance = 24.0f;
  float stroke_width = 2.0f;
  float font_size = 16.0f;
};

class InputManager {
 public:
  void HandleEvent(const SDL_Event& event);
  void Update(float delta_time);

  [[nodiscard]] std::string GetModeName() const;
  [[nodiscard]] std::string GetToolName() const;
  [[nodiscard]] std::vector<Tool> GetToolsForCurrentMode() const;
  [[nodiscard]] std::string GetToolDisplayName(Tool tool) const;
  [[nodiscard]] char GetToolKeybinding(Tool tool) const;
  [[nodiscard]] std::vector<Mode> GetAllModes() const;
  [[nodiscard]] std::string GetModeDisplayName(Mode mode) const;
  [[nodiscard]] char GetModeKeybinding(Mode mode) const;

  [[nodiscard]] bool WantsPanLeft() const {
    return pan_left_;
  }
  [[nodiscard]] bool WantsPanRight() const {
    return pan_right_;
  }
  [[nodiscard]] bool WantsPanUp() const {
    return pan_up_;
  }
  [[nodiscard]] bool WantsPanDown() const {
    return pan_down_;
  }

  bool ConsumeZoomIn();
  bool ConsumeZoomOut();
  bool ConsumeResetZoom();

  [[nodiscard]] Mode GetMode() const {
    return current_mode_;
  }
  void SetMode(Mode mode);
  [[nodiscard]] Tool GetTool() const {
    return current_tool_;
  }
  void SetTool(Tool tool);
  // Hotkey path: pressing the active tool's key toggles its settings menu,
  // pressing another tool's key switches to it (closing any open menu).
  void PressToolKey(Tool tool);

  // Tool size is stored per tool; these access the active tool's size.
  [[nodiscard]] float GetToolSize() const;
  void SetToolSize(float size);

  [[nodiscard]] const ToolSettings& GetToolSettings(Tool tool) const;
  void SetToolSetting(Tool tool, ToolSettingId id, float value);

  // Double-press settings menu (centered modal, one at a time).
  [[nodiscard]] bool IsToolMenuOpen() const {
    return tool_menu_tool_ != Tool::kNone;
  }
  [[nodiscard]] Tool GetToolMenuTool() const {
    return tool_menu_tool_;
  }
  void OpenToolMenu(Tool tool);
  void CloseToolMenu() {
    tool_menu_tool_ = Tool::kNone;
    tool_menu_drag_row_ = -1;
  }
  // Keyboard/slider focus inside the open menu.
  [[nodiscard]] int GetToolMenuRow() const {
    return tool_menu_row_;
  }
  void SetToolMenuRow(int row) {
    tool_menu_row_ = row;
  }
  void MoveToolMenuRow(int dir);
  void AdjustToolMenuRow(int dir);
  [[nodiscard]] bool IsToolMenuDragging() const {
    return tool_menu_drag_row_ >= 0;
  }
  [[nodiscard]] int GetToolMenuDragRow() const {
    return tool_menu_drag_row_;
  }
  void SetToolMenuDragging(int row) {
    tool_menu_drag_row_ = row;
  }

  [[nodiscard]] bool IsLeaderPressed() const {
    return leader_pressed_;
  }
  void SetLeaderPressed(bool v) {
    leader_pressed_ = v;
  }

  [[nodiscard]] bool QuitRequested() const {
    return quit_requested_;
  }
  void SetQuitRequested(bool v) {
    quit_requested_ = v;
  }

  [[nodiscard]] bool IsCommandModeActive() const {
    return command_mode_active_;
  }
  void SetCommandModeActive(bool v) {
    command_mode_active_ = v;
  }
  [[nodiscard]] const std::string& GetCommandBuffer() const {
    return command_buffer_;
  }
  void SetCommandBuffer(const std::string& s) {
    command_buffer_ = s;
  }
  void AppendCommandBuffer(char c) {
    command_buffer_ += c;
  }
  void PopCommandBuffer() {
    if (!command_buffer_.empty()) command_buffer_.pop_back();
  }
  void ClearCommandBuffer();

  void PushCommandHistory(const std::string& cmd);
  [[nodiscard]] bool HasCommandHistory() const;
  void HistoryPrevious();
  void HistoryNext();

  [[nodiscard]] const std::string& GetCompletionFilter() const {
    return completion_filter_;
  }

  void SetCompletionCandidates(
      const std::vector<CommandCompletion>& candidates);
  void CompletionNext();
  void CompletionPrev();
  [[nodiscard]] int GetCompletionIndex() const {
    return completion_index_;
  }
  [[nodiscard]] const std::vector<CommandCompletion>& GetCompletions() const {
    return completions_;
  }
  void ClearCompletions() {
    completions_.clear();
    completion_index_ = -1;
  }

  [[nodiscard]] bool IsPaletteHovered() const {
    return palette_hovered_;
  }
  void SetPaletteHovered(bool hovered);
  [[nodiscard]] bool IsPaletteExpanded() const {
    return palette_expanded_;
  }
  void SetPaletteExpanded(bool expanded);
  void TogglePaletteExpanded();

  [[nodiscard]] float GetPaletteExpansionFactor() const {
    return expansion_t_;
  }
  [[nodiscard]] int GetDisplayedTone() const {
    return displayed_tone_;
  }
  void SetDisplayedTone(int tone) {
    displayed_tone_ = tone;
  }

  [[nodiscard]] int GetPendingPaletteTone() const {
    return pending_palette_tone_;
  }
  void SetPendingPaletteTone(int tone) {
    pending_palette_tone_ = tone;
  }
  void ClearPendingPaletteTone() {
    pending_palette_tone_ = -1;
  }
  void RequestPaletteSelection(int row, int tone);
  [[nodiscard]] bool HasPaletteSelectionRequest() const {
    return palette_selection_row_ >= 0 && palette_selection_tone_ >= 0;
  }
  [[nodiscard]] int GetPaletteSelectionRow() const {
    return palette_selection_row_;
  }
  [[nodiscard]] int GetPaletteSelectionTone() const {
    return palette_selection_tone_;
  }
  void ClearPaletteSelectionRequest();

  [[nodiscard]] bool IsPaletteBrowserOpen() const {
    return palette_browser_open_;
  }
  void SetPaletteBrowserOpen(bool open) {
    palette_browser_open_ = open;
  }
  [[nodiscard]] const std::string& GetPaletteBrowserExpandedName() const {
    return palette_browser_expanded_name_;
  }
  void SetPaletteBrowserExpandedName(const std::string& name) {
    palette_browser_expanded_name_ = name;
  }

  [[nodiscard]] int GetLayerSelectReq() const {
    return layer_select_req_;
  }
  void SetLayerSelectReq(int index) {
    layer_select_req_ = index;
  }

  // Full-window main menu (double-escape toggles between canvas and menu).
  [[nodiscard]] bool IsInMainMenu() const {
    return in_main_menu_;
  }
  void SetInMainMenu(bool v);
  // Row 0 is "+ New document", rows 1..N are the filtered doc entries.
  // Filtering is a case-insensitive substring match on GetMainMenuSearch()
  // plus the doc-type filter (0 = All, else DocType+1).
  [[nodiscard]] int GetMainMenuRowCount() const;
  [[nodiscard]] int GetMainMenuSelected() const {
    return main_menu_selected_;
  }
  void SetMainMenuSelected(int row);
  void MoveMainMenuSelection(int dir);
  // Grid navigation: drow/dcol in cells, cols = current grid width.
  void MoveMainMenuSelectionInGrid(int drow, int dcol, int cols);
  void SetMainMenuCols(int cols) {
    main_menu_cols_ = cols > 0 ? cols : 1;
  }
  [[nodiscard]] int GetMainMenuCols() const {
    return main_menu_cols_;
  }
  void RefreshMainMenuDocs();
  void ActivateMainMenuSelection();
  [[nodiscard]] const std::vector<DocEntry>& GetMainMenuDocs() const {
    return main_menu_docs_;
  }
  // Indices into main_menu_docs_ matching the search + type filters.
  [[nodiscard]] std::vector<size_t> GetMainMenuFilteredIndices() const;
  // Type filter chips: 0 = All, 1..3 = infinite/bounded/slideshow.
  [[nodiscard]] int GetMainMenuTypeFilter() const {
    return main_menu_type_filter_;
  }
  void SetMainMenuTypeFilter(int f);
  void CycleMainMenuTypeFilter(int dir);
  // Delete flow: clicking a tile's × arms it, clicking again (or vim-style
  // dd with an empty search) deletes the file. The arm keys on the doc
  // path so it survives search/filter/selection shifts; Esc, the type
  // chooser, and open/new requests disarm.
  [[nodiscard]] bool IsDeleteArmedForPath(const std::string& path) const {
    return !delete_armed_path_.empty() && delete_armed_path_ == path;
  }
  void ActivateMainMenuDelete(int row);
  void RequestDeleteMainMenuDoc();
  [[nodiscard]] bool HasDeletedDocRequest() const {
    return !deleted_doc_name_.empty();
  }
  [[nodiscard]] std::string TakeDeletedDocRequest() {
    std::string n = deleted_doc_name_;
    deleted_doc_name_.clear();
    return n;
  }
  [[nodiscard]] const std::string& GetMainMenuSearch() const {
    return main_menu_search_;
  }
  void SetMainMenuSearch(const std::string& s);
  void AppendMainMenuSearch(const std::string& utf8);
  void PopMainMenuSearch();
  void ClearMainMenuSearch();
  [[nodiscard]] bool HasOpenPathRequest() const {
    return !open_path_request_.empty();
  }
  [[nodiscard]] std::string TakeOpenPathRequest() {
    std::string p = open_path_request_;
    open_path_request_.clear();
    return p;
  }
  void RequestOpenPath(const std::string& path) {
    open_path_request_ = path;
    delete_armed_path_.clear();
  }
  // "..." browse button next to the menu search bar: asks the main loop
  // (which owns the SDL_Window) to show a native .nano open dialog.
  void RequestBrowseForDoc() {
    browse_requested_ = true;
  }
  [[nodiscard]] bool ConsumeBrowseRequest() {
    const bool r = browse_requested_;
    browse_requested_ = false;
    return r;
  }
  struct NewDocRequest {
    DocType type = DocType::kInfinite;
    float page_w = 1920.0f;
    float page_h = 1080.0f;
  };
  void RequestNewDoc(DocType type = DocType::kInfinite, float page_w = 1920.0f,
                     float page_h = 1080.0f) {
    new_doc_request_ = true;
    pending_new_doc_.type = type;
    pending_new_doc_.page_w = page_w;
    pending_new_doc_.page_h = page_h;
    delete_armed_path_.clear();
  }
  [[nodiscard]] bool ConsumeNewDocRequest() {
    const bool r = new_doc_request_;
    new_doc_request_ = false;
    return r;
  }
  // Takes the pending new-document request (type + page size), resetting
  // to defaults.
  [[nodiscard]] NewDocRequest TakeNewDoc() {
    const NewDocRequest r = pending_new_doc_;
    pending_new_doc_ = NewDocRequest();
    return r;
  }
  // New-document type chooser overlay (opened from the + New tile).
  [[nodiscard]] bool IsChoosingDocType() const {
    return choosing_doc_type_;
  }
  void SetChoosingDocType(bool v);
  [[nodiscard]] bool IsChoosingDocSize() const {
    return choosing_size_;
  }
  void SetChoosingSize(bool v);
  [[nodiscard]] bool IsEditingCustomSize() const {
    return editing_custom_size_;
  }
  void SetEditingCustomSize(bool v);
  [[nodiscard]] const std::string& GetCustomSizeText() const {
    return custom_size_text_;
  }
  void AppendCustomSizeText(const std::string& utf8);
  void PopCustomSizeText();
  [[nodiscard]] bool ParseCustomSize(float* w, float* h) const;
  [[nodiscard]] int GetDocSizeChoice() const {
    return size_choice_;
  }
  void SetDocSizeChoice(int c);
  void MoveDocSizeChoice(int dir);
  void ActivateDocSizeChoice();
  void CancelDocSizeChoice();
  [[nodiscard]] int GetDocTypeChoice() const {
    return doc_type_choice_;
  }
  void SetDocTypeChoice(int c);
  void MoveDocTypeChoice(int dir);
  void ActivateDocTypeChoice();
  void CancelDocTypeChoice() {
    choosing_doc_type_ = false;
    delete_armed_path_.clear();
  }

 private:
  void HandleKeyDown(const SDL_Event& event);
  void HandleCommandModeKey(const SDL_Event& event);
  bool HandleMainMenuKey(const SDL_Event& event);
  bool HandleDocChoiceKey(const SDL_Event& event);
  bool HandleDocSizeChoiceKey(const SDL_Event& event);
  bool HandleMenuBrowserKey(const SDL_Event& event);
  bool HandleEscapeKey(SDL_Keycode key);
  bool HandleLeaderKey(SDL_Keycode key);
  bool HandleToolMenuKey(SDL_Keycode key);
  bool HandleCommandActivationKey(SDL_Keycode key);
  bool HandlePaletteKey(SDL_Keycode key);
  void HandleNavigateKey(SDL_Keycode key);
  bool HandlePaletteRowKey(SDL_Keycode key);
  bool HandleLayerKey(SDL_Keycode key);
  void HandleModeToolKey(SDL_Keycode key);
  void HandleKeyUp(const SDL_Event& event);
  void HandleTextInput(const SDL_Event& event);

  Mode current_mode_ = Mode::kDraw;
  Tool current_tool_ = Tool::kPencil;
  // Per-tool settings (size, brush params, ...). Created lazily with
  // tool-specific defaults so each tool remembers its own configuration.
  mutable std::map<Tool, ToolSettings> tool_settings_;
  // Double-press tool menu state (Tool::kNone = closed).
  Tool tool_menu_tool_ = Tool::kNone;
  int tool_menu_row_ = 0;
  int tool_menu_drag_row_ = -1;
  // Double-escape main menu state.
  bool in_main_menu_ = false;
  int main_menu_selected_ = 0;
  std::vector<DocEntry> main_menu_docs_;
  std::string main_menu_search_;
  int main_menu_type_filter_ = 0;  // 0 = All, 1..3 = DocType+1
  std::string delete_armed_path_;  // doc path armed for delete, empty none
  bool pending_menu_dd_ = false;   // first 'd' of a vim-style dd
  Uint64 last_menu_d_ms_ = 0;
  bool suppress_menu_text_once_ = false;  // swallow 2nd 'd' TEXT_INPUT of dd
  bool choosing_doc_type_ = false;
  int doc_type_choice_ = 0;     // 0..2, index into DocType
  bool choosing_size_ = false;  // paged doc: size step of the chooser
  int size_choice_ = 0;         // 0..4 presets, 5 = custom size
  bool editing_custom_size_ = false;
  std::string custom_size_text_;
  NewDocRequest pending_new_doc_;
  int main_menu_cols_ = 1;
  Uint64 last_escape_ms_ = 0;
  std::string open_path_request_;
  std::string deleted_doc_name_;  // just-deleted doc stem (session check)
  bool new_doc_request_ = false;
  bool browse_requested_ = false;
  bool leader_pressed_ = false;
  bool quit_requested_ = false;

  bool command_mode_active_ = false;
  std::string command_buffer_;
  std::string completion_filter_;
  std::string history_stash_;
  std::vector<std::string> command_history_;
  int history_browse_index_ = -1;
  std::vector<CommandCompletion> completions_;
  int completion_index_ = -1;

  bool palette_hovered_ = false;
  bool palette_expanded_ = false;
  float expansion_t_ = 0.0f;
  int displayed_tone_ = 5;
  int pending_palette_tone_ = -1;
  int palette_selection_row_ = -1;
  int palette_selection_tone_ = -1;

  bool palette_browser_open_ = false;
  std::string palette_browser_expanded_name_;

  int layer_select_req_ = -1;

  bool pan_left_ = false;
  bool pan_right_ = false;
  bool pan_up_ = false;
  bool pan_down_ = false;

  bool zoom_in_req_ = false;
  bool zoom_out_req_ = false;
  bool reset_zoom_req_ = false;
};

}  // namespace nanote
