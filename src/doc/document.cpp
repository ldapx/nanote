#include "doc/document.h"

#include <blend2d/blend2d.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include <SDL3/SDL.h>

#include "canvas/viewport.h"
#include "core/bitmap.h"
#include "core/path.h"
#include "core/string_utils.h"
#include "layer/layer.h"

namespace nanote {

namespace {

// ---------------------------------------------------------------------------
// Little-endian binary writer.
// ---------------------------------------------------------------------------

struct Writer {
  std::vector<uint8_t> b;

  void u8(uint8_t v) {
    b.push_back(v);
  }
  void u32(uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
  }
  void i32(int32_t v) {
    uint32_t u = 0;
    std::memcpy(&u, &v, 4);
    u32(u);
  }
  void f32(float v) {
    uint32_t u = 0;
    std::memcpy(&u, &v, 4);
    u32(u);
  }
  void str(const std::string& s) {
    u32(static_cast<uint32_t>(s.size()));
    b.insert(b.end(), s.begin(), s.end());
  }
};

// ---------------------------------------------------------------------------
// Bounds-checked little-endian reader with sanity caps.
// ---------------------------------------------------------------------------

class Reader {
 public:
  Reader(const uint8_t* data, size_t size) : p_(data), n_(size) {}

  [[nodiscard]] bool ok() const {
    return ok_;
  }
  [[nodiscard]] bool eof() const {
    return pos_ == n_;
  }

  uint8_t u8() {
    if (!require(1)) return 0;
    return p_[pos_++];
  }
  uint32_t u32() {
    if (!require(4)) return 0;
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i)
      v |= static_cast<uint32_t>(p_[pos_ + i]) << (8 * i);
    pos_ += 4;
    return v;
  }
  int32_t i32() {
    uint32_t u = u32();
    int32_t v = 0;
    std::memcpy(&v, &u, 4);
    return v;
  }
  float f32() {
    uint32_t u = u32();
    float v = 0.0f;
    std::memcpy(&v, &u, 4);
    return v;
  }
  std::string str() {
    const uint32_t len = u32();
    if (len > kMaxString) {
      ok_ = false;
      return "";
    }
    if (!require(len)) return "";
    std::string s(reinterpret_cast<const char*>(p_ + pos_), len);
    pos_ += len;
    return s;
  }
  const uint8_t* bytes(size_t count) {
    if (!require(count)) return nullptr;
    const uint8_t* out = p_ + pos_;
    pos_ += count;
    return out;
  }

 private:
  static constexpr size_t kMaxString = 1u << 20;
  bool require(size_t count) {
    if (!ok_ || count > n_ - pos_) {
      ok_ = false;
      return false;
    }
    return true;
  }
  const uint8_t* p_;
  size_t n_;
  size_t pos_ = 0;
  bool ok_ = true;
};

constexpr size_t kMaxPixelsDim = 16384;
constexpr size_t kMaxCount = 1u << 20;
constexpr size_t kMaxSlides = 4096;
constexpr int kMaxGroupDepth = 64;
constexpr uint32_t kVersion = 3;
// Page sizes are clamped to the raster dimension cap on load.
constexpr float kMaxPageDim = 16384.0f;

uint32_t UnpremultiplyPixel(uint32_t p) {
  uint8_t a = static_cast<uint8_t>((p >> 24) & 0xFF);
  uint8_t r = static_cast<uint8_t>((p >> 16) & 0xFF);
  uint8_t g = static_cast<uint8_t>((p >> 8) & 0xFF);
  uint8_t b = static_cast<uint8_t>(p & 0xFF);
  if (a == 0 || a == 255) return p;
  r = static_cast<uint8_t>(std::min(255, (r * 255 + a / 2) / a));
  g = static_cast<uint8_t>(std::min(255, (g * 255 + a / 2) / a));
  b = static_cast<uint8_t>(std::min(255, (b * 255 + a / 2) / a));
  return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(r) << 16) |
         (static_cast<uint32_t>(g) << 8) | b;
}

uint32_t PremultiplyRGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  if (a == 0) return 0;
  if (a == 255)
    return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(r) << 16) |
           (static_cast<uint32_t>(g) << 8) | b;
  r = static_cast<uint8_t>((r * a + 127) / 255);
  g = static_cast<uint8_t>((g * a + 127) / 255);
  b = static_cast<uint8_t>((b * a + 127) / 255);
  return (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(r) << 16) |
         (static_cast<uint32_t>(g) << 8) | b;
}

void WriteVectorObject(Writer& w, const VectorObject& obj);
void WritePath(Writer& w, const Path& path) {
  const auto& cmds = path.cmds();
  w.u32(static_cast<uint32_t>(cmds.size()));
  for (const auto& c : cmds) {
    w.u8(static_cast<uint8_t>(c.verb));
    const int floats = (c.verb == 1 || c.verb == 0)
                           ? 2
                           : (c.verb == 2 ? 4 : (c.verb == 3 ? 6 : 0));
    for (int i = 0; i < floats; ++i) w.f32(c.pts[i]);
  }
}

void WriteVectorObject(Writer& w, const VectorObject& obj) {
  int kind = 0;
  if (dynamic_cast<const PathObject*>(&obj) != nullptr) {
    kind = 0;
  } else if (dynamic_cast<const BrushObject*>(&obj) != nullptr) {
    kind = 1;
  } else if (dynamic_cast<const TextObject*>(&obj) != nullptr) {
    kind = 2;
  } else if (dynamic_cast<const GroupObject*>(&obj) != nullptr) {
    kind = 3;
  }
  w.u8(static_cast<uint8_t>(kind));
  w.u32(obj.stroke_color);
  w.u32(obj.fill_color);
  w.f32(obj.stroke_width);
  if (const auto* path = dynamic_cast<const PathObject*>(&obj)) {
    WritePath(w, path->path);
  } else if (const auto* brush = dynamic_cast<const BrushObject*>(&obj)) {
    w.u32(brush->color);
    w.f32(brush->width);
    w.u32(static_cast<uint32_t>(brush->points.size()));
    for (const auto& p : brush->points) {
      w.f32(p.x);
      w.f32(p.y);
    }
  } else if (const auto* text = dynamic_cast<const TextObject*>(&obj)) {
    w.f32(text->x);
    w.f32(text->y);
    w.f32(text->font_size);
    w.str(text->text);
  } else if (const auto* group = dynamic_cast<const GroupObject*>(&obj)) {
    w.u32(static_cast<uint32_t>(group->children.size()));
    for (const auto& child : group->children) WriteVectorObject(w, *child);
  } else {
    // Unknown future object: store an empty path so the stream stays
    // aligned and loadable.
    w.u32(0);
  }
}

std::unique_ptr<VectorObject> ReadVectorObject(
    Reader& r, const std::shared_ptr<Typeface>& typeface, int depth) {
  if (depth > kMaxGroupDepth) return nullptr;
  const uint8_t kind = r.u8();
  const uint32_t stroke_color = r.u32();
  const uint32_t fill_color = r.u32();
  const float stroke_width = r.f32();
  if (kind == 0) {
    auto obj = std::make_unique<PathObject>();
    const uint32_t n = r.u32();
    if (n > kMaxCount) return nullptr;
    Path path;
    for (uint32_t i = 0; i < n; ++i) {
      const int verb = r.u8();
      float pts[6] = {0, 0, 0, 0, 0, 0};
      const int floats =
          (verb == 1 || verb == 0) ? 2 : (verb == 2 ? 4 : (verb == 3 ? 6 : 0));
      if (verb < 0 || verb > 4) return nullptr;
      for (int j = 0; j < floats; ++j) pts[j] = r.f32();
      switch (verb) {
        case 0:
          path.moveTo(pts[0], pts[1]);
          break;
        case 1:
          path.lineTo(pts[0], pts[1]);
          break;
        case 2:
          path.quadTo(pts[0], pts[1], pts[2], pts[3]);
          break;
        case 3:
          path.cubicTo(pts[0], pts[1], pts[2], pts[3], pts[4], pts[5]);
          break;
        case 4:
          path.close();
          break;
      }
    }
    if (!r.ok()) return nullptr;
    obj->path = std::move(path);
    obj->stroke_color = stroke_color;
    obj->fill_color = fill_color;
    obj->stroke_width = stroke_width;
    return obj;
  }
  if (kind == 1) {
    auto obj = std::make_unique<BrushObject>();
    obj->color = r.u32();
    obj->width = r.f32();
    const uint32_t n = r.u32();
    if (n > kMaxCount) return nullptr;
    obj->points.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
      const float x = r.f32();
      const float y = r.f32();
      obj->points.push_back({x, y});
    }
    if (!r.ok()) return nullptr;
    obj->stroke_color = stroke_color;
    obj->fill_color = fill_color;
    obj->stroke_width = stroke_width;
    return obj;
  }
  if (kind == 2) {
    auto obj = std::make_unique<TextObject>();
    obj->x = r.f32();
    obj->y = r.f32();
    obj->font_size = r.f32();
    obj->text = r.str();
    if (!r.ok()) return nullptr;
    obj->stroke_color = stroke_color;
    obj->fill_color = fill_color;
    obj->stroke_width = stroke_width;
    obj->typeface = typeface;
    return obj;
  }
  if (kind == 3) {
    auto obj = std::make_unique<GroupObject>();
    const uint32_t n = r.u32();
    if (n > kMaxCount) return nullptr;
    for (uint32_t i = 0; i < n; ++i) {
      auto child = ReadVectorObject(r, typeface, depth + 1);
      if (!child) return nullptr;
      obj->children.push_back(std::move(child));
    }
    obj->stroke_color = stroke_color;
    obj->fill_color = fill_color;
    obj->stroke_width = stroke_width;
    return obj;
  }
  return nullptr;
}

bool WriteFile(const std::string& path, const std::vector<uint8_t>& data) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (f == nullptr) return false;
  const size_t written = std::fwrite(data.data(), 1, data.size(), f);
  std::fclose(f);
  return written == data.size();
}

bool ReadFile(const std::string& path, std::vector<uint8_t>& out) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) return false;
  std::fseek(f, 0, SEEK_END);
  const long size = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (size < 0 || static_cast<uint64_t>(size) > (1ull << 31)) {
    std::fclose(f);
    return false;
  }
  out.resize(static_cast<size_t>(size));
  const size_t got = std::fread(out.data(), 1, out.size(), f);
  std::fclose(f);
  return got == out.size();
}

}  // namespace

std::string DocumentsDir() {
  // Explicit override (portable installs, tests).
  if (const char* env = std::getenv("NANOTE_DOCS_DIR")) {
    if (env[0] != '\0') return env;
  }
  const char* base = SDL_GetUserFolder(SDL_FOLDER_DOCUMENTS);
  if (base == nullptr || base[0] == '\0') {
    char* pref = SDL_GetPrefPath("nanote", "nanote");
    if (pref == nullptr) return "";
    std::string dir = std::string(pref) + "documents";
    SDL_free(pref);
    return dir;
  }
  return std::string(base) + "nanote";
}

bool EnsureDocumentsDir() {
  const std::string dir = DocumentsDir();
  if (dir.empty()) return false;
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  return !ec;
}

std::vector<DocEntry> ListDocuments() {
  std::vector<DocEntry> out;
  const std::string dir = DocumentsDir();
  if (dir.empty()) return out;
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (ec) break;
    if (!entry.is_regular_file(ec)) continue;
    std::string path = entry.path().string();
    std::string ext = entry.path().extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
      return std::tolower(c);
    });
    if (ext != ".nano") continue;
    DocEntry e;
    e.path = path;
    e.name = DocNameFromPath(path);
    e.size_bytes = entry.file_size(ec);
    e.type = PeekDocType(path);
    auto mtime = entry.last_write_time(ec);
    if (!ec) {
      using namespace std::chrono;
      // file_time_type uses an implementation-defined epoch, so convert
      // to system-clock (unix) seconds for display and cache comparison.
      const auto sys_tp = time_point_cast<system_clock::duration>(
          mtime - decltype(mtime)::clock::now() + system_clock::now());
      const int64_t secs =
          duration_cast<seconds>(sys_tp.time_since_epoch()).count();
      e.mtime = secs >= 0 ? static_cast<uint64_t>(secs) : 0;
    }
    out.push_back(std::move(e));
  }
  std::sort(out.begin(), out.end(), [](const DocEntry& a, const DocEntry& b) {
    if (a.mtime != b.mtime) return a.mtime > b.mtime;
    return a.name < b.name;
  });
  return out;
}

bool DeleteDocument(const std::string& path) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec) || ec) return false;
  return std::filesystem::remove(path, ec) && !ec;
}

DocType PeekDocType(const std::string& path) {
  // Header prefix: magic(4) version(4) layers(4) active(4) cx(4) cy(4)
  // zoom(4) grid(1) gridsize(4) = 33 bytes, then the u8 doc type.
  FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) return DocType::kInfinite;
  uint8_t head[40] = {0};
  const size_t got = std::fread(head, 1, sizeof(head), f);
  std::fclose(f);
  if (got < 8) return DocType::kInfinite;
  const bool is_nano =
      head[0] == 'N' && head[1] == 'A' && head[2] == 'N' && head[3] == 'O';
  if (!is_nano) return DocType::kInfinite;
  const uint32_t ver = static_cast<uint32_t>(head[4]) |
                       (static_cast<uint32_t>(head[5]) << 8) |
                       (static_cast<uint32_t>(head[6]) << 16) |
                       (static_cast<uint32_t>(head[7]) << 24);
  if (ver != kVersion || got < 34) return DocType::kInfinite;
  return head[33] <= 2 ? static_cast<DocType>(head[33]) : DocType::kInfinite;
}

std::string SanitizeDocName(const std::string& raw) {
  std::string out;
  out.reserve(raw.size());
  for (char c : raw) {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == ' ' || c == '-' ||
                    c == '_' || c == '.' || c == '(' || c == ')';
    out.push_back(ok ? c : '_');
  }
  // Trim spaces/dots (Windows-hostile trailing characters).
  size_t start = out.find_first_not_of(' ');
  if (start == std::string::npos) return "";
  size_t end = out.find_last_not_of(" .");
  out = out.substr(start, end - start + 1);
  if (out.size() > 64) out.resize(64);
  if (out == "." || out == "..") return "";
  return out;
}

std::string PathForDocName(const std::string& name) {
  const std::string dir = DocumentsDir();
  if (dir.empty()) return "";
  std::string clean = SanitizeDocName(name);
  if (clean.empty()) clean = "Untitled";
  std::string filename = clean;
  if (filename.size() < 5 ||
      filename.compare(filename.size() - 5, 5, ".nano") != 0) {
    filename += ".nano";
  }
  return dir + "/" + filename;
}

std::string DocNameFromPath(const std::string& path) {
  std::string base = path;
  const size_t slash = base.find_last_of("/\\");
  if (slash != std::string::npos) base = base.substr(slash + 1);
  // Strip the ".nano" extension (any case) for a clean document name.
  if (base.size() > 5 &&
      Lower(std::string_view(base).substr(base.size() - 5)) == ".nano") {
    base.resize(base.size() - 5);
  }
  return base.empty() ? "Untitled" : base;
}
std::string UniqueUntitledName() {
  const std::string dir = DocumentsDir();
  auto exists = [&](const std::string& name) {
    if (dir.empty()) return false;
    std::error_code ec;
    return std::filesystem::exists(dir + "/" + name + ".nano", ec);
  };
  if (!exists("Untitled")) return "Untitled";
  for (int i = 2; i < 10000; ++i) {
    std::string name = "Untitled " + std::to_string(i);
    if (!exists(name)) return name;
  }
  return "Untitled";
}

bool SaveDocument(const std::string& path, const LayerStack& stack,
                  const Viewport& view, const DocProps& props) {
  {
    // Ensure the destination folder exists (no-op if it already does).
    std::error_code ec;
    const size_t slash = path.find_last_of("/\\");
    if (slash != std::string::npos) {
      std::filesystem::create_directories(path.substr(0, slash), ec);
      if (ec) return false;
    }
  }
  Writer w;
  w.u8('N');
  w.u8('A');
  w.u8('N');
  w.u8('O');
  w.u32(kVersion);
  const auto& layers = stack.GetLayers();
  w.u32(static_cast<uint32_t>(layers.size()));
  w.i32(stack.GetActiveLayerIndex());
  w.f32(view.center_.x);
  w.f32(view.center_.y);
  w.f32(view.zoom_);
  w.u8(view.IsGridVisible() ? 1 : 0);
  w.f32(view.GetGridSize());
  w.u8(static_cast<uint8_t>(props.type));
  if (props.has_page()) {
    w.f32(props.page_w);
    w.f32(props.page_h);
  }
  if (props.type == DocType::kSlideshow) {
    w.u32(static_cast<uint32_t>(props.slides.size()));
    for (const Slide& s : props.slides) {
      w.str(s.name);
      w.f32(s.cx);
      w.f32(s.cy);
      w.f32(s.zoom);
    }
    w.i32(props.current_slide);
  }

  BLImageCodec png;
  const bool have_png = png.find_by_name("PNG") == BL_SUCCESS;

  for (const auto& layer : layers) {
    w.u8(layer->type == LayerType::kVector ? 1 : 0);
    w.str(layer->name);
    w.u8(layer->visible ? 1 : 0);
    w.u8(layer->locked ? 1 : 0);
    w.f32(layer->opacity);
    if (layer->type == LayerType::kRaster) {
      const auto* raster = static_cast<const RasterLayer*>(layer.get());
      const int sw = raster->bitmap.width();
      const int sh = raster->bitmap.height();
      w.i32(sw);
      w.i32(sh);
      w.i32(raster->offset_x);
      w.i32(raster->offset_y);
      if (sw <= 0 || sh <= 0 || raster->bitmap.empty()) {
        w.u8(0);
        w.u32(0);
        continue;
      }
      bool wrote_png = false;
      if (have_png) {
        BLArray<uint8_t> encoded;
        if (raster->bitmap.image().write_to_data(encoded, png) == BL_SUCCESS) {
          w.u8(1);
          w.u32(static_cast<uint32_t>(encoded.size()));
          w.b.insert(w.b.end(), encoded.begin(), encoded.end());
          wrote_png = true;
        }
      }
      if (!wrote_png) {
        BLImageData data;
        if (raster->bitmap.image().get_data(&data) != BL_SUCCESS ||
            data.format != BL_FORMAT_PRGB32) {
          w.u8(0);
          w.u32(0);
          continue;
        }
        const uint32_t* src = static_cast<const uint32_t*>(data.pixel_data);
        const int stride = static_cast<int>(data.stride / 4);
        w.u8(0);
        w.u32(static_cast<uint32_t>(sw * sh * 4));
        const size_t base = w.b.size();
        w.b.resize(base + static_cast<size_t>(sw) * sh * 4);
        for (int y = 0; y < sh; ++y) {
          for (int x = 0; x < sw; ++x) {
            const uint32_t u = UnpremultiplyPixel(src[y * stride + x]);
            uint8_t* dst = &w.b[base + (static_cast<size_t>(y) * sw + x) * 4];
            dst[0] = static_cast<uint8_t>((u >> 16) & 0xFF);
            dst[1] = static_cast<uint8_t>((u >> 8) & 0xFF);
            dst[2] = static_cast<uint8_t>(u & 0xFF);
            dst[3] = static_cast<uint8_t>((u >> 24) & 0xFF);
          }
        }
      }
    } else {
      const auto* vector = static_cast<const VectorLayer*>(layer.get());
      w.u32(static_cast<uint32_t>(vector->objects.size()));
      for (const auto& obj : vector->objects) WriteVectorObject(w, *obj);
    }
  }
  return WriteFile(path, w.b);
}

bool LoadDocument(const std::string& path, LayerStack& stack, Viewport& view,
                  const std::shared_ptr<Typeface>& typeface, DocProps& props) {
  std::vector<uint8_t> data;
  if (!ReadFile(path, data) || data.size() < 8) return false;
  Reader r(data.data(), data.size());
  const uint8_t m0 = r.u8(), m1 = r.u8(), m2 = r.u8(), m3 = r.u8();
  const bool is_nano = m0 == 'N' && m1 == 'A' && m2 == 'N' && m3 == 'O';
  if (!is_nano) return false;
  if (r.u32() != kVersion) return false;
  props = DocProps();
  const uint32_t layer_count = r.u32();
  if (layer_count > kMaxCount) return false;
  const int32_t active_index = r.i32();
  const float cx = r.f32();
  const float cy = r.f32();
  const float zoom = r.f32();
  const bool show_grid = r.u8() != 0;
  const float grid_size = r.f32();
  {  // doc-type payload (v2 files always carry it)
    const uint8_t type_byte = r.u8();
    if (type_byte > 2) return false;
    props.type = static_cast<DocType>(type_byte);
    if (props.has_page()) {
      props.page_w = std::clamp(r.f32(), 1.0f, kMaxPageDim);
      props.page_h = std::clamp(r.f32(), 1.0f, kMaxPageDim);
    }
    if (props.type == DocType::kSlideshow) {
      const uint32_t n = r.u32();
      if (n > kMaxSlides) return false;
      props.slides.reserve(n);
      for (uint32_t i = 0; i < n; ++i) {
        Slide s;
        s.name = r.str();
        s.cx = r.f32();
        s.cy = r.f32();
        s.zoom = std::clamp(r.f32(), 0.05f, 32.0f);
        if (!r.ok() || !std::isfinite(s.cx) || !std::isfinite(s.cy)) {
          return false;
        }
        props.slides.push_back(std::move(s));
      }
      const int32_t cur = r.i32();
      props.current_slide =
          props.slides.empty()
              ? -1
              : std::clamp(cur, 0,
                           static_cast<int32_t>(props.slides.size()) - 1);
    }
    if (!r.ok()) return false;
  }

  LayerStack loaded;
  loaded.Clear();  // drop the default Background; file owns the stack
  for (uint32_t li = 0; li < layer_count; ++li) {
    const int type = r.u8();
    const std::string name = r.str();
    const bool visible = r.u8() != 0;
    const bool locked = r.u8() != 0;
    const float opacity = std::clamp(r.f32(), 0.0f, 1.0f);
    if (!r.ok()) return false;
    if (type == 0) {
      const int32_t w = r.i32();
      const int32_t h = r.i32();
      const int32_t ox = r.i32();
      const int32_t oy = r.i32();
      const int encoding = r.u8();
      const uint32_t len = r.u32();
      const uint8_t* bytes = r.bytes(len);
      if (!r.ok() || bytes == nullptr) return false;
      if (w < 0 || h < 0 || w > 16384 || h > 16384) return false;
      auto layer = std::make_unique<RasterLayer>();
      layer->name = name;
      layer->visible = visible;
      layer->locked = locked;
      layer->opacity = opacity;
      layer->offset_x = ox;
      layer->offset_y = oy;
      if (w > 0 && h > 0) {
        if (encoding == 1) {
          BLImage img;
          if (img.read_from_data(bytes, len, BLImageCodec::built_in_codecs()) !=
                  BL_SUCCESS ||
              img.width() != w || img.height() != h ||
              img.convert(BL_FORMAT_PRGB32) != BL_SUCCESS) {
            return false;
          }
          layer->bitmap.allocN32Pixels(w, h);
          BLImageData src, dst;
          if (img.get_data(&src) != BL_SUCCESS ||
              layer->bitmap.image().make_mutable(&dst) != BL_SUCCESS) {
            return false;
          }
          const uint32_t* srow = static_cast<const uint32_t*>(src.pixel_data);
          uint32_t* drow = static_cast<uint32_t*>(dst.pixel_data);
          const int ss = static_cast<int>(src.stride / 4);
          const int ds = static_cast<int>(dst.stride / 4);
          for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) drow[y * ds + x] = srow[y * ss + x];
        } else if (encoding == 0) {
          if (len != static_cast<uint32_t>(w) * h * 4) return false;
          layer->bitmap.allocN32Pixels(w, h);
          BLImageData dst;
          if (layer->bitmap.image().make_mutable(&dst) != BL_SUCCESS) {
            return false;
          }
          uint32_t* drow = static_cast<uint32_t*>(dst.pixel_data);
          const int ds = static_cast<int>(dst.stride / 4);
          for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
              const uint8_t* s = bytes + (static_cast<size_t>(y) * w + x) * 4;
              drow[y * ds + x] = PremultiplyRGBA(s[0], s[1], s[2], s[3]);
            }
          }
        } else {
          return false;
        }
      }
      loaded.GetLayers().push_back(std::move(layer));
    } else if (type == 1) {
      auto layer = std::make_unique<VectorLayer>();
      layer->name = name;
      layer->visible = visible;
      layer->locked = locked;
      layer->opacity = opacity;
      const uint32_t n = r.u32();
      if (n > kMaxCount) return false;
      for (uint32_t i = 0; i < n; ++i) {
        auto obj = ReadVectorObject(r, typeface, 0);
        if (!obj) return false;
        layer->objects.push_back(std::move(obj));
      }
      loaded.GetLayers().push_back(std::move(layer));
    } else {
      return false;
    }
  }
  if (!r.ok()) return false;

  if (loaded.GetLayers().empty()) {
    loaded.AddRasterLayer("Background", 1000, 1000);
  }
  int active = active_index;
  if (active < 0 || active >= static_cast<int>(loaded.GetLayers().size())) {
    active = static_cast<int>(loaded.GetLayers().size()) - 1;
  }
  loaded.SetActiveLayerIndex(active);
  loaded.GetSelection().Clear();

  view.center_.x = cx;
  view.center_.y = cy;
  view.SetZoom(std::clamp(zoom, 0.05f, 32.0f));
  view.SetGridVisible(show_grid);
  view.SetGridSize(grid_size);

  stack = std::move(loaded);
  return true;
}

}  // namespace nanote
