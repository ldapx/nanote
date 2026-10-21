#include "app/app.h"

#include "app/doc_session.h"
#include "app/file_dialog.h"
#include "canvas/viewport.h"
#include "color/color_manager.h"
#include "command/command_executor.h"
#include "core/color.h"
#include "core/easing.h"
#include "core/font.h"
#include "core/geometry.h"
#include "core/path.h"
#include "core/types.h"
#include "doc/document.h"
#include "drawing/brush_engine.h"
#include "drawing/drawing.h"
#include "input/input_manager.h"
#include "layer/layer.h"
#include "render/canvas.h"
#include "ui/main_menu.h"
#include "ui/tool_menu.h"
#include "ui/ui_hit_test.h"
#include "ui/ui_renderer.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace nanote {

void App::ProcessRequests() {
  if (input.ConsumeUndoRequest()) {
    Undo();
  }
  if (input.ConsumeRedoRequest()) {
    Redo();
  }
  ProcessCommandBar();
  ProcessDocRequests();
  UpdateWindowTitle(window, session, input.IsInMainMenu());
  ProcessSelectionRequests();
  ProcessModeExits();
  ProcessNavigation();
  UpdateCursor();
}

void App::ProcessCommandBar() {
  // Auto-compute completion candidates while in command mode.
  if (input.IsCommandModeActive()) {
    auto candidates =
        CommandExecutor::GetCompletions(input.GetCompletionFilter());
    input.SetCompletionCandidates(candidates);
  }

  // Execute pending command on Enter.
  if (!input.IsCommandModeActive() && !input.IsInMainMenu() &&
      !input.GetCommandBuffer().empty()) {
    const std::string cmd = input.GetCommandBuffer();
    const Vec2 cursor_canvas = viewport.ScreenToCanvas(mouse_x, mouse_y);
    const int prev_palette_index = color_manager.GetCurrentPaletteIndex();
    BeginHistory();
    CommitFloat();  // anchor floating pixels
                    // before the command runs
    executor.Execute(cmd, layer_stack, input, viewport, color_manager,
                     cursor_canvas, session);
    CommitHistory();
    if (color_manager.GetCurrentPaletteIndex() != prev_palette_index) {
      input.SetDisplayedTone(color_manager.GetActivePaletteDefaultTone());
    }
    input.PushCommandHistory(cmd);
    input.ClearCommandBuffer();
  }
}

void App::ProcessDocRequests() {
  // Native file-dialog results (queued from another thread).
  {
    const std::vector<std::string> picked = TakePendingDialogPaths();
    for (const std::string& p : picked) {
      if (!p.empty()) input.RequestOpenPath(p);
    }
  }
  // Open / new document requests (main menu or :open / :new commands). Either
  // one leaves the menu, so any enter-menu flight is cancelled.
  if (input.ConsumeNewDocRequest()) {
    CommitFloat();
    AutosaveIfNeeded(session, layer_stack, viewport);
    const InputManager::NewDocRequest req = input.TakeNewDoc();
    NewDocument(req.type, req.page_w, req.page_h);
    input.SetInMainMenu(false);
    input.RefreshMainMenuDocs();
    was_in_menu = false;
    menu_transition.active = false;
  }
  if (input.HasOpenPathRequest()) {
    const std::string req_path = input.TakeOpenPathRequest();
    CommitFloat();
    AutosaveIfNeeded(session, layer_stack, viewport);
    if (OpenPath(req_path)) {
      input.SetInMainMenu(false);
      input.RefreshMainMenuDocs();
    }
    was_in_menu = input.IsInMainMenu();
    if (!input.IsInMainMenu()) menu_transition.active = false;
  }
  // "..." browse button: native .nano picker (async; result opens above).
  if (input.ConsumeBrowseRequest()) {
    static const SDL_DialogFileFilter filters[] = {
        {"Nanote documents", "nano"},
    };
    SDL_ShowOpenFileDialog(OnOpenDocDialog, nullptr, window, filters, 1,
                           DocumentsDir().c_str(), false);
  }
  // Deleting the open document from the menu moves the in-memory session to a
  // fresh Untitled slot (dirty, so it persists); the deleted file itself
  // stays deleted.
  if (input.HasDeletedDocRequest()) {
    const std::string gone = input.TakeDeletedDocRequest();
    if (!gone.empty() && gone == session.name) {
      session.name = UniqueUntitledName();
      session.dirty = true;
    }
  }
}

void App::ProcessSelectionRequests() {
  if (input.HasPaletteSelectionRequest()) {
    const int row = input.GetPaletteSelectionRow();
    const int tone = input.GetPaletteSelectionTone();
    const Palette& palette = color_manager.GetActivePalette();
    if (row >= 0 && row < palette.GetRowCount() && tone >= 0 &&
        tone < palette.GetToneCount()) {
      color_manager.SetActivePaletteCell(row, tone);
    }
    input.ClearPaletteSelectionRequest();
    input.ClearPendingPaletteTone();
  }
  if (input.GetLayerSelectReq() >= 0) {
    const int layer_count = static_cast<int>(layer_stack.GetLayers().size());
    if (input.GetLayerSelectReq() < layer_count) {
      // z=0 maps to top layer (highest index), x=1 to next, etc.
      const int index = layer_count - 1 - input.GetLayerSelectReq();
      CommitFloat();  // anchor before leaving
                      // the layer
      layer_stack.SetActiveLayerIndex(index);
    }
    input.SetLayerSelectReq(-1);
  }
}

void App::ProcessModeExits() {
  // If we left node mode, commit any pending node edits.
  if (input.GetMode() != Mode::kNode && node_editor.editing) {
    Layer* active =
        layer_stack.GetLayers()[layer_stack.GetActiveLayerIndex()].get();
    if (active->type == LayerType::kVector) {
      auto* vector = static_cast<VectorLayer*>(active);
      node_editor.Commit(vector);
      session.dirty = true;
    }
    node_editor.Clear();
  }

  // Apply smooth/corner immediately if the tool changed while a node is
  // selected.
  if (input.GetMode() == Mode::kNode && node_editor.editing &&
      node_editor.selected_node >= 0 && input.GetTool() != previous_tool) {
    if (input.GetTool() == Tool::kSmooth) {
      SetNodeSmooth(node_editor.path, node_editor.selected_node, true);
      Layer* active =
          layer_stack.GetLayers()[layer_stack.GetActiveLayerIndex()].get();
      if (active->type == LayerType::kVector) {
        auto* vector = static_cast<VectorLayer*>(active);
        node_editor.Commit(vector);
        session.dirty = true;
      }
    } else if (input.GetTool() == Tool::kCorner) {
      SetNodeSmooth(node_editor.path, node_editor.selected_node, false);
      Layer* active =
          layer_stack.GetLayers()[layer_stack.GetActiveLayerIndex()].get();
      if (active->type == LayerType::kVector) {
        auto* vector = static_cast<VectorLayer*>(active);
        node_editor.Commit(vector);
        session.dirty = true;
      }
    }
  }
  previous_tool = input.GetTool();

  if (input.IsCommandModeActive() || text_editor.active ||
      input.IsInMainMenu()) {
    SDL_StartTextInput(window);
  } else {
    SDL_StopTextInput(window);
  }

  // If we left text mode, commit any pending text edits.
  if (input.GetMode() != Mode::kText && text_editor.active) {
    BeginHistory();
    Layer* active =
        layer_stack.GetLayers()[layer_stack.GetActiveLayerIndex()].get();
    CommitEditorText(active, text_editor.text, text_editor.x, text_editor.y,
                     text_editor.font_size, text_editor.object_index,
                     color_manager.GetCurrentColor(), ui_typeface,
                     BoundedPage());
    CommitHistory();
    session.dirty = true;
    text_editor.active = false;
  }

  // Reset ruler angle when leaving ruler mode.
  if (input.GetMode() != Mode::kRuler) {
    ruler.angle_stage = 0;
    ruler.measuring = false;
  }
}

void App::ProcessNavigation() {
  if (input.GetMode() == Mode::kNavigate) {
    const float pan_amount = kPanSpeed * frame_dt / viewport.zoom_;
    if (input.WantsPanLeft()) {
      viewport.Pan(-pan_amount, 0);
    }
    if (input.WantsPanRight()) {
      viewport.Pan(pan_amount, 0);
    }
    if (input.WantsPanUp()) {
      viewport.Pan(0, -pan_amount);
    }
    if (input.WantsPanDown()) {
      viewport.Pan(0, pan_amount);
    }
  }

  if (input.ConsumeZoomIn()) {
    viewport.ZoomIn();
  }
  if (input.ConsumeZoomOut()) {
    viewport.ZoomOut();
  }
  if (input.ConsumeResetZoom()) {
    viewport.ResetZoom();
  }
}

void App::UpdateCursor() {
  // Cursor feedback.
  if (input.IsInMainMenu()) {
    SDL_SetCursor(cursor_default);
  } else if (input.GetMode() == Mode::kDraw ||
             input.GetMode() == Mode::kShape ||
             input.GetMode() == Mode::kErase ||
             input.GetMode() == Mode::kSelect ||
             input.GetMode() == Mode::kFill) {
    SDL_SetCursor(cursor_crosshair);
  } else if (input.GetMode() == Mode::kTransform && gizmo.dragging) {
    SDL_SetCursor(cursor_crosshair);
  } else if (input.GetMode() == Mode::kText) {
    SDL_SetCursor(text_editor.active ? cursor_ibeam : cursor_crosshair);
  } else if (input.GetMode() == Mode::kRuler) {
    SDL_SetCursor(cursor_crosshair);
  } else {
    SDL_SetCursor(cursor_default);
  }
}

}  // namespace nanote
