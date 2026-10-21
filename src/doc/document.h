#pragma once

// Nanote native document format (.nano) plus the user documents folder.
//
// Format (little-endian, versioned binary):
//   magic "NANO", u32 version (=3)
//   u32 layer_count, i32 active_layer_index
//   f32 view_center_x, view_center_y, view_zoom
//   u8  show_grid, f32 grid_size
//   u8  doc_type (0=infinite canvas, 1=bounded canvas, 2=slideshow)
//   bounded, slideshow: f32 page_w, f32 page_h (page centered on origin)
//   slideshow: u32 slide_count, then slides, i32 current_slide
//              slide: u32 name_len + bytes, f32 view_cx, cy, zoom
//   per layer:
//     u8 type (0=raster, 1=vector), u32 name_len + bytes
//     u8 visible, u8 locked, f32 opacity
//     raster: i32 w,h,offset_x,offset_y; u8 encoding (0=raw RGBA,1=PNG);
//             u32 data_len + bytes
//     vector: u32 object_count, then objects (recursive for groups):
//       u8 kind (0=path,1=brush,2=text,3=group)
//       u32 stroke_color, u32 fill_color, f32 stroke_width
//       path:   u32 cmd_count; per cmd u8 verb + floats
//               (move/line:2, quad:4, cubic:6, close:0)
//       brush:  u32 color, f32 width, u32 point_count, f32 x/y pairs
//       text:   f32 x,y,font_size; u32 text_len + utf8 bytes
//               (typeface is resolved to the UI typeface on load)
//       group:  u32 child_count + children

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace nanote {

class LayerStack;
class Viewport;
class Typeface;

// Document kind: endless canvas, fixed-size page, or steppable slide deck.
enum class DocType : uint8_t { kInfinite = 0, kBounded = 1, kSlideshow = 2 };

[[nodiscard]] inline const char* DocTypeName(DocType t) {
  switch (t) {
    case DocType::kInfinite:
      return "Infinite";
    case DocType::kBounded:
      return "Bounded";
    case DocType::kSlideshow:
      return "Slideshow";
  }
  return "Infinite";
}

// A slideshow slide: a named viewport bookmark.
struct Slide {
  std::string name;
  float cx = 0.0f;
  float cy = 0.0f;
  float zoom = 1.0f;
};

// Bounded-canvas size presets offered by the new-document chooser.
struct PageSizePreset {
  const char* label;
  float w = 1920.0f;
  float h = 1080.0f;
};

[[nodiscard]] inline const PageSizePreset* PageSizePresets(int* count) {
  static const PageSizePreset kPresets[] = {
      {"1920 x 1080 Full HD", 1920.0f, 1080.0f},
      {"1280 x 720 HD", 1280.0f, 720.0f},
      {"1080 x 1080 Square", 1080.0f, 1080.0f},
      {"800 x 600 Small", 800.0f, 600.0f},
      {"3840 x 2160 4K", 3840.0f, 2160.0f},
  };
  if (count != nullptr) *count = 5;
  return kPresets;
}

// Per-document properties carried alongside the layer stack (persisted in
// the .nano file, version 2+).
struct DocProps {
  DocType type = DocType::kInfinite;
  float page_w = 1920.0f;  // bounded canvas / slideshow (centered on origin)
  float page_h = 1080.0f;
  std::vector<Slide> slides;  // slideshow only
  int current_slide = -1;
  [[nodiscard]] bool has_page() const {
    return type == DocType::kBounded || type == DocType::kSlideshow;
  }
};

// In-memory open document (name without extension + dirty flag).
struct OpenDocument {
  std::string name = "Untitled";
  bool dirty = false;
  DocProps props;
};

struct DocEntry {
  std::string name;    // stem without extension
  std::string path;    // full path
  uint64_t mtime = 0;  // modification time, unix seconds (0 = unknown)
  uint64_t size_bytes = 0;
  DocType type = DocType::kInfinite;
};

// User documents folder ("<Documents>/nanote"), created on demand.
// Empty when the platform reports no documents folder.
[[nodiscard]] std::string DocumentsDir();
bool EnsureDocumentsDir();
[[nodiscard]] std::vector<DocEntry> ListDocuments();

// Name helpers.
[[nodiscard]] std::string SanitizeDocName(const std::string& raw);
[[nodiscard]] std::string PathForDocName(const std::string& name);
[[nodiscard]] std::string DocNameFromPath(const std::string& path);
// First unused "Untitled", "Untitled 2", ... name.
[[nodiscard]] std::string UniqueUntitledName();
// Delete a document file. Returns false when there is nothing to delete
// or the removal fails.
[[nodiscard]] bool DeleteDocument(const std::string& path);

bool SaveDocument(const std::string& path, const LayerStack& stack,
                  const Viewport& view, const DocProps& props);
bool LoadDocument(const std::string& path, LayerStack& stack, Viewport& view,
                  const std::shared_ptr<Typeface>& typeface, DocProps& props);
// Peek only the file header: returns the stored doc type, defaulting to
// infinite for unreadable, unknown, or non-v2 files. Cheap enough to call
// per file when listing the documents folder.
[[nodiscard]] DocType PeekDocType(const std::string& path);

}  // namespace nanote
