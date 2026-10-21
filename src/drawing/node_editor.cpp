#include "drawing/node_editor.h"
#include "layer/layer.h"
#include "render/canvas.h"

#include <algorithm>
#include <cmath>

namespace nanote {

namespace {

Vec2 EvaluateCubicMidpoint(const Vec2& p0, const Vec2& c0, const Vec2& c1,
                           const Vec2& p1) {
  Vec2 q0{(p0.x + c0.x) * 0.5f, (p0.y + c0.y) * 0.5f};
  Vec2 q1{(c0.x + c1.x) * 0.5f, (c0.y + c1.y) * 0.5f};
  Vec2 q2{(c1.x + p1.x) * 0.5f, (c1.y + p1.y) * 0.5f};
  Vec2 r0{(q0.x + q1.x) * 0.5f, (q0.y + q1.y) * 0.5f};
  Vec2 r1{(q1.x + q2.x) * 0.5f, (q1.y + q2.y) * 0.5f};
  return Vec2{(r0.x + r1.x) * 0.5f, (r0.y + r1.y) * 0.5f};
}

Vec2 EvaluateCubicMidpoint(const Node& a, const Node& b) {
  return EvaluateCubicMidpoint(a.pos, a.cp_out, b.cp_in, b.pos);
}

EditablePath Reversed(const EditablePath& path) {
  EditablePath result;
  result.closed = path.closed;
  for (int i = static_cast<int>(path.nodes.size()) - 1; i >= 0; --i) {
    Node n = path.nodes[i];
    std::swap(n.cp_in, n.cp_out);
    result.nodes.push_back(n);
  }
  return result;
}

}  // namespace

// ------------------------------------------------------------------
// EditablePath
// ------------------------------------------------------------------

EditablePath EditablePath::FromPath(const Path& path) {
  EditablePath result;
  bool has_pos = false;
  Vec2 cur{0, 0};
  for (const auto& c : path.cmds()) {
    switch (c.verb) {
      case 0:  // move
        result.nodes.push_back({{c.pts[0], c.pts[1]},
                                {c.pts[0], c.pts[1]},
                                {c.pts[0], c.pts[1]},
                                false});
        cur = {c.pts[0], c.pts[1]};
        has_pos = true;
        break;
      case 1: {  // line
        if (!has_pos) {
          result.nodes.push_back({{c.pts[0], c.pts[1]},
                                  {c.pts[0], c.pts[1]},
                                  {c.pts[0], c.pts[1]},
                                  false});
          has_pos = true;
          break;
        }
        result.nodes.back().cp_out = result.nodes.back().pos;
        result.nodes.push_back({{c.pts[0], c.pts[1]},
                                {c.pts[0], c.pts[1]},
                                {c.pts[0], c.pts[1]},
                                false});
        cur = {c.pts[0], c.pts[1]};
        break;
      }
      case 2: {  // quad -> cubic
        Vec2 p0 = cur;
        Vec2 qc{c.pts[0], c.pts[1]};
        Vec2 p2{c.pts[2], c.pts[3]};
        if (!has_pos) {
          result.nodes.push_back({p0, p0, p0, false});
          has_pos = true;
        }
        Vec2 c0{(p0.x + 2 * qc.x) / 3.0f, (p0.y + 2 * qc.y) / 3.0f};
        Vec2 c1{(p2.x + 2 * qc.x) / 3.0f, (p2.y + 2 * qc.y) / 3.0f};
        result.nodes.back().cp_out = c0;
        result.nodes.push_back({p2, c1, p2, false});
        cur = p2;
        break;
      }
      case 3: {  // cubic
        Vec2 p0 = cur;
        Vec2 c0{c.pts[0], c.pts[1]};
        Vec2 c1{c.pts[2], c.pts[3]};
        Vec2 p3{c.pts[4], c.pts[5]};
        if (!has_pos) {
          result.nodes.push_back({p0, p0, p0, false});
          has_pos = true;
        }
        result.nodes.back().cp_out = c0;
        result.nodes.push_back({p3, c1, p3, false});
        cur = p3;
        break;
      }
      case 4:
        result.closed = true;
        break;
    }
  }

  if (result.nodes.size() == 1) {
    result.nodes[0].cp_in = result.nodes[0].pos;
    result.nodes[0].cp_out = result.nodes[0].pos;
  }

  return result;
}

Path EditablePath::ToPath() const {
  Path path;
  if (nodes.empty()) return path;

  path.moveTo(nodes[0].pos.x, nodes[0].pos.y);
  for (size_t i = 1; i < nodes.size(); ++i) {
    path.cubicTo(nodes[i - 1].cp_out.x, nodes[i - 1].cp_out.y, nodes[i].cp_in.x,
                 nodes[i].cp_in.y, nodes[i].pos.x, nodes[i].pos.y);
  }
  if (closed && nodes.size() > 1) {
    path.cubicTo(nodes.back().cp_out.x, nodes.back().cp_out.y, nodes[0].cp_in.x,
                 nodes[0].cp_in.y, nodes[0].pos.x, nodes[0].pos.y);
    path.close();
  }
  return path;
}

// ------------------------------------------------------------------
// NodeEditorState
// ------------------------------------------------------------------

void NodeEditorState::Clear() {
  editing = false;
  path_object_index = -1;
  path = EditablePath();
  selected_node = -1;
  selected_type = NodeHitType::kNone;
  hovered_node = -1;
  hovered_type = NodeHitType::kNone;
  dragging = false;
  join_pending = false;
  join_source_object = -1;
  join_source_node = -1;
}

void NodeEditorState::BeginEdit(PathObject* obj, int index) {
  if (obj == nullptr) return;
  Clear();
  path_object_index = index;
  path = EditablePath::FromPath(obj->path);
  editing = true;
}

void NodeEditorState::Commit(VectorLayer* layer) {
  if (!editing || path_object_index < 0 || layer == nullptr) return;
  if (path_object_index >= static_cast<int>(layer->objects.size())) return;

  auto* path_obj =
      dynamic_cast<PathObject*>(layer->objects[path_object_index].get());
  if (path_obj != nullptr) {
    path_obj->path = path.ToPath();
  }
}

// ------------------------------------------------------------------
// Hit testing
// ------------------------------------------------------------------

bool HitTestNodes(const EditablePath& path, float x, float y, float tolerance,
                  int* out_index, NodeHitType* out_type) {
  float best_dist = tolerance * tolerance;
  bool found = false;

  for (int i = 0; i < static_cast<int>(path.nodes.size()); ++i) {
    const Node& n = path.nodes[i];

    float dx = n.pos.x - x;
    float dy = n.pos.y - y;
    float dist = dx * dx + dy * dy;
    if (dist < best_dist) {
      best_dist = dist;
      *out_index = i;
      *out_type = NodeHitType::kAnchor;
      found = true;
    }

    if (path.closed || i > 0) {
      float dx_in = n.cp_in.x - x;
      float dy_in = n.cp_in.y - y;
      float dist_in = dx_in * dx_in + dy_in * dy_in;
      if (dist_in < best_dist) {
        best_dist = dist_in;
        *out_index = i;
        *out_type = NodeHitType::kCpIn;
        found = true;
      }
    }

    if (path.closed || i < static_cast<int>(path.nodes.size()) - 1) {
      float dx_out = n.cp_out.x - x;
      float dy_out = n.cp_out.y - y;
      float dist_out = dx_out * dx_out + dy_out * dy_out;
      if (dist_out < best_dist) {
        best_dist = dist_out;
        *out_index = i;
        *out_type = NodeHitType::kCpOut;
        found = true;
      }
    }
  }

  return found;
}

int FindNearestSegment(const EditablePath& path, float x, float y) {
  int best_index = -1;
  float best_dist = 1e9f;

  for (size_t i = 1; i < path.nodes.size(); ++i) {
    Vec2 m = EvaluateCubicMidpoint(path.nodes[i - 1], path.nodes[i]);
    float dist = std::hypot(m.x - x, m.y - y);
    if (dist < best_dist) {
      best_dist = dist;
      best_index = static_cast<int>(i);
    }
  }

  if (path.closed && path.nodes.size() > 1) {
    Vec2 m = EvaluateCubicMidpoint(path.nodes.back(), path.nodes[0]);
    float dist = std::hypot(m.x - x, m.y - y);
    if (dist < best_dist) {
      best_index = 0;
    }
  }

  return best_index;
}

// ------------------------------------------------------------------
// Node manipulation
// ------------------------------------------------------------------

void InsertNodeAtSegment(EditablePath& path, int segment_index) {
  auto split_segment = [](const Vec2& p0, const Vec2& c0, const Vec2& c1,
                          const Vec2& p1, Node* out_prev, Node* out_new,
                          Node* out_next) {
    Vec2 q0{(p0.x + c0.x) * 0.5f, (p0.y + c0.y) * 0.5f};
    Vec2 q1{(c0.x + c1.x) * 0.5f, (c0.y + c1.y) * 0.5f};
    Vec2 q2{(c1.x + p1.x) * 0.5f, (c1.y + p1.y) * 0.5f};
    Vec2 r0{(q0.x + q1.x) * 0.5f, (q0.y + q1.y) * 0.5f};
    Vec2 r1{(q1.x + q2.x) * 0.5f, (q1.y + q2.y) * 0.5f};
    Vec2 s{(r0.x + r1.x) * 0.5f, (r0.y + r1.y) * 0.5f};

    out_prev->cp_out = q0;
    out_new->pos = s;
    out_new->cp_in = r0;
    out_new->cp_out = r1;
    out_new->smooth = true;
    out_next->cp_in = q2;
  };

  if (segment_index <= 0) {
    if (!path.closed || path.nodes.size() < 2) return;
    Node new_node;
    split_segment(path.nodes.back().pos, path.nodes.back().cp_out,
                  path.nodes[0].cp_in, path.nodes[0].pos, &path.nodes.back(),
                  &new_node, &path.nodes[0]);
    path.nodes.push_back(new_node);
    return;
  }

  if (segment_index >= static_cast<int>(path.nodes.size())) return;

  Node new_node;
  split_segment(
      path.nodes[segment_index - 1].pos, path.nodes[segment_index - 1].cp_out,
      path.nodes[segment_index].cp_in, path.nodes[segment_index].pos,
      &path.nodes[segment_index - 1], &new_node, &path.nodes[segment_index]);
  path.nodes.insert(path.nodes.begin() + segment_index, new_node);
}

void MoveNode(EditablePath& path, int index, NodeHitType type, float x,
              float y) {
  if (index < 0 || index >= static_cast<int>(path.nodes.size())) return;
  Node& n = path.nodes[index];

  if (type == NodeHitType::kAnchor) {
    float dx = x - n.pos.x;
    float dy = y - n.pos.y;
    n.pos.x += dx;
    n.pos.y += dy;
    n.cp_in.x += dx;
    n.cp_in.y += dy;
    n.cp_out.x += dx;
    n.cp_out.y += dy;
  } else if (type == NodeHitType::kCpIn) {
    n.cp_in.x = x;
    n.cp_in.y = y;
    if (n.smooth) {
      n.cp_out.x = n.pos.x * 2.0f - x;
      n.cp_out.y = n.pos.y * 2.0f - y;
    }
  } else if (type == NodeHitType::kCpOut) {
    n.cp_out.x = x;
    n.cp_out.y = y;
    if (n.smooth) {
      n.cp_in.x = n.pos.x * 2.0f - x;
      n.cp_in.y = n.pos.y * 2.0f - y;
    }
  }
}

void DeleteNode(EditablePath& path, int index) {
  if (index < 0 || index >= static_cast<int>(path.nodes.size())) return;
  if (path.nodes.size() <= 1) {
    path.nodes.clear();
    path.closed = false;
    return;
  }

  path.nodes.erase(path.nodes.begin() + index);

  if (!path.closed && !path.nodes.empty()) {
    path.nodes[0].cp_in = path.nodes[0].pos;
  }
  if (!path.closed && path.nodes.size() > 1) {
    path.nodes.back().cp_out = path.nodes.back().pos;
  }
}

void SetNodeSmooth(EditablePath& path, int index, bool smooth) {
  if (index < 0 || index >= static_cast<int>(path.nodes.size())) return;
  Node& n = path.nodes[index];
  n.smooth = smooth;

  if (smooth) {
    float angle_in = std::atan2(n.cp_in.y - n.pos.y, n.cp_in.x - n.pos.x);
    float angle_out = std::atan2(n.cp_out.y - n.pos.y, n.cp_out.x - n.pos.x);

    float sin_in = std::sin(angle_in);
    float cos_in = std::cos(angle_in);
    float sin_out = std::sin(angle_out);
    float cos_out = std::cos(angle_out);

    float avg_sin = sin_in + sin_out;
    float avg_cos = cos_in + cos_out;
    float avg_angle = std::atan2(avg_sin, avg_cos);

    float dist_in = std::hypot(n.cp_in.x - n.pos.x, n.cp_in.y - n.pos.y);
    float dist_out = std::hypot(n.cp_out.x - n.pos.x, n.cp_out.y - n.pos.y);

    if (dist_in < 0.001f) dist_in = 0.001f;
    if (dist_out < 0.001f) dist_out = 0.001f;

    n.cp_in.x = n.pos.x + std::cos(avg_angle + 3.14159265f) * dist_in;
    n.cp_in.y = n.pos.y + std::sin(avg_angle + 3.14159265f) * dist_in;
    n.cp_out.x = n.pos.x + std::cos(avg_angle) * dist_out;
    n.cp_out.y = n.pos.y + std::sin(avg_angle) * dist_out;
  }
}

std::unique_ptr<EditablePath> BreakPathAtNode(EditablePath& path, int index) {
  if (path.nodes.empty()) return nullptr;

  if (path.closed) {
    std::vector<Node> reordered;
    reordered.reserve(path.nodes.size());
    for (size_t i = index; i < path.nodes.size(); ++i) {
      reordered.push_back(path.nodes[i]);
    }
    for (size_t i = 0; i < static_cast<size_t>(index); ++i) {
      reordered.push_back(path.nodes[i]);
    }
    path.nodes = std::move(reordered);
    path.closed = false;
    path.nodes[0].cp_in = path.nodes[0].pos;
    path.nodes.back().cp_out = path.nodes.back().pos;
    return nullptr;
  }

  if (index <= 0 || index >= static_cast<int>(path.nodes.size()) - 1) {
    return nullptr;
  }

  auto second = std::make_unique<EditablePath>();
  second->closed = false;
  for (size_t i = index; i < path.nodes.size(); ++i) {
    second->nodes.push_back(path.nodes[i]);
  }
  second->nodes[0].cp_in = second->nodes[0].pos;
  second->nodes.back().cp_out = second->nodes.back().pos;

  path.nodes.resize(index + 1);
  path.nodes.back().cp_out = path.nodes.back().pos;
  return second;
}

EditablePath JoinPaths(const EditablePath& a, int node_a, const EditablePath& b,
                       int node_b) {
  EditablePath result;

  bool a_is_start = (node_a == 0);
  bool a_is_end = (node_a == static_cast<int>(a.nodes.size()) - 1);
  bool b_is_start = (node_b == 0);
  bool b_is_end = (node_b == static_cast<int>(b.nodes.size()) - 1);

  if (!a_is_start && !a_is_end) return a;
  if (!b_is_start && !b_is_end) return a;

  EditablePath lhs = a;
  EditablePath rhs = b;

  if (a_is_end && b_is_start) {
  } else if (a_is_start && b_is_end) {
    lhs = b;
    rhs = a;
  } else if (a_is_end && b_is_end) {
    rhs = Reversed(b);
  } else if (a_is_start && b_is_start) {
    lhs = Reversed(a);
    rhs = b;
  }

  result.closed = false;
  result.nodes = lhs.nodes;
  for (size_t i = 1; i < rhs.nodes.size(); ++i) {
    result.nodes.push_back(rhs.nodes[i]);
  }

  return result;
}

// ------------------------------------------------------------------
// Rendering
// ------------------------------------------------------------------

void DrawNodeEditor(Canvas* canvas, const NodeEditorState& editor, float zoom) {
  if (!editor.editing) return;

  const float anchor_size = 6.0f / zoom;
  const float cp_size = 4.0f / zoom;
  const float line_width = 1.0f / zoom;

  Paint handle_line = Paint::Stroke(MakeARGB(150, 200, 200, 200), line_width);
  Paint anchor = Paint::Fill(MakeARGB(255, 255, 255, 255));
  Paint anchor_sel = Paint::Fill(MakeARGB(255, 100, 200, 255));
  Paint anchor_hov = Paint::Fill(MakeARGB(255, 150, 220, 255));
  Paint cp = Paint::Fill(MakeARGB(255, 200, 200, 200));
  Paint cp_sel = Paint::Fill(MakeARGB(255, 100, 200, 255));
  Paint cp_hov = Paint::Fill(MakeARGB(255, 150, 220, 255));

  for (size_t i = 0; i < editor.path.nodes.size(); ++i) {
    const Node& n = editor.path.nodes[i];
    if (editor.path.closed || i > 0) {
      canvas->drawLine(n.pos.x, n.pos.y, n.cp_in.x, n.cp_in.y, handle_line);
    }
    if (editor.path.closed || i < editor.path.nodes.size() - 1) {
      canvas->drawLine(n.pos.x, n.pos.y, n.cp_out.x, n.cp_out.y, handle_line);
    }
  }

  for (size_t i = 0; i < editor.path.nodes.size(); ++i) {
    const Node& n = editor.path.nodes[i];
    bool is_selected = (editor.selected_node == static_cast<int>(i));

    if (editor.path.closed || i > 0) {
      bool sel = is_selected && editor.selected_type == NodeHitType::kCpIn;
      bool hov = editor.hovered_node == static_cast<int>(i) &&
                 editor.hovered_type == NodeHitType::kCpIn && !sel;
      Rect r = Rect::MakeXYWH(n.cp_in.x - cp_size, n.cp_in.y - cp_size,
                              cp_size * 2, cp_size * 2);
      if (sel) {
        canvas->drawOval(r, cp_sel);
      } else if (hov) {
        canvas->drawOval(r, cp_hov);
      } else {
        canvas->drawOval(r, cp);
      }
    }

    if (editor.path.closed || i < editor.path.nodes.size() - 1) {
      bool sel = is_selected && editor.selected_type == NodeHitType::kCpOut;
      bool hov = editor.hovered_node == static_cast<int>(i) &&
                 editor.hovered_type == NodeHitType::kCpOut && !sel;
      Rect r = Rect::MakeXYWH(n.cp_out.x - cp_size, n.cp_out.y - cp_size,
                              cp_size * 2, cp_size * 2);
      if (sel) {
        canvas->drawOval(r, cp_sel);
      } else if (hov) {
        canvas->drawOval(r, cp_hov);
      } else {
        canvas->drawOval(r, cp);
      }
    }
  }

  for (size_t i = 0; i < editor.path.nodes.size(); ++i) {
    const Node& n = editor.path.nodes[i];
    bool is_selected = (editor.selected_node == static_cast<int>(i) &&
                        editor.selected_type == NodeHitType::kAnchor);
    bool is_hovered =
        (editor.hovered_node == static_cast<int>(i) &&
         editor.hovered_type == NodeHitType::kAnchor && !is_selected);

    Rect r = Rect::MakeXYWH(n.pos.x - anchor_size, n.pos.y - anchor_size,
                            anchor_size * 2, anchor_size * 2);
    if (is_selected) {
      canvas->drawRect(r, anchor_sel);
    } else if (is_hovered) {
      canvas->drawRect(r, anchor_hov);
    } else {
      canvas->drawRect(r, anchor);
    }
  }
}

}  // namespace nanote
