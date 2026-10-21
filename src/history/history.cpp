#include "history/history.h"

#include <algorithm>
#include <cstring>

namespace nanote {
namespace {

// FNV-1a 64-bit.
struct Fnv {
  uint64_t h = 14695981039346656037ull;
  void bytes(const void* p, size_t n) {
    const auto* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; ++i) {
      h ^= b[i];
      h *= 1099511628211ull;
    }
  }
  void u64(uint64_t v) {
    for (int i = 0; i < 8; ++i)
      bytes_single(static_cast<uint8_t>((v >> (8 * i)) & 0xff));
  }
  void u32(uint32_t v) {
    for (int i = 0; i < 4; ++i)
      bytes_single(static_cast<uint8_t>((v >> (8 * i)) & 0xff));
  }
  void u8(uint8_t v) {
    bytes_single(v);
  }
  void boolean(bool v) {
    bytes_single(v ? 1 : 0);
  }
  void number(int v) {
    u32(static_cast<uint32_t>(v));
  }
  void number(size_t v) {
    u64(static_cast<uint64_t>(v));
  }
  void real(float v) {
    uint32_t u = 0;
    static_assert(sizeof(u) == sizeof(v));
    std::memcpy(&u, &v, sizeof(u));
    u32(u);
  }
  void str(const std::string& s) {
    number(s.size());
    bytes(s.data(), s.size());
  }

 private:
  void bytes_single(uint8_t b) {
    h ^= b;
    h *= 1099511628211ull;
  }
};

void HashColor(Fnv& f, Color c) {
  f.u32(c);
}

void HashRect(Fnv& f, const Rect& r) {
  f.real(r.left);
  f.real(r.top);
  f.real(r.right);
  f.real(r.bottom);
}

void HashPath(Fnv& f, const Path& path) {
  for (const auto& cmd : path.cmds()) {
    f.number(cmd.verb);
    for (float v : cmd.pts) f.real(v);
  }
}

void HashObject(Fnv& f, const VectorObject& obj);

void HashVectorLayer(Fnv& f, const VectorLayer& layer) {
  f.number(layer.objects.size());
  for (const auto& obj : layer.objects) HashObject(f, *obj);
}

void HashObject(Fnv& f, const VectorObject& obj) {
  if (const auto* o = dynamic_cast<const PathObject*>(&obj)) {
    f.u8(1);
    HashPath(f, o->path);
  } else if (const auto* o = dynamic_cast<const BrushObject*>(&obj)) {
    f.u8(2);
    HashColor(f, o->color);
    f.real(o->width);
    f.number(o->points.size());
    for (const auto& p : o->points) {
      f.real(p.x);
      f.real(p.y);
    }
  } else if (const auto* o = dynamic_cast<const TextObject*>(&obj)) {
    f.u8(3);
    f.str(o->text);
    f.real(o->x);
    f.real(o->y);
    f.real(o->font_size);
    // typeface is session-global and never changes per object; skipped.
  } else if (const auto* o = dynamic_cast<const GroupObject*>(&obj)) {
    f.u8(4);
    f.number(o->children.size());
    for (const auto& child : o->children) HashObject(f, *child);
  } else {
    f.u8(0);  // unknown object kind: hash common paint only
  }
  HashColor(f, obj.stroke_color);
  HashColor(f, obj.fill_color);
  f.real(obj.stroke_width);
}

void HashBitmap(Fnv& f, const Bitmap& bmp) {
  f.number(bmp.width());
  f.number(bmp.height());
  if (bmp.width() <= 0 || bmp.height() <= 0 || bmp.empty()) return;
  BLImageData data;
  if (bmp.image().get_data(&data) != BL_SUCCESS || data.pixel_data == nullptr)
    return;
  const int stride = static_cast<int>(data.stride);
  const int row_bytes = bmp.width() * 4;
  const auto* base = static_cast<const uint8_t*>(data.pixel_data);
  for (int y = 0; y < bmp.height(); ++y) f.bytes(base + y * stride, row_bytes);
}

void HashLayer(Fnv& f, const Layer& layer) {
  f.number(static_cast<int>(layer.type));
  f.str(layer.name);
  f.boolean(layer.visible);
  f.boolean(layer.locked);
  f.real(layer.opacity);
  if (layer.type == LayerType::kRaster) {
    const auto& raster = static_cast<const RasterLayer&>(layer);
    f.number(raster.offset_x);
    f.number(raster.offset_y);
    HashBitmap(f, raster.bitmap);
  } else if (layer.type == LayerType::kVector) {
    const auto& vec = static_cast<const VectorLayer&>(layer);
    HashVectorLayer(f, vec);
  }
}

uint64_t FingerprintLayers(const std::vector<std::unique_ptr<Layer>>& layers,
                           const Selection& sel, int active,
                           const DocProps& props) {
  Fnv f;
  f.number(layers.size());
  for (const auto& layer : layers) HashLayer(f, *layer);
  f.boolean(sel.active);
  HashRect(f, sel.canvas_rect);
  f.number(sel.object_indices.size());
  for (int i : sel.object_indices) f.number(i);
  f.number(active);
  f.number(static_cast<int>(props.type));
  f.real(props.page_w);
  f.real(props.page_h);
  // Note: current slide index is navigation state, deliberately excluded.
  f.number(props.slides.size());
  for (const auto& s : props.slides) {
    f.str(s.name);
    f.real(s.cx);
    f.real(s.cy);
    f.real(s.zoom);
  }
  return f.h;
}

}  // namespace

void HistoryManager::Begin(const LayerStack& layers, const DocProps& props) {
  if (depth_++ > 0) return;  // nested: outer transaction owns the snapshot
  pending_ = TakeSnapshot(layers, props);
  pending_fp_ = Fingerprint(pending_);
  pending_open_ = true;
}

void HistoryManager::Commit(const LayerStack& layers, const DocProps& props) {
  if (depth_ == 0) return;
  if (--depth_ > 0) return;  // nested: outer commit finalizes
  if (pending_open_ && FingerprintCurrent(layers, props) != pending_fp_) {
    undo_.push_back(std::move(pending_));
    redo_.clear();
  }
  pending_ = DocSnapshot();
  pending_fp_ = 0;
  pending_open_ = false;
}

void HistoryManager::Clear() {
  undo_.clear();
  redo_.clear();
  pending_ = DocSnapshot();
  pending_fp_ = 0;
  pending_open_ = false;
  depth_ = 0;
}

bool HistoryManager::Undo(LayerStack& layers, DocProps& props) {
  if (undo_.empty()) return false;
  DocSnapshot cur = TakeSnapshot(layers, props);
  redo_.push_back(std::move(cur));
  DocSnapshot top = std::move(undo_.back());
  undo_.pop_back();
  Restore(layers, props, top);
  return true;
}

bool HistoryManager::Redo(LayerStack& layers, DocProps& props) {
  if (redo_.empty()) return false;
  DocSnapshot cur = TakeSnapshot(layers, props);
  undo_.push_back(std::move(cur));
  DocSnapshot top = std::move(redo_.back());
  redo_.pop_back();
  Restore(layers, props, top);
  return true;
}

DocSnapshot HistoryManager::TakeSnapshot(const LayerStack& layers,
                                         const DocProps& props) {
  DocSnapshot snap;
  snap.layers.reserve(layers.GetLayers().size());
  for (const auto& layer : layers.GetLayers())
    snap.layers.push_back(layer->Clone());
  snap.selection = layers.GetSelection();
  snap.active_layer = layers.GetActiveLayerIndex();
  snap.props = props;
  return snap;
}

uint64_t HistoryManager::Fingerprint(const DocSnapshot& snap) {
  return FingerprintLayers(snap.layers, snap.selection, snap.active_layer,
                           snap.props);
}

uint64_t HistoryManager::FingerprintCurrent(const LayerStack& layers,
                                            const DocProps& props) {
  return FingerprintLayers(layers.GetLayers(), layers.GetSelection(),
                           layers.GetActiveLayerIndex(), props);
}

void HistoryManager::Restore(LayerStack& layers, DocProps& props,
                             const DocSnapshot& snap) {
  const int keep_slide = props.current_slide;
  std::vector<std::unique_ptr<Layer>> cloned;
  cloned.reserve(snap.layers.size());
  for (const auto& layer : snap.layers) cloned.push_back(layer->Clone());
  layers.ReplaceAll(std::move(cloned), snap.active_layer);
  layers.GetSelection() = snap.selection;
  props = snap.props;
  if (props.slides.empty()) {
    props.current_slide = -1;
  } else {
    props.current_slide =
        std::clamp(keep_slide, 0, static_cast<int>(props.slides.size()) - 1);
  }
}

}  // namespace nanote
