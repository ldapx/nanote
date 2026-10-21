#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "core/selection.h"
#include "doc/document.h"
#include "layer/layer.h"

namespace nanote {

// A restorable document state: deep layer copies plus selection, active
// layer and document properties. Viewport, tools, palette and other session
// state are intentionally NOT part of history (panning/zooming and tool
// changes are never undone).
struct DocSnapshot {
  std::vector<std::unique_ptr<Layer>> layers;
  Selection selection;
  int active_layer = 0;
  DocProps props;
};

// Unlimited undo/redo over whole-document snapshots. Each mutating gesture
// or command brackets its work in Begin/Commit: Begin stores a pre-state,
// Commit pushes it onto the undo stack only if the document actually
// changed (fingerprinted), so no-op gestures leave no trace and never
// clear the redo stack. Nested Begin/Commit pairs fold into the outermost
// transaction.
class HistoryManager {
 public:
  // Begins a transaction, snapshotting the pre-state. Nested calls are
  // ignored (the outer transaction owns the snapshot).
  //
  // NOTE: the current slide index is navigation state and is deliberately
  // excluded from undo: fingerprints skip props.current_slide and restores
  // preserve the live value (clamped to the restored deck) instead of
  // overwriting it.
  void Begin(const LayerStack& layers, const DocProps& props);
  // Ends a transaction. Pushes the pre-state onto undo and clears redo
  // iff the document changed since Begin. Always clears the pending slot.
  void Commit(const LayerStack& layers, const DocProps& props);
  void Clear();

  [[nodiscard]] bool CanUndo() const {
    return !undo_.empty();
  }
  [[nodiscard]] bool CanRedo() const {
    return !redo_.empty();
  }

  // Restores the top entry, moving the current state to the opposite
  // stack. Returns false when the respective stack is empty.
  bool Undo(LayerStack& layers, DocProps& props);
  bool Redo(LayerStack& layers, DocProps& props);

 private:
  static DocSnapshot TakeSnapshot(const LayerStack& layers,
                                  const DocProps& props);
  static uint64_t Fingerprint(const DocSnapshot& snap);
  static uint64_t FingerprintCurrent(const LayerStack& layers,
                                     const DocProps& props);
  static void Restore(LayerStack& layers, DocProps& props,
                      const DocSnapshot& snap);

  std::vector<DocSnapshot> undo_;
  std::vector<DocSnapshot> redo_;
  DocSnapshot pending_;
  uint64_t pending_fp_ = 0;
  bool pending_open_ = false;
  int depth_ = 0;
};

}  // namespace nanote
