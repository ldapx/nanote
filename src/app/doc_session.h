#pragma once

#include "canvas/viewport.h"
#include "doc/document.h"
#include "layer/layer.h"

#include <SDL3/SDL.h>

namespace nanote {

// Autosave the open document if it has unsaved changes. Untitled documents
// are filed under an auto-generated slot; pristine documents are skipped so
// no junk files appear in the documents folder.
void AutosaveIfNeeded(OpenDocument& doc, const LayerStack& layers,
                      const Viewport& view);

void UpdateWindowTitle(SDL_Window* window, const OpenDocument& doc,
                       bool in_menu);

}  // namespace nanote
