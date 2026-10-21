#include "input/input_manager.h"

#include <algorithm>
#include <cstdio>

#include "core/string_utils.h"
#include "ui/tool_menu.h"

namespace nanote {

namespace {

int PaletteRowFromKey(SDL_Keycode key, Mode current_mode) {
  switch (key) {
    case SDLK_A:
      return 0;
    case SDLK_S:
      return 1;
    case SDLK_D:
      return 2;
    case SDLK_F:
      return 3;
    case SDLK_G:
      return 4;
    case SDLK_H:
      return current_mode == Mode::kNavigate ? -1 : 5;
    case SDLK_J:
      return current_mode == Mode::kNavigate ? -1 : 6;
    case SDLK_K:
      return current_mode == Mode::kNavigate ? -1 : 7;
    default:
      return -1;
  }
}

int PaletteToneFromKey(SDL_Keycode key) {
  switch (key) {
    case SDLK_1:
      return 0;
    case SDLK_2:
      return 1;
    case SDLK_3:
      return 2;
    case SDLK_4:
      return 3;
    case SDLK_5:
      return 4;
    case SDLK_6:
      return 5;
    case SDLK_7:
      return 6;
    case SDLK_8:
      return 7;
    default:
      return -1;
  }
}

// Display names indexed by Tool (Tools are 0..kCount, in enum order).
constexpr const char* kToolDisplayNames[] = {
    "none",                                                          // kNone
    "pan",                                                           // kPan
    "pencil",   "pen",       "brush",         "marker",  "spray",    // Draw
    "line",     "rectangle", "circle",        "ellipse", "polygon",  // Shape
    "star",     "arc",       "bezier",                               //
    "distance", "angle",     "perpendicular", "tangent", "grid",     // Ruler
    "stroke",   "area",      "point",                                // Erase
    "single",   "lasso",     "magic",         "all",                 // Select
    "single",   "paragraph", "path",                                 // Text
    "solid",    "gradient",  "pattern",                              // Fill
    "move",     "scale",     "rotate",        "skew",    "mirror",  // Transform
    "edit",     "smooth",    "corner",        "break",   "join",    // Node
};
static_assert(std::size(kToolDisplayNames) ==
              static_cast<size_t>(Tool::kCount));

// Display names and hotkeys indexed by Mode (Modes are 0..kCount).
constexpr const char* kModeDisplayNames[] = {
    "nav",    "draw", "shape", "ruler", "erase",
    "select", "text", "fill",  "trans", "node",
};
static_assert(std::size(kModeDisplayNames) ==
              static_cast<size_t>(Mode::kCount));
constexpr char kModeKeybindings[] = {'q', 'w', 'e', 'r', 't',
                                     'y', 'u', 'i', 'o', 'p'};
static_assert(std::size(kModeKeybindings) == static_cast<size_t>(Mode::kCount));

constexpr const char* kModeNames[] = {
    "navigate", "draw", "shape", "ruler",     "erase",
    "select",   "text", "fill",  "transform", "node",
};
static_assert(std::size(kModeNames) == static_cast<size_t>(Mode::kCount));

// (Mode, tool, hotkey) triples. Single source of truth for tool hotkeys,
// read by both the toolbar labels (GetToolKeybinding) and the key dispatch
// (HandleModeToolKey).
struct ToolKeyBinding {
  Mode mode;
  Tool tool;
  char key;
};
constexpr ToolKeyBinding kToolKeyBindings[] = {
    {Mode::kDraw, Tool::kPencil, 'q'},
    {Mode::kDraw, Tool::kPen, 'w'},
    {Mode::kDraw, Tool::kBrush, 'e'},
    {Mode::kDraw, Tool::kMarker, 'r'},
    {Mode::kDraw, Tool::kSpray, 't'},
    {Mode::kShape, Tool::kLine, 'q'},
    {Mode::kShape, Tool::kRectangle, 'w'},
    {Mode::kShape, Tool::kCircle, 'e'},
    {Mode::kShape, Tool::kEllipse, 'r'},
    {Mode::kShape, Tool::kPolygon, 't'},
    {Mode::kShape, Tool::kStar, 'y'},
    {Mode::kShape, Tool::kArc, 'u'},
    {Mode::kShape, Tool::kBezier, 'i'},
    {Mode::kRuler, Tool::kDistance, 'q'},
    {Mode::kRuler, Tool::kAngle, 'w'},
    {Mode::kRuler, Tool::kPerpendicular, 'e'},
    {Mode::kRuler, Tool::kTangent, 'r'},
    {Mode::kRuler, Tool::kGridTool, 't'},
    {Mode::kErase, Tool::kStroke, 'q'},
    {Mode::kErase, Tool::kArea, 'w'},
    {Mode::kErase, Tool::kPoint, 'e'},
    {Mode::kSelect, Tool::kSingle, 'q'},
    {Mode::kSelect, Tool::kLasso, 'w'},
    {Mode::kSelect, Tool::kMagic, 'e'},
    {Mode::kSelect, Tool::kAll, 'r'},
    {Mode::kText, Tool::kSingleLine, 'q'},
    {Mode::kText, Tool::kParagraph, 'w'},
    {Mode::kText, Tool::kPathText, 'e'},
    {Mode::kFill, Tool::kSolid, 'q'},
    {Mode::kFill, Tool::kGradient, 'w'},
    {Mode::kFill, Tool::kPattern, 'e'},
    {Mode::kTransform, Tool::kMove, 'q'},
    {Mode::kTransform, Tool::kScale, 'w'},
    {Mode::kTransform, Tool::kRotate, 'e'},
    {Mode::kTransform, Tool::kSkew, 'r'},
    {Mode::kTransform, Tool::kMirror, 't'},
    {Mode::kNode, Tool::kEdit, 'q'},
    {Mode::kNode, Tool::kSmooth, 'w'},
    {Mode::kNode, Tool::kCorner, 'e'},
    {Mode::kNode, Tool::kBreak, 'r'},
    {Mode::kNode, Tool::kJoin, 't'},
};

}  // namespace

void InputManager::HandleEvent(const SDL_Event& event) {
  switch (event.type) {
    case SDL_EVENT_QUIT:
      quit_requested_ = true;
      break;
    case SDL_EVENT_KEY_DOWN:
      HandleKeyDown(event);
      break;
    case SDL_EVENT_KEY_UP:
      HandleKeyUp(event);
      break;
    case SDL_EVENT_TEXT_INPUT:
      HandleTextInput(event);
      break;
  }
}

void InputManager::Update(float delta_time) {
  constexpr float kAnimationSpeed = 8.0f;
  if (palette_expanded_) {
    expansion_t_ = std::min(1.0f, expansion_t_ + kAnimationSpeed * delta_time);
  } else {
    expansion_t_ = std::max(0.0f, expansion_t_ - kAnimationSpeed * delta_time);
  }
}

void InputManager::HandleKeyDown(const SDL_Event& event) {
  const SDL_Keycode key = event.key.key;

  if (command_mode_active_) {
    HandleCommandModeKey(event);
    return;
  }
  if (in_main_menu_ && HandleMainMenuKey(event)) {
    return;
  }

  if (event.key.repeat != 0) {
    return;
  }

  // Undo/redo are canvas-only (command bar and documents menu have their
  // own text handling) and are not repeatable: one keypress, one step.
  {
    const bool ctrl = (event.key.mod & SDL_KMOD_CTRL) != 0;
    const bool shift = (event.key.mod & SDL_KMOD_SHIFT) != 0;
    if (ctrl && !in_main_menu_) {
      if (key == SDLK_Z && !shift) {
        RequestUndo();
        return;
      }
      if ((key == SDLK_Z && shift) || key == SDLK_Y) {
        RequestRedo();
        return;
      }
    }
  }

  if (HandleEscapeKey(key)) {
    return;
  }
  if (HandleLeaderKey(key)) {
    return;
  }

  if (key == SDLK_SPACE) {
    leader_pressed_ = true;
    return;
  }

  if (HandleToolMenuKey(key)) {
    return;
  }
  if (HandleCommandActivationKey(key)) {
    return;
  }

  if (palette_browser_open_) {
    return;
  }

  if (HandlePaletteKey(key)) {
    return;
  }
  HandleNavigateKey(key);
  if (HandlePaletteRowKey(key)) {
    return;
  }
  if (HandleLayerKey(key)) {
    return;
  }
  HandleModeToolKey(key);
}

void InputManager::HandleCommandModeKey(const SDL_Event& event) {
  const SDL_Keycode key = event.key.key;
  const bool ctrl = (event.key.mod & SDL_KMOD_CTRL) != 0;
  const bool shift = (event.key.mod & SDL_KMOD_SHIFT) != 0;

  if (key == SDLK_ESCAPE) {
    command_mode_active_ = false;
    ClearCommandBuffer();
    history_browse_index_ = -1;
    return;
  }
  if (key == SDLK_RETURN || key == SDLK_RETURN2) {
    command_mode_active_ = false;
    history_browse_index_ = -1;
    return;
  }
  if (key == SDLK_BACKSPACE) {
    if (!command_buffer_.empty()) {
      command_buffer_.pop_back();
    }
    completion_filter_ = command_buffer_;
    history_browse_index_ = -1;
    return;
  }
  if (key == SDLK_UP) {
    HistoryPrevious();
    return;
  }
  if (key == SDLK_DOWN) {
    HistoryNext();
    return;
  }
  if (key == SDLK_TAB) {
    if (shift) {
      CompletionPrev();
    } else {
      CompletionNext();
    }
    return;
  }
  if (ctrl && key == SDLK_N) {
    CompletionNext();
    return;
  }
  if (ctrl && key == SDLK_P) {
    CompletionPrev();
    return;
  }
  return;
}

bool InputManager::HandleMainMenuKey(const SDL_Event& event) {
  if (choosing_doc_type_) {
    return HandleDocChoiceKey(event);
  }
  return HandleMenuBrowserKey(event);
}

bool InputManager::HandleDocChoiceKey(const SDL_Event& event) {
  const SDL_Keycode key = event.key.key;
  const bool repeat = event.key.repeat != 0;
  if (choosing_size_) {
    return HandleDocSizeChoiceKey(event);
  }
  if (key == SDLK_UP || key == SDLK_LEFT) {
    MoveDocTypeChoice(-1);
    return true;
  }
  if (key == SDLK_DOWN || key == SDLK_RIGHT) {
    MoveDocTypeChoice(1);
    return true;
  }
  if (!repeat && (key == SDLK_RETURN || key == SDLK_RETURN2 ||
                  key == SDLK_SPACE || key == SDLK_KP_ENTER)) {
    ActivateDocTypeChoice();
    return true;
  }
  if (!repeat && (key == SDLK_1 || key == SDLK_KP_1)) {
    SetDocTypeChoice(0);
    ActivateDocTypeChoice();
    return true;
  }
  if (!repeat && (key == SDLK_2 || key == SDLK_KP_2)) {
    SetDocTypeChoice(1);
    ActivateDocTypeChoice();
    return true;
  }
  if (!repeat && (key == SDLK_3 || key == SDLK_KP_3)) {
    SetDocTypeChoice(2);
    ActivateDocTypeChoice();
    return true;
  }
  if (key == SDLK_ESCAPE) {
    CancelDocTypeChoice();
    return true;
  }
  return true;  // modal: swallow everything else, no search text
  return true;
}

bool InputManager::HandleDocSizeChoiceKey(const SDL_Event& event) {
  const SDL_Keycode key = event.key.key;
  const bool repeat = event.key.repeat != 0;
  if (editing_custom_size_) {
    // Typing a custom "W x H" resolution.
    if (key == SDLK_ESCAPE) {
      editing_custom_size_ = false;
      custom_size_text_.clear();
      return true;  // back to the preset list
    }
    if (key == SDLK_BACKSPACE) {
      PopCustomSizeText();
      return true;
    }
    if (!repeat &&
        (key == SDLK_RETURN || key == SDLK_RETURN2 || key == SDLK_KP_ENTER)) {
      ActivateDocSizeChoice();  // creates if valid, else stays editing
      return true;
    }
    return true;  // modal: text arrives via TEXT_INPUT
  }
  if (key == SDLK_UP || key == SDLK_LEFT) {
    MoveDocSizeChoice(-1);
    return true;
  }
  if (key == SDLK_DOWN || key == SDLK_RIGHT) {
    MoveDocSizeChoice(1);
    return true;
  }
  if (!repeat && (key == SDLK_RETURN || key == SDLK_RETURN2 ||
                  key == SDLK_SPACE || key == SDLK_KP_ENTER)) {
    ActivateDocSizeChoice();
    return true;
  }
  if (!repeat && (key == SDLK_1 || key == SDLK_KP_1)) {
    SetDocSizeChoice(0);
    ActivateDocSizeChoice();
    return true;
  }
  if (!repeat && (key == SDLK_2 || key == SDLK_KP_2)) {
    SetDocSizeChoice(1);
    ActivateDocSizeChoice();
    return true;
  }
  if (!repeat && (key == SDLK_3 || key == SDLK_KP_3)) {
    SetDocSizeChoice(2);
    ActivateDocSizeChoice();
    return true;
  }
  if (!repeat && (key == SDLK_4 || key == SDLK_KP_4)) {
    SetDocSizeChoice(3);
    ActivateDocSizeChoice();
    return true;
  }
  if (!repeat && (key == SDLK_5 || key == SDLK_KP_5)) {
    SetDocSizeChoice(4);
    ActivateDocSizeChoice();
    return true;
  }
  if (key == SDLK_ESCAPE) {
    CancelDocSizeChoice();
    return true;
  }
  return true;  // modal: swallow everything else, no search text
  return true;
}

bool InputManager::HandleMenuBrowserKey(const SDL_Event& event) {
  const SDL_Keycode key = event.key.key;
  const bool repeat = event.key.repeat != 0;
  const bool shift = (event.key.mod & SDL_KMOD_SHIFT) != 0;
  // Vim-style dd deletes the selected document. The first 'd' is held
  // back (not inserted) until a second keystroke disambiguates: a fast
  // second 'd' with an empty search deletes; anything else flushes the
  // held 'd' into the search box first, so typing words ("dog", "add")
  // never deletes and never loses letters.
  static constexpr Uint64 kDoublePressMs = 500;
  const bool no_menu_mods = (event.key.mod & SDL_KMOD_CTRL) == 0 &&
                            (event.key.mod & SDL_KMOD_ALT) == 0 &&
                            (event.key.mod & SDL_KMOD_GUI) == 0;
  const bool plain_d = !repeat && key == SDLK_D && no_menu_mods;
  const bool is_modifier = key == SDLK_LSHIFT || key == SDLK_RSHIFT ||
                           key == SDLK_LCTRL || key == SDLK_RCTRL ||
                           key == SDLK_LALT || key == SDLK_RALT ||
                           key == SDLK_LGUI || key == SDLK_RGUI;
  if (plain_d) {
    const Uint64 now = SDL_GetTicks();
    if (pending_menu_dd_ && main_menu_search_.empty() &&
        now - last_menu_d_ms_ <= kDoublePressMs) {
      pending_menu_dd_ = false;
      RequestDeleteMainMenuDoc();
      suppress_menu_text_once_ = true;  // swallow the queued 2nd 'd'
      return true;
    }
    if (main_menu_search_.empty()) {
      pending_menu_dd_ = true;  // arm; hold this 'd' back for now
      last_menu_d_ms_ = now;
      suppress_menu_text_once_ = true;
      return true;
    }
    pending_menu_dd_ = false;  // searching already: plain typing
  } else if (!is_modifier && pending_menu_dd_) {
    // Some other key came first: flush the held-back 'd', then handle
    // this key normally below.
    pending_menu_dd_ = false;
    suppress_menu_text_once_ = false;
    AppendMainMenuSearch("d");
  }
  // Arrow keys navigate the grid; letters always go to the search
  // filter via TEXT_INPUT (no HJKL navigation so typing "hjkl" works).
  if (key == SDLK_UP) {
    MoveMainMenuSelectionInGrid(-1, 0, main_menu_cols_);
    return true;
  }
  if (key == SDLK_DOWN) {
    MoveMainMenuSelectionInGrid(1, 0, main_menu_cols_);
    return true;
  }
  if (key == SDLK_LEFT) {
    MoveMainMenuSelectionInGrid(0, -1, main_menu_cols_);
    return true;
  }
  if (key == SDLK_RIGHT) {
    MoveMainMenuSelectionInGrid(0, 1, main_menu_cols_);
    return true;
  }
  if (key == SDLK_BACKSPACE) {
    PopMainMenuSearch();
    return true;
  }
  if (!repeat && key == SDLK_TAB) {
    CycleMainMenuTypeFilter(shift ? -1 : 1);
    return true;
  }
  if (!repeat && (key == SDLK_RETURN || key == SDLK_RETURN2)) {
    ActivateMainMenuSelection();
    return true;
  }
  // Ctrl+N opens the type chooser; bare N types into the search filter.
  if (!repeat && key == SDLK_N && (event.key.mod & SDL_KMOD_CTRL) != 0) {
    SetChoosingDocType(true);
    return true;
  }
  // Swallow everything else in the menu except text (handled via
  // TEXT_INPUT) and Escape, which falls through below so a
  // double-press toggles back to the canvas.
  if (key != SDLK_ESCAPE) return true;
  // Single Escape disarms a pending delete, then clears search, then the
  // type filter, instead of starting a toggle back to the canvas.
  if (!delete_armed_path_.empty()) {
    delete_armed_path_.clear();
    return true;
  }
  if (!main_menu_search_.empty()) {
    ClearMainMenuSearch();
    return true;
  }
  if (main_menu_type_filter_ != 0) {
    SetMainMenuTypeFilter(0);
    return true;
  }
  return false;
}

bool InputManager::HandleEscapeKey(SDL_Keycode key) {
  if (key == SDLK_ESCAPE) {
    if (tool_menu_tool_ != Tool::kNone) {
      CloseToolMenu();
      return true;
    }
    if (palette_browser_open_) {
      if (!palette_browser_expanded_name_.empty()) {
        palette_browser_expanded_name_.clear();
      } else {
        palette_browser_open_ = false;
      }
      return true;
    }
    if (palette_expanded_) {
      SetPaletteExpanded(false);
      return true;
    }
    // Double-escape toggles the full-window main menu. Only reached when
    // Escape closed nothing above.
    static constexpr Uint64 kDoubleEscapeMs = 500;
    const Uint64 now = SDL_GetTicks();
    if (last_escape_ms_ != 0 && now - last_escape_ms_ <= kDoubleEscapeMs) {
      last_escape_ms_ = 0;
      if (in_main_menu_) {
        in_main_menu_ = false;
      } else {
        CloseToolMenu();
        SetInMainMenu(true);
      }
      return true;
    }
    last_escape_ms_ = now;
  }
  return false;
}

bool InputManager::HandleLeaderKey(SDL_Keycode key) {
  if (!leader_pressed_) {
    return false;
  }
  leader_pressed_ = false;
  for (int i = 0; i < static_cast<int>(Mode::kCount); ++i) {
    const Mode mode = static_cast<Mode>(i);
    if (key == static_cast<SDL_Keycode>(GetModeKeybinding(mode))) {
      current_mode_ = mode;
      current_tool_ = DefaultToolForMode(mode);
      CloseToolMenu();
      return true;
    }
  }
  CloseToolMenu();
  return true;
}

bool InputManager::HandleToolMenuKey(SDL_Keycode key) {
  // Open tool menu: slider navigation/adjustment. Other keys (tool
  // hotkeys, palette, layers, ...) fall through to normal handling so a
  // menu can be swapped/closed by switching tools.
  if (tool_menu_tool_ == Tool::kNone) {
    return false;
  }
  if (key == SDLK_UP) {
    MoveToolMenuRow(-1);
    return true;
  }
  if (key == SDLK_DOWN) {
    MoveToolMenuRow(1);
    return true;
  }
  if (key == SDLK_LEFT || key == SDLK_MINUS) {
    AdjustToolMenuRow(-1);
    return true;
  }
  if (key == SDLK_RIGHT || key == SDLK_EQUALS || key == SDLK_PLUS) {
    AdjustToolMenuRow(1);
    return true;
  }
  if (key == SDLK_RETURN || key == SDLK_RETURN2 || key == SDLK_KP_ENTER) {
    CloseToolMenu();
    return true;
  }
  return false;
}

bool InputManager::HandleCommandActivationKey(SDL_Keycode key) {
  if (key == SDLK_COLON || key == SDLK_SEMICOLON || key == SDLK_SLASH ||
      key == SDLK_PERIOD) {
    command_mode_active_ = true;
    command_buffer_.clear();
    CloseToolMenu();
    return true;
  }
  return false;
}

bool InputManager::HandlePaletteKey(SDL_Keycode key) {
  if (palette_hovered_ && key == SDLK_TAB) {
    TogglePaletteExpanded();
    return true;
  }

  if (palette_expanded_) {
    const int tone_index = PaletteToneFromKey(key);
    if (tone_index >= 0) {
      displayed_tone_ = tone_index;
      SetPaletteExpanded(false);
      return true;
    }

    const int row_index = PaletteRowFromKey(key, current_mode_);
    if (row_index >= 0) {
      RequestPaletteSelection(
          row_index, pending_palette_tone_ >= 0 ? pending_palette_tone_ : 0);
      pending_palette_tone_ = -1;
      return true;
    }
  }
  return false;
}

void InputManager::HandleNavigateKey(SDL_Keycode key) {
  if (current_mode_ == Mode::kNavigate) {
    switch (key) {
      case SDLK_H:
        pan_left_ = true;
        break;
      case SDLK_J:
        pan_down_ = true;
        break;
      case SDLK_K:
        pan_up_ = true;
        break;
      case SDLK_L:
        pan_right_ = true;
        break;
      case SDLK_EQUALS:
      case SDLK_PLUS:
        zoom_in_req_ = true;
        break;
      case SDLK_MINUS:
        zoom_out_req_ = true;
        break;
      case SDLK_0:
        reset_zoom_req_ = true;
        break;
      default:
        break;
    }
  }
}

bool InputManager::HandlePaletteRowKey(SDL_Keycode key) {
  // Palette keybinds (a-k, skipped in navigate mode for h/j/k/l).
  const int row_index = PaletteRowFromKey(key, current_mode_);
  if (row_index >= 0) {
    RequestPaletteSelection(row_index, displayed_tone_);
    return true;
  }
  return false;
}

bool InputManager::HandleLayerKey(SDL_Keycode key) {
  // Layer keybinds (z-m).
  const int kLayerIndex = [&]() -> int {
    switch (key) {
      case SDLK_Z:
        return 0;
      case SDLK_X:
        return 1;
      case SDLK_C:
        return 2;
      case SDLK_V:
        return 3;
      case SDLK_B:
        return 4;
      case SDLK_N:
        return 5;
      case SDLK_M:
        return 6;
      default:
        return -1;
    }
  }();
  if (kLayerIndex >= 0) {
    // Layer selection is handled externally via LayerStack.
    // We just store the requested index so main.cpp can apply it.
    layer_select_req_ = kLayerIndex;
    return true;
  }
  return false;
}

void InputManager::HandleModeToolKey(SDL_Keycode key) {
  // Navigate has no tool hotkeys (HJKL pan and zoom handled separately).
  if (current_mode_ == Mode::kNavigate) {
    return;
  }
  // Tool hotkeys mirror GetToolKeybinding: the key shown on each tool box
  // selects that tool (double-press toggles its settings menu). A single
  // table instead of a second copy of the mode/tool/key mapping.
  for (Tool tool : GetToolsForCurrentMode()) {
    if (key == static_cast<SDL_Keycode>(GetToolKeybinding(tool))) {
      PressToolKey(tool);
      return;
    }
  }
}

void InputManager::HandleKeyUp(const SDL_Event& event) {
  const SDL_Keycode key = event.key.key;

  if (current_mode_ == Mode::kNavigate) {
    switch (key) {
      case SDLK_H:
        pan_left_ = false;
        break;
      case SDLK_J:
        pan_down_ = false;
        break;
      case SDLK_K:
        pan_up_ = false;
        break;
      case SDLK_L:
        pan_right_ = false;
        break;
      default:
        break;
    }
  }
}

void InputManager::HandleTextInput(const SDL_Event& event) {
  if (in_main_menu_) {
    // Swallow a held-back 'd' (pending or instant dd).
    if (suppress_menu_text_once_) {
      suppress_menu_text_once_ = false;
      return;
    }
    if (editing_custom_size_ && event.text.text != nullptr) {
      AppendCustomSizeText(event.text.text);
      return;
    }
    if (pending_menu_dd_) {
      // A held-back first 'd' followed by other text: flush it first so
      // no letters are lost.
      pending_menu_dd_ = false;
      AppendMainMenuSearch("d");
    }
    // The type chooser is modal: typed text is dropped, not searched.
    if (!choosing_doc_type_ && event.text.text != nullptr) {
      AppendMainMenuSearch(event.text.text);
    }
    return;
  }
  if (!command_mode_active_) {
    return;
  }
  command_buffer_ += event.text.text;
  completion_filter_ = command_buffer_;
  history_browse_index_ = -1;
}

bool InputManager::ConsumeZoomIn() {
  const bool result = zoom_in_req_;
  zoom_in_req_ = false;
  return result;
}

bool InputManager::ConsumeZoomOut() {
  const bool result = zoom_out_req_;
  zoom_out_req_ = false;
  return result;
}

bool InputManager::ConsumeResetZoom() {
  const bool result = reset_zoom_req_;
  reset_zoom_req_ = false;
  return result;
}

std::string InputManager::GetModeName() const {
  const auto index = static_cast<size_t>(current_mode_);
  if (index >= std::size(kModeNames)) {
    return "unknown";
  }
  return kModeNames[index];
}

std::string InputManager::GetToolName() const {
  return GetToolDisplayName(current_tool_);
}

std::vector<Tool> InputManager::GetToolsForCurrentMode() const {
  // Indexed by Mode (see kModeTools); kCount has no tools.
  static const std::vector<Tool> kTools[] = {
      {Tool::kPan},
      {Tool::kPencil, Tool::kPen, Tool::kBrush, Tool::kMarker, Tool::kSpray},
      {Tool::kLine, Tool::kRectangle, Tool::kCircle, Tool::kEllipse,
       Tool::kPolygon, Tool::kStar, Tool::kArc, Tool::kBezier},
      {Tool::kDistance, Tool::kAngle, Tool::kPerpendicular, Tool::kTangent,
       Tool::kGridTool},
      {Tool::kStroke, Tool::kArea, Tool::kPoint},
      {Tool::kSingle, Tool::kLasso, Tool::kMagic, Tool::kAll},
      {Tool::kSingleLine, Tool::kParagraph, Tool::kPathText},
      {Tool::kSolid, Tool::kGradient, Tool::kPattern},
      {Tool::kMove, Tool::kScale, Tool::kRotate, Tool::kSkew, Tool::kMirror},
      {Tool::kEdit, Tool::kSmooth, Tool::kCorner, Tool::kBreak, Tool::kJoin},
  };
  static_assert(std::size(kTools) == static_cast<size_t>(Mode::kCount));
  const auto index = static_cast<size_t>(current_mode_);
  if (index >= std::size(kTools)) {
    return {};
  }
  return kTools[index];
}

std::string InputManager::GetToolDisplayName(Tool tool) const {
  const auto index = static_cast<size_t>(tool);
  if (index >= std::size(kToolDisplayNames)) {
    return "unknown";
  }
  return kToolDisplayNames[index];
}

char InputManager::GetToolKeybinding(Tool tool) const {
  // Navigate answers 'q' for any tool (kept for compatibility; navigate has
  // no tool hotkeys and HandleModeToolKey never consults this in that mode).
  if (current_mode_ == Mode::kNavigate) {
    return 'q';
  }
  for (const ToolKeyBinding& binding : kToolKeyBindings) {
    if (binding.mode == current_mode_ && binding.tool == tool) {
      return binding.key;
    }
  }
  return '?';
}

std::string InputManager::GetModeDisplayName(Mode mode) const {
  const auto index = static_cast<size_t>(mode);
  if (index >= std::size(kModeDisplayNames)) {
    return "unknown";
  }
  return kModeDisplayNames[index];
}

std::vector<Mode> InputManager::GetAllModes() const {
  std::vector<Mode> modes;
  for (int i = 0; i < static_cast<int>(Mode::kCount); ++i) {
    modes.push_back(static_cast<Mode>(i));
  }
  return modes;
}

char InputManager::GetModeKeybinding(Mode mode) const {
  const auto index = static_cast<size_t>(mode);
  if (index >= std::size(kModeKeybindings)) {
    return '?';
  }
  return kModeKeybindings[index];
}

void InputManager::PushCommandHistory(const std::string& cmd) {
  if (cmd.empty()) {
    return;
  }
  // Avoid duplicate consecutive entries.
  if (!command_history_.empty() && command_history_.back() == cmd) {
    return;
  }
  command_history_.push_back(cmd);
  // Limit history size.
  constexpr size_t kMaxHistory = 100;
  if (command_history_.size() > kMaxHistory) {
    command_history_.erase(command_history_.begin());
  }
}

bool InputManager::HasCommandHistory() const {
  return !command_history_.empty();
}

void InputManager::HistoryPrevious() {
  if (command_history_.empty()) {
    return;
  }
  if (history_browse_index_ == -1) {
    history_stash_ = command_buffer_;
    history_browse_index_ = static_cast<int>(command_history_.size()) - 1;
  } else if (history_browse_index_ > 0) {
    --history_browse_index_;
  }
  command_buffer_ = command_history_[history_browse_index_];
}

void InputManager::HistoryNext() {
  if (history_browse_index_ == -1) {
    return;
  }
  ++history_browse_index_;
  if (history_browse_index_ >= static_cast<int>(command_history_.size())) {
    history_browse_index_ = -1;
    command_buffer_ = history_stash_;
  } else {
    command_buffer_ = command_history_[history_browse_index_];
  }
}

void InputManager::ClearCommandBuffer() {
  command_buffer_.clear();
  completion_filter_.clear();
  completions_.clear();
  completion_index_ = -1;
}

void InputManager::SetCompletionCandidates(
    const std::vector<CommandCompletion>& candidates) {
  if (candidates == completions_) {
    return;
  }
  completions_ = candidates;
  completion_index_ = -1;
}

void InputManager::CompletionNext() {
  if (completions_.empty()) {
    return;
  }
  if (completion_index_ < 0) {
    completion_index_ = 0;
  } else {
    completion_index_ =
        (completion_index_ + 1) % static_cast<int>(completions_.size());
  }
  command_buffer_ = completions_[completion_index_].insert_text;
  completion_filter_ = command_buffer_;
}

void InputManager::CompletionPrev() {
  if (completions_.empty()) {
    return;
  }
  if (completion_index_ < 0) {
    completion_index_ = static_cast<int>(completions_.size()) - 1;
  } else {
    completion_index_ =
        (completion_index_ - 1 + static_cast<int>(completions_.size())) %
        static_cast<int>(completions_.size());
  }
  command_buffer_ = completions_[completion_index_].insert_text;
  completion_filter_ = command_buffer_;
}

void InputManager::SetMode(Mode mode) {
  if (current_mode_ != mode) {
    CloseToolMenu();
  }
  current_mode_ = mode;
}

void InputManager::SetTool(Tool tool) {
  current_tool_ = tool;
  CloseToolMenu();
}

void InputManager::PressToolKey(Tool tool) {
  if (tool == current_tool_) {
    // Double-press: toggle this tool's settings menu.
    if (tool_menu_tool_ == tool) {
      CloseToolMenu();
    } else {
      OpenToolMenu(tool);
    }
  } else {
    current_tool_ = tool;
    CloseToolMenu();
  }
}

void InputManager::OpenToolMenu(Tool tool) {
  tool_menu_tool_ = tool;
  tool_menu_row_ = 0;
  tool_menu_drag_row_ = -1;
  // Focus the first adjustable row.
  const int count = ToolMenuRowCount(tool);
  while (tool_menu_row_ < count &&
         !ToolMenuRowAdjustable(tool, tool_menu_row_)) {
    ++tool_menu_row_;
  }
  if (tool_menu_row_ >= count) tool_menu_row_ = 0;
}

void InputManager::SetInMainMenu(bool v) {
  if (in_main_menu_ == v) return;
  in_main_menu_ = v;
  if (v) {
    CloseToolMenu();
    main_menu_search_.clear();
    main_menu_type_filter_ = 0;
    delete_armed_path_.clear();
    pending_menu_dd_ = false;
    suppress_menu_text_once_ = false;
    choosing_doc_type_ = false;
    choosing_size_ = false;
    editing_custom_size_ = false;
    custom_size_text_.clear();
    main_menu_cols_ = 1;
    RefreshMainMenuDocs();
    main_menu_selected_ = 0;
    open_path_request_.clear();
    new_doc_request_ = false;
    pending_new_doc_ = NewDocRequest();
  }
}

std::vector<size_t> InputManager::GetMainMenuFilteredIndices() const {
  std::vector<size_t> out;
  const std::string needle = Lower(main_menu_search_);
  for (size_t i = 0; i < main_menu_docs_.size(); ++i) {
    const DocEntry& doc = main_menu_docs_[i];
    if (main_menu_type_filter_ != 0 &&
        static_cast<int>(doc.type) != main_menu_type_filter_ - 1) {
      continue;
    }
    if (!needle.empty() && Lower(doc.name).find(needle) == std::string::npos) {
      continue;
    }
    out.push_back(i);
  }
  return out;
}

void InputManager::SetMainMenuTypeFilter(int f) {
  main_menu_type_filter_ = std::clamp(f, 0, 3);
  const int count = GetMainMenuRowCount();
  if (main_menu_selected_ >= count) {
    main_menu_selected_ = std::max(0, count - 1);
  }
}

void InputManager::CycleMainMenuTypeFilter(int dir) {
  int f = main_menu_type_filter_ + (dir >= 0 ? 1 : -1);
  f = (f % 4 + 4) % 4;
  SetMainMenuTypeFilter(f);
}

void InputManager::SetMainMenuSearch(const std::string& s) {
  main_menu_search_ = s;
  const int count = GetMainMenuRowCount();
  if (main_menu_selected_ >= count) {
    main_menu_selected_ = std::max(0, count - 1);
  }
}

void InputManager::AppendMainMenuSearch(const std::string& utf8) {
  if (utf8.empty()) return;
  main_menu_search_ += utf8;
  const int count = GetMainMenuRowCount();
  if (main_menu_selected_ >= count) {
    main_menu_selected_ = std::max(0, count - 1);
  }
}

void InputManager::PopMainMenuSearch() {
  while (!main_menu_search_.empty()) {
    const unsigned char last =
        static_cast<unsigned char>(main_menu_search_.back());
    main_menu_search_.pop_back();
    // Stop after removing a full UTF-8 codepoint (lead byte removed).
    if ((last & 0xC0) != 0x80) break;
  }
  const int count = GetMainMenuRowCount();
  if (main_menu_selected_ >= count) {
    main_menu_selected_ = std::max(0, count - 1);
  }
}

void InputManager::ClearMainMenuSearch() {
  if (main_menu_search_.empty()) return;
  main_menu_search_.clear();
  const int count = GetMainMenuRowCount();
  if (main_menu_selected_ >= count) {
    main_menu_selected_ = std::max(0, count - 1);
  }
}

void InputManager::RefreshMainMenuDocs() {
  main_menu_docs_ = ListDocuments();
  const int max_row = GetMainMenuRowCount() - 1;
  if (main_menu_selected_ > max_row) main_menu_selected_ = max_row;
  if (main_menu_selected_ < 0) main_menu_selected_ = 0;
}

int InputManager::GetMainMenuRowCount() const {
  return static_cast<int>(GetMainMenuFilteredIndices().size()) +
         1;  // row 0 = New
}

void InputManager::SetMainMenuSelected(int row) {
  const int count = GetMainMenuRowCount();
  if (count <= 0) {
    main_menu_selected_ = 0;
    return;
  }
  main_menu_selected_ = std::clamp(row, 0, count - 1);
}

void InputManager::MoveMainMenuSelection(int dir) {
  // Wheel / legacy step: move by one grid row when a grid is active so
  // scrolling feels like paging through rows of previews.
  const int cols = main_menu_cols_ > 0 ? main_menu_cols_ : 1;
  if (cols > 1) {
    MoveMainMenuSelectionInGrid(dir >= 0 ? 1 : -1, 0, cols);
    return;
  }
  const int count = GetMainMenuRowCount();
  if (count <= 0) {
    main_menu_selected_ = 0;
    return;
  }
  int row = main_menu_selected_ + (dir >= 0 ? 1 : -1);
  row = (row % count + count) % count;  // wrap around
  main_menu_selected_ = row;
}

void InputManager::MoveMainMenuSelectionInGrid(int drow, int dcol, int cols) {
  const int count = GetMainMenuRowCount();
  if (count <= 0) {
    main_menu_selected_ = 0;
    return;
  }
  if (cols < 1) cols = 1;
  main_menu_cols_ = cols;
  if (cols <= 1) {
    // Single column (narrow window): keep legacy wrap-around stepping so
    // Up/Down cycle through the whole list.
    int step = drow + dcol;
    if (step == 0) return;
    int row = main_menu_selected_ + (step >= 0 ? 1 : -1);
    row = (row % count + count) % count;
    main_menu_selected_ = row;
    return;
  }
  const int cur = main_menu_selected_;
  const int cur_r = cur / cols;
  const int cur_c = cur % cols;
  int nr = cur_r + drow;
  int nc = cur_c + dcol;
  // Clamp within the grid; moving past an edge sticks to the edge cell.
  const int max_r = (count - 1) / cols;
  if (nr < 0) nr = 0;
  if (nr > max_r) nr = max_r;
  if (nc < 0) nc = 0;
  if (nc >= cols) nc = cols - 1;
  int next = nr * cols + nc;
  if (next >= count) {
    // Last row is partial: clamp to its final cell.
    next = count - 1;
    // Moving down into a gap lands on the last cell; moving sideways in
    // the last row also sticks to the last cell.
  }
  main_menu_selected_ = std::clamp(next, 0, count - 1);
}

void InputManager::ActivateMainMenuSelection() {
  if (main_menu_selected_ <= 0) {
    // The + New tile opens the type chooser instead of creating directly.
    choosing_doc_type_ = true;
    doc_type_choice_ = 0;
    delete_armed_path_.clear();
    return;
  }
  const std::vector<size_t> filt = GetMainMenuFilteredIndices();
  const size_t fi = static_cast<size_t>(main_menu_selected_ - 1);
  if (fi < filt.size() && filt[fi] < main_menu_docs_.size()) {
    open_path_request_ = main_menu_docs_[filt[fi]].path;
  }
}

void InputManager::ActivateMainMenuDelete(int row) {
  // Row 0 is New and has no ×. A second click on an armed tile deletes;
  // otherwise this click just arms it.
  const std::vector<size_t> filt = GetMainMenuFilteredIndices();
  if (row <= 0 || static_cast<size_t>(row - 1) >= filt.size()) {
    delete_armed_path_.clear();
    return;
  }
  const size_t di = filt[static_cast<size_t>(row - 1)];
  if (di >= main_menu_docs_.size()) {
    delete_armed_path_.clear();
    return;
  }
  const std::string& path = main_menu_docs_[di].path;
  if (IsDeleteArmedForPath(path)) {
    RequestDeleteMainMenuDoc();
    return;
  }
  delete_armed_path_ = path;
}

void InputManager::RequestDeleteMainMenuDoc() {
  const int row = main_menu_selected_;
  const std::vector<size_t> filt = GetMainMenuFilteredIndices();
  delete_armed_path_.clear();
  if (row <= 0 || static_cast<size_t>(row - 1) >= filt.size()) {
    return;
  }
  const size_t di = filt[static_cast<size_t>(row - 1)];
  if (di >= main_menu_docs_.size()) return;
  const DocEntry doc = main_menu_docs_[di];
  if (!DeleteDocument(doc.path)) return;
  deleted_doc_name_ = doc.name;
  RefreshMainMenuDocs();
}

void InputManager::SetChoosingDocType(bool v) {
  choosing_doc_type_ = v;
  choosing_size_ = false;
  editing_custom_size_ = false;
  custom_size_text_.clear();
  if (v) doc_type_choice_ = 0;
  if (pending_menu_dd_) {
    pending_menu_dd_ = false;
    AppendMainMenuSearch("d");  // flush any held-back 'd'
  }
  suppress_menu_text_once_ = false;
  delete_armed_path_.clear();
}

void InputManager::SetDocTypeChoice(int c) {
  doc_type_choice_ = std::clamp(c, 0, 2);
}

void InputManager::MoveDocTypeChoice(int dir) {
  doc_type_choice_ = std::clamp(doc_type_choice_ + (dir >= 0 ? 1 : -1), 0, 2);
}

void InputManager::ActivateDocTypeChoice() {
  if (static_cast<DocType>(doc_type_choice_) == DocType::kBounded ||
      static_cast<DocType>(doc_type_choice_) == DocType::kSlideshow) {
    // Paged documents ask for a canvas size next instead of creating.
    choosing_size_ = true;
    size_choice_ = 0;
    custom_size_text_.clear();
    editing_custom_size_ = false;
    return;
  }
  choosing_doc_type_ = false;
  RequestNewDoc(static_cast<DocType>(doc_type_choice_));
}

void InputManager::SetChoosingSize(bool v) {
  choosing_size_ = v;
  editing_custom_size_ = false;
  custom_size_text_.clear();
  if (v) size_choice_ = 0;
}

void InputManager::SetDocSizeChoice(int c) {
  // 0..4 presets, 5 = custom size.
  size_choice_ = std::clamp(c, 0, 5);
}

void InputManager::MoveDocSizeChoice(int dir) {
  size_choice_ = std::clamp(size_choice_ + (dir >= 0 ? 1 : -1), 0, 5);
}

void InputManager::ActivateDocSizeChoice() {
  if (editing_custom_size_) {
    float w = 0, h = 0;
    if (ParseCustomSize(&w, &h)) {
      choosing_size_ = false;
      choosing_doc_type_ = false;
      editing_custom_size_ = false;
      RequestNewDoc(static_cast<DocType>(doc_type_choice_), w, h);
    }
    return;
  }
  if (size_choice_ >= 5) {
    // Custom size: start typing it (Enter confirms, Esc backs out).
    editing_custom_size_ = true;
    custom_size_text_.clear();
    return;
  }
  int n = 0;
  const PageSizePreset* presets = PageSizePresets(&n);
  const int i = std::clamp(size_choice_, 0, std::max(0, n - 1));
  choosing_size_ = false;
  choosing_doc_type_ = false;
  RequestNewDoc(static_cast<DocType>(doc_type_choice_), presets[i].w,
                presets[i].h);
}

void InputManager::SetEditingCustomSize(bool v) {
  editing_custom_size_ = v;
  if (v) custom_size_text_.clear();
}

void InputManager::AppendCustomSizeText(const std::string& utf8) {
  if (utf8.empty() || custom_size_text_.size() >= 24) return;
  // Only resolution-like input: digits, separators, whitespace.
  // Anything else (letters, symbols) is ignored.
  for (unsigned char ch : utf8) {
    const bool ascii = ch < 0x80;
    const bool ok = !ascii || (ch >= '0' && ch <= '9') || ch == 'x' ||
                    ch == 'X' || ch == '*' || ch == '.' || ch == ',' ||
                    ch == ' ' || ch == '\t';
    if (ok) custom_size_text_ += static_cast<char>(ch);
  }
}

void InputManager::PopCustomSizeText() {
  while (!custom_size_text_.empty()) {
    const unsigned char last =
        static_cast<unsigned char>(custom_size_text_.back());
    custom_size_text_.pop_back();
    if ((last & 0xC0) != 0x80) break;
  }
}

bool InputManager::ParseCustomSize(float* w, float* h) const {
  if (w == nullptr || h == nullptr) return false;
  // Accept "W x H" with x, X, *, or whitespace separators.
  std::string norm = custom_size_text_;
  for (char& c : norm) {
    if (c == 'x' || c == 'X' || c == '*' || c == ',')
      c = ' ';
    else if (!((c >= '0' && c <= '9') || c == '.' || c == ' ' || c == '\t')) {
      return false;
    }
  }
  float a = 0, b = 0;
  if (std::sscanf(norm.c_str(), "%f %f", &a, &b) != 2) return false;
  if (!(a >= 1.0f) || !(b >= 1.0f)) return false;
  *w = std::min(a, 16384.0f);
  *h = std::min(b, 16384.0f);
  return true;
}

void InputManager::CancelDocSizeChoice() {
  // Back to the type step (Esc in the type step cancels everything).
  choosing_size_ = false;
  editing_custom_size_ = false;
  custom_size_text_.clear();
}

namespace {

ToolSettings DefaultToolSettings(Tool tool) {
  ToolSettings s;
  switch (tool) {
    case Tool::kPencil:
      s.hardness = 1.0f;
      s.flow = 1.0f;
      s.spacing = 0.15f;
      break;
    case Tool::kPen:
      s.hardness = 0.85f;
      s.flow = 1.0f;
      s.spacing = 0.12f;
      break;
    case Tool::kBrush:
      s.hardness = 0.35f;
      s.flow = 0.4f;
      s.spacing = 0.1f;
      break;
    case Tool::kMarker:
      s.hardness = 0.25f;
      s.flow = 0.18f;
      s.spacing = 0.08f;
      break;
    case Tool::kSpray:
      s.size = 21.0f;
      s.flow = 1.0f;
      s.spacing = 0.5f;
      break;
    case Tool::kStroke:
      s.hardness = 0.7f;
      s.spacing = 0.15f;
      break;
    default:
      break;
  }
  return s;
}

}  // namespace

const ToolSettings& InputManager::GetToolSettings(Tool tool) const {
  auto it = tool_settings_.find(tool);
  if (it == tool_settings_.end()) {
    it = tool_settings_.emplace(tool, DefaultToolSettings(tool)).first;
  }
  return it->second;
}

void InputManager::SetToolSetting(Tool tool, ToolSettingId id, float value) {
  ToolSettings& s = const_cast<ToolSettings&>(GetToolSettings(tool));
  switch (id) {
    case ToolSettingId::kSize:
      s.size = std::clamp(value, 1.0f, 200.0f);
      break;
    case ToolSettingId::kHardness:
      s.hardness = std::clamp(value, 0.0f, 1.0f);
      break;
    case ToolSettingId::kFlow:
      s.flow = std::clamp(value, 0.01f, 1.0f);
      break;
    case ToolSettingId::kSpacing:
      s.spacing = std::clamp(value, 0.01f, 2.0f);
      break;
    case ToolSettingId::kSprayCount:
      s.spray_count = std::max(0.0f, value);
      break;
    case ToolSettingId::kSpraySpread:
      s.spray_spread = std::clamp(value, 0.05f, 5.0f);
      break;
    case ToolSettingId::kSprayParticle:
      s.spray_particle = std::max(0.0f, value);
      break;
    case ToolSettingId::kTolerance:
      s.tolerance = std::clamp(value, 0.0f, 255.0f);
      break;
    case ToolSettingId::kStrokeWidth:
      s.stroke_width = std::clamp(value, 0.1f, 128.0f);
      break;
    case ToolSettingId::kFontSize:
      s.font_size = std::clamp(value, 1.0f, 256.0f);
      break;
  }
}

float InputManager::GetToolSize() const {
  return GetToolSettings(current_tool_).size;
}

void InputManager::SetToolSize(float size) {
  SetToolSetting(current_tool_, ToolSettingId::kSize, size);
}

void InputManager::MoveToolMenuRow(int dir) {
  if (tool_menu_tool_ == Tool::kNone) return;
  const int count = ToolMenuRowCount(tool_menu_tool_);
  int row = tool_menu_row_;
  for (int guard = 0; guard < count + 1; ++guard) {
    row += (dir >= 0 ? 1 : -1);
    if (row < 0 || row >= count) return;  // stop at the ends
    if (ToolMenuRowAdjustable(tool_menu_tool_, row)) {
      tool_menu_row_ = row;
      return;
    }
  }
}

void InputManager::AdjustToolMenuRow(int dir) {
  if (tool_menu_tool_ == Tool::kNone) return;
  StepToolMenuRow(*this, tool_menu_tool_, tool_menu_row_, dir >= 0 ? 1 : -1);
}

void InputManager::SetPaletteHovered(bool hovered) {
  palette_hovered_ = hovered;
  if (!palette_hovered_) {
    SetPaletteExpanded(false);
  }
}

void InputManager::SetPaletteExpanded(bool expanded) {
  palette_expanded_ = expanded;
  if (!palette_expanded_) {
    pending_palette_tone_ = -1;
  }
}

void InputManager::TogglePaletteExpanded() {
  SetPaletteExpanded(!palette_expanded_);
}

void InputManager::RequestPaletteSelection(int row, int tone) {
  palette_selection_row_ = row;
  palette_selection_tone_ = tone;
}

void InputManager::ClearPaletteSelectionRequest() {
  palette_selection_row_ = -1;
  palette_selection_tone_ = -1;
}

}  // namespace nanote
