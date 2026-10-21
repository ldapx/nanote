#include "app/app.h"

#include "render/canvas.h"
#include "ui/ui_renderer.h"

namespace nanote {

// Document content only (background + layers, no editing chrome or
// screen-space UI). Shared by the canvas renderer and the menu-enter
// transition, which replays it while zooming out.
void App::RenderDocumentContent(Canvas& canvas) {
  const bool bounded = session.props.has_page();
  // Off-limits is solid gray; the page holds the checkerboard.
  canvas.clear(bounded ? MakeARGB(255, 128, 128, 128)
                       : MakeARGB(255, 26, 26, 26));

  // Canvas-space rendering.
  canvas.save();
  canvas.concat(viewport.GetCanvasToScreen());

  Rect page_rect = Rect::MakeEmpty();
  if (bounded) {
    page_rect = Rect::MakeXYWH(-session.props.page_w * 0.5f,
                               -session.props.page_h * 0.5f,
                               session.props.page_w, session.props.page_h);
  }

  if (viewport.IsGridVisible()) {
    if (bounded) {
      DrawCheckerboardRect(&canvas, viewport.zoom_, viewport.GetGridSize(),
                           page_rect);
    } else {
      DrawCheckerboard(&canvas, viewport.zoom_, viewport.GetGridSize());
    }
  }

  // Composite layers bottom-to-top.
  const auto& layers = layer_stack.GetLayers();
  for (const auto& layer : layers) {
    if (layer->visible) {
      layer->Render(&canvas);
    }
  }

  if (bounded && !page_rect.isEmpty()) {
    // Solid gray over everything outside the page, then the outline.
    const Paint cover = Paint::Fill(MakeARGB(255, 128, 128, 128));
    const float vl = viewport.VisibleLeft();
    const float vr = viewport.VisibleRight();
    const float vt = viewport.VisibleTop();
    const float vb = viewport.VisibleBottom();
    auto cover_rect = [&](float l, float t, float r, float b) {
      const Rect rr = Rect::MakeLTRB(std::max(l, vl), std::max(t, vt),
                                     std::min(r, vr), std::min(b, vb));
      if (!rr.isEmpty()) canvas.drawRect(rr, cover);
    };
    cover_rect(vl, vt, page_rect.left, vb);
    cover_rect(page_rect.right, vt, vr, vb);
    cover_rect(page_rect.left, vt, page_rect.right, page_rect.top);
    cover_rect(page_rect.left, page_rect.bottom, page_rect.right, vb);
    Paint edge =
        Paint::Stroke(MakeARGB(255, 110, 110, 115), 1.5f / viewport.zoom_);
    canvas.drawRect(page_rect, edge);
  }

  canvas.restore();
}

}  // namespace nanote
