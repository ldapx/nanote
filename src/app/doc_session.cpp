#include "app/doc_session.h"

#include <string>

namespace nanote {

void AutosaveIfNeeded(OpenDocument& doc, const LayerStack& layers,
                      const Viewport& view) {
  if (!doc.dirty) return;
  std::string name = doc.name.empty() ? "Untitled" : doc.name;
  if (name == "Untitled") name = UniqueUntitledName();
  const std::string path = PathForDocName(name);
  if (path.empty()) return;
  if (SaveDocument(path, layers, view, doc.props)) {
    doc.name = DocNameFromPath(path);
    doc.dirty = false;
  }
}

void UpdateWindowTitle(SDL_Window* window, const OpenDocument& doc,
                       bool in_menu) {
  static std::string last;
  std::string title = in_menu ? "Nanote — Documents"
                              : "Nanote — " + doc.name + (doc.dirty ? "*" : "");
  if (title != last) {
    last = title;
    SDL_SetWindowTitle(window, title.c_str());
  }
}

}  // namespace nanote
