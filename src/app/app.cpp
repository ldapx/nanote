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

bool App::Init() {
  if (!host.Init(kWindowWidth, kWindowHeight, "Nanote v0.13")) {
    return false;
  }
  window = host.window();

  viewport.window_width_ = window_width;
  viewport.window_height_ = window_height;

  input.SetToolSize(3.0f);
  input.SetDisplayedTone(color_manager.GetActivePaletteDefaultTone());

  ui_typeface = LoadUiTypeface();
  if (ui_typeface == nullptr) {
    SDL_Log("Warning: no UI font found, text will not render");
  }

  previous_tool = input.GetTool();
  last_time = SDL_GetTicks();

  cursor_default = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
  cursor_crosshair = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
  cursor_ibeam = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_TEXT);
  return true;
}

// Recreate the render targets after a window size change (host keeps the
// null-texture guarantee; zero-size configures are ignored).
void App::ResizeRenderTargets(int w, int h) {
  if (w <= 0 || h <= 0) return;
  window_width = w;
  window_height = h;
  viewport.window_width_ = window_width;
  viewport.window_height_ = window_height;
  host.Resize(window_width, window_height);
  input.SetPaletteHovered(IsPointInPaletteBounds(
      mouse_x, mouse_y, input, color_manager, window_width, window_height));
}

int App::Run() {
  if (!Init()) {
    return 1;
  }

  while (!input.QuitRequested()) {
    TickFrame();
    PollEvents();
    ProcessRequests();
    RenderFrame();
  }

  // Never lose work: persist unsaved changes before tearing down SDL.
  CommitFloat();
  AutosaveIfNeeded(session, layer_stack, viewport);

  SDL_DestroyCursor(cursor_default);
  SDL_DestroyCursor(cursor_crosshair);
  SDL_DestroyCursor(cursor_ibeam);
  // SdlHost destructor tears down the window/renderer/texture and SDL.

  return 0;
}
void App::TickFrame() {
  frame_now = SDL_GetTicks();
  frame_dt = static_cast<float>(frame_now - last_time) / 1000.0f;
  last_time = frame_now;

  input.Update(frame_dt);
  // Bounded documents can't zoom out past fitting the page. With a rotated
  // view the page's axis-aligned bounds grow, so fit those.
  if (session.props.has_page() && session.props.page_w > 0.0f &&
      session.props.page_h > 0.0f) {
    const float c = std::abs(std::cos(viewport.angle_));
    const float sn = std::abs(std::sin(viewport.angle_));
    const float ew = session.props.page_w * c + session.props.page_h * sn;
    const float eh = session.props.page_w * sn + session.props.page_h * c;
    viewport.SetMinZoom(std::min(window_width / ew, window_height / eh));
  } else {
    viewport.SetMinZoom(0.0f);
  }
  viewport.Update(frame_dt);

  // Text cursor blink.
  if (text_editor.active) {
    text_editor.blink_timer += frame_dt;
    if (text_editor.blink_timer > 0.5f) {
      text_editor.blink_timer -= 0.5f;
      text_editor.cursor_visible = !text_editor.cursor_visible;
    }
  }

  input.SetPaletteHovered(IsPointInPaletteBounds(
      mouse_x, mouse_y, input, color_manager, window_width, window_height));
}

void App::PollEvents() {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    HandleEvent(event);
  }
}
}  // namespace nanote
