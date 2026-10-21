#pragma once

#include "core/geometry.h"
#include "core/path.h"
#include "core/types.h"

#include <memory>
#include <vector>

namespace nanote {

class PathObject;
class VectorLayer;
class Canvas;

enum class NodeHitType {
  kNone,
  kAnchor,
  kCpIn,
  kCpOut,
};

struct Node {
  Vec2 pos;
  Vec2 cp_in;
  Vec2 cp_out;
  bool smooth = false;
};

struct EditablePath {
  std::vector<Node> nodes;
  bool closed = false;

  static EditablePath FromPath(const Path& path);
  [[nodiscard]] Path ToPath() const;
};

struct NodeEditorState {
  bool editing = false;
  int path_object_index = -1;
  EditablePath path;
  int selected_node = -1;
  NodeHitType selected_type = NodeHitType::kNone;
  int hovered_node = -1;
  NodeHitType hovered_type = NodeHitType::kNone;
  bool dragging = false;
  Vec2 drag_start_canvas;

  // For join tool: first endpoint selected
  bool join_pending = false;
  int join_source_object = -1;
  int join_source_node = -1;

  void Clear();
  void BeginEdit(PathObject* obj, int index);
  void Commit(VectorLayer* layer);
};

// Find nearest node or control point to (x,y). Returns true if found.
bool HitTestNodes(const EditablePath& path, float x, float y, float tolerance,
                  int* out_index, NodeHitType* out_type);

// Find nearest path segment to (x,y). Returns node index after which to insert.
// For closed paths, may return 0 to indicate the closing segment.
[[nodiscard]] int FindNearestSegment(const EditablePath& path, float x,
                                     float y);

// Add a node at the midpoint of the segment ending at `segment_index`.
void InsertNodeAtSegment(EditablePath& path, int segment_index);

// Move node or control point.
void MoveNode(EditablePath& path, int index, NodeHitType type, float x,
              float y);

// Delete node.
void DeleteNode(EditablePath& path, int index);

// Set smooth/corner.
void SetNodeSmooth(EditablePath& path, int index, bool smooth);

// Break path at node. For closed paths, opens them. For open paths at interior,
// splits into two paths. Returns the second path if split, or nullptr if just
// opened.
[[nodiscard]] std::unique_ptr<EditablePath> BreakPathAtNode(EditablePath& path,
                                                            int index);

// Join two open paths at their endpoints. Returns merged path.
[[nodiscard]] EditablePath JoinPaths(const EditablePath& a, int node_a,
                                     const EditablePath& b, int node_b);

// Render node editing UI.
void DrawNodeEditor(Canvas* canvas, const NodeEditorState& editor, float zoom);

}  // namespace nanote
