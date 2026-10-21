#include "ui/main_menu.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "canvas/viewport.h"
#include "core/color.h"
#include "core/font.h"
#include "core/string_utils.h"
#include "drawing/drawing.h"
#include "layer/layer.h"
#include "render/canvas.h"
#include "ui/ui_renderer.h"

namespace nanote {

namespace {

// Portable localtime: localtime_r (POSIX) vs localtime_s (Windows, with
// swapped arguments).
void ToLocalTime(std::time_t t, std::tm* out) {
#if defined(_WIN32)
  localtime_s(out, &t);
#else
  localtime_r(&t, out);
#endif
}

constexpr float kPadding = 16.0f;
constexpr float kTitleHeight = 36.0f;
constexpr float kSearchHeight = 34.0f;
constexpr float kBrowseWidth = 44.0f;
constexpr float kBrowseGap = 8.0f;
constexpr float kSearchGap = 10.0f;
constexpr float kFilterHeight = 26.0f;
constexpr float kFilterGap = 8.0f;
constexpr float kFooterHeight = 26.0f;
constexpr float kCellW = 176.0f;
constexpr float kCellH = 150.0f;
constexpr float kGap = 12.0f;
constexpr float kThumbH = 96.0f;

constexpr int kThumbPxW = 320;
constexpr int kThumbPxH = 192;

// ---------------------------------------------------------------------------
// Document thumbnails (rendered on demand, cached by path+mtime).
// ---------------------------------------------------------------------------

struct CachedThumb {
  uint64_t mtime = 0;
  Bitmap bmp;
};

std::unordered_map<std::string, CachedThumb>& ThumbCache() {
  static std::unordered_map<std::string, CachedThumb> cache;
  return cache;
}

const Bitmap* ThumbnailFor(const DocEntry& doc,
                           const std::shared_ptr<Typeface>& typeface) {
  auto& cache = ThumbCache();
  auto it = cache.find(doc.path);
  if (it != cache.end() && it->second.mtime == doc.mtime &&
      !it->second.bmp.empty()) {
    return &it->second.bmp;
  }
  // Cap the cache so a huge documents folder can't grow it without bound.
  if (cache.size() >= 128) {
    // Drop entries that no longer match; if still huge, clear all.
    // The caller prunes on each draw via PruneThumbCache, so reaching
    // the cap here means many live docs: just clear and rebuild lazily.
    cache.clear();
  }
  LayerStack stack;
  Viewport view;
  DocProps props;
  if (!LoadDocument(doc.path, stack, view, typeface, props)) {
    return nullptr;
  }
  Rect bounds = DocContentBounds(stack);
  // Paged documents (bounded/slideshow) preview the whole page, mirroring
  // the canvas: checkerboard inside, gray outside.
  Rect page = Rect::MakeEmpty();
  if (props.has_page() && props.page_w > 0.0f && props.page_h > 0.0f) {
    page = BoundedPageRect(props);
    bounds.join(page);
  }
  if (bounds.isEmpty()) {
    bounds = Rect::MakeXYWH(-128.0f, -128.0f, 256.0f, 256.0f);
  }
  bounds.outset(std::max(bounds.width(), bounds.height()) * 0.05f + 8.0f,
                std::max(bounds.width(), bounds.height()) * 0.05f + 8.0f);

  Bitmap bmp;
  bmp.allocN32Pixels(kThumbPxW, kThumbPxH);
  Canvas c(bmp);
  const float grid = view.GetGridSize() > 0.0f ? view.GetGridSize() : 50.0f;
  const float s =
      std::min(kThumbPxW / bounds.width(), kThumbPxH / bounds.height());
  if (s > 0.0f && std::isfinite(s)) {
    c.translate(kThumbPxW * 0.5f, kThumbPxH * 0.5f);
    c.scale(s, s);
    c.translate(-bounds.centerX(), -bounds.centerY());
    if (page.isEmpty()) {
      // Infinite canvas: checkerboard across the whole preview.
      c.clear(MakeARGB(255, 34, 34, 38));
      DrawCheckerboardRect(&c, s, grid, bounds);
    } else {
      // Paged: solid gray base, checkerboard inside the page.
      c.clear(MakeARGB(255, 128, 128, 128));
      DrawCheckerboardRect(&c, s, grid, page);
    }
    for (const auto& layer : stack.GetLayers()) {
      if (layer != nullptr && layer->visible) layer->Render(&c);
    }
    if (!page.isEmpty()) {
      // Gray over anything outside the page, then the page outline —
      // same order as the canvas so stray vector marks stay hidden.
      const Paint cover = Paint::Fill(MakeARGB(255, 128, 128, 128));
      auto cover_rect = [&](float l, float t, float r, float b) {
        const Rect rr = Rect::MakeLTRB(
            std::max(l, bounds.left), std::max(t, bounds.top),
            std::min(r, bounds.right), std::min(b, bounds.bottom));
        if (!rr.isEmpty()) c.drawRect(rr, cover);
      };
      cover_rect(bounds.left, bounds.top, page.left, bounds.bottom);
      cover_rect(page.right, bounds.top, bounds.right, bounds.bottom);
      cover_rect(page.left, bounds.top, page.right, page.top);
      cover_rect(page.left, page.bottom, page.right, bounds.bottom);
      const Paint edge = Paint::Stroke(MakeARGB(255, 110, 110, 115), 1.5f / s);
      c.drawRect(page, edge);
    }
  } else {
    c.clear(MakeARGB(255, 34, 34, 38));
  }
  CachedThumb entry;
  entry.mtime = doc.mtime;
  entry.bmp = bmp;
  auto inserted = cache.emplace(doc.path, std::move(entry));
  // emplace may not insert if another thread... single-threaded: assign.
  if (!inserted.second) {
    cache[doc.path].mtime = doc.mtime;
    cache[doc.path].bmp = bmp;
    return &cache[doc.path].bmp;
  }
  return &inserted.first->second.bmp;
}

void PruneThumbCache(const std::vector<DocEntry>& docs) {
  auto& cache = ThumbCache();
  if (cache.empty()) return;
  std::unordered_set<std::string> live;
  live.reserve(docs.size() * 2 + 1);
  for (const auto& d : docs) live.insert(d.path);
  for (auto it = cache.begin(); it != cache.end();) {
    if (live.find(it->first) == live.end()) {
      it = cache.erase(it);
    } else {
      ++it;
    }
  }
}

std::string Ellipsize(const std::string& s, const Font& font, float max_w) {
  if (font.measureText(s.c_str(), s.size()) <= max_w) return s;
  std::string out = s;
  while (out.size() > 4) {
    out.resize(out.size() - 4);
    std::string cand = out + "\xe2\x80\xa6";  // ellipsis
    if (font.measureText(cand.c_str(), cand.size()) <= max_w) return cand;
  }
  return "\xe2\x80\xa6";
}

}  // namespace

MainMenuLayout ComputeMainMenuLayout(int window_w, int window_h, int total_rows,
                                     int selected) {
  MainMenuLayout layout;
  layout.total_rows = total_rows;
  const float panel_w = std::min(980.0f, std::max(320.0f, window_w - 48.0f));
  const float panel_h = std::min(760.0f, std::max(320.0f, window_h - 48.0f));
  layout.panel = Rect::MakeXYWH((window_w - panel_w) * 0.5f,
                                (window_h - panel_h) * 0.5f, panel_w, panel_h);
  layout.search_bar = Rect::MakeXYWH(
      layout.panel.left + kPadding, layout.panel.top + kPadding + kTitleHeight,
      panel_w - kPadding * 2.0f - kBrowseWidth - kBrowseGap, kSearchHeight);
  layout.browse_button =
      Rect::MakeXYWH(layout.search_bar.right + kBrowseGap,
                     layout.search_bar.top, kBrowseWidth, kSearchHeight);
  layout.filter_bar = Rect::MakeXYWH(layout.panel.left + kPadding,
                                     layout.search_bar.bottom + kFilterGap,
                                     panel_w - kPadding * 2.0f, kFilterHeight);
  {
    const float chip_gap = 6.0f;
    const float chip_w = (layout.filter_bar.width() - chip_gap * 3) / 4.0f;
    for (int i = 0; i < 4; ++i) {
      layout.chips[i] =
          Rect::MakeXYWH(layout.filter_bar.left + i * (chip_w + chip_gap),
                         layout.filter_bar.top, chip_w, kFilterHeight);
    }
  }

  const float grid_top = layout.filter_bar.bottom + kSearchGap;
  const float grid_bottom = layout.panel.bottom - kPadding - kFooterHeight;
  const float avail_w = panel_w - kPadding * 2.0f;
  int cols = static_cast<int>((avail_w + kGap) / (kCellW + kGap));
  cols = std::max(1, cols);
  cols = std::min(cols, std::max(1, total_rows));
  layout.cols = cols;

  const float grid_w = cols * kCellW + (cols - 1) * kGap;
  const float grid_left =
      layout.panel.left + kPadding + (avail_w - grid_w) * 0.5f;
  const float grid_h = std::max(0.0f, grid_bottom - grid_top);
  int visible_grid_rows = static_cast<int>((grid_h + kGap) / (kCellH + kGap));
  visible_grid_rows = std::max(1, visible_grid_rows);

  const int total_grid_rows = (total_rows + cols - 1) / cols;
  const int sel_clamped = std::clamp(selected, 0, std::max(0, total_rows - 1));
  const int sel_grid_row = (cols > 0) ? sel_clamped / cols : 0;
  int first_grid_row = 0;
  if (total_grid_rows > visible_grid_rows) {
    first_grid_row = std::clamp(sel_grid_row - visible_grid_rows / 2, 0,
                                total_grid_rows - visible_grid_rows);
  }
  const int last_grid_row =
      std::min(total_grid_rows, first_grid_row + visible_grid_rows);
  layout.first_visible = first_grid_row * cols;
  for (int gr = first_grid_row; gr < last_grid_row; ++gr) {
    for (int gc = 0; gc < cols; ++gc) {
      const int pos = gr * cols + gc;
      if (pos >= total_rows) break;
      const float x = grid_left + gc * (kCellW + kGap);
      const float y = grid_top + (gr - first_grid_row) * (kCellH + kGap);
      MainMenuCell cell;
      cell.rect = Rect::MakeXYWH(x, y, kCellW, kCellH);
      cell.thumb = Rect::MakeXYWH(x + 8.0f, y + 8.0f, kCellW - 16.0f, kThumbH);
      if (pos > 0) {
        // × delete button overlapping the thumb's top-right corner.
        constexpr float kDelSize = 20.0f;
        cell.del = Rect::MakeXYWH(cell.rect.right - 8.0f - kDelSize,
                                  cell.rect.top + 8.0f, kDelSize, kDelSize);
      }
      cell.row = pos;
      layout.cells.push_back(cell);
    }
  }
  return layout;
}

int MainMenuHitRow(const MainMenuLayout& layout, float mx, float my) {
  for (const auto& cell : layout.cells) {
    if (cell.rect.contains(mx, my)) return cell.row;
  }
  return -1;
}

int MainMenuHitDeleteRow(const MainMenuLayout& layout, float mx, float my) {
  for (const auto& cell : layout.cells) {
    if (!cell.del.isEmpty() && cell.del.contains(mx, my)) return cell.row;
  }
  return -1;
}

bool MainMenuHitSearch(const MainMenuLayout& layout, float mx, float my) {
  return layout.search_bar.contains(mx, my);
}

bool MainMenuHitBrowse(const MainMenuLayout& layout, float mx, float my) {
  return !layout.browse_button.isEmpty() &&
         layout.browse_button.contains(mx, my);
}

int MainMenuHitFilter(const MainMenuLayout& layout, float mx, float my) {
  for (int i = 0; i < 4; ++i) {
    if (layout.chips[i].contains(mx, my)) return i;
  }
  return -1;
}

namespace {

constexpr float kChoicePanelW = 400.0f;
constexpr float kChoiceRowH = 52.0f;
constexpr float kChoiceRowGap = 8.0f;

}  // namespace

DocTypeChoiceLayout ComputeDocTypeChoiceLayout(int window_w, int window_h) {
  DocTypeChoiceLayout layout;
  const float panel_h =
      kPadding * 2 + 52.0f + 3 * kChoiceRowH + 2 * kChoiceRowGap + 26.0f;
  const float panel_w = std::min(kChoicePanelW, window_w - 48.0f);
  layout.panel = Rect::MakeXYWH((window_w - panel_w) * 0.5f,
                                (window_h - panel_h) * 0.5f, panel_w, panel_h);
  float y = layout.panel.top + kPadding + 52.0f;
  for (int i = 0; i < 3; ++i) {
    layout.rows[i] = Rect::MakeXYWH(layout.panel.left + kPadding, y,
                                    panel_w - kPadding * 2.0f, kChoiceRowH);
    y += kChoiceRowH + kChoiceRowGap;
  }
  return layout;
}

int DocTypeChoiceHitRow(const DocTypeChoiceLayout& layout, float mx, float my) {
  for (int i = 0; i < 3; ++i) {
    if (layout.rows[i].contains(mx, my)) return i;
  }
  return -1;
}

namespace {
constexpr float kChoiceSizeRowH = 44.0f;
}  // namespace

DocSizeChoiceLayout ComputeDocSizeChoiceLayout(int window_w, int window_h) {
  DocSizeChoiceLayout layout;
  int preset_count = 0;
  (void)PageSizePresets(&preset_count);  // layout needs the count only
  constexpr int kChoiceRowCount = 6;     // 5 presets + custom size
  const float panel_h = kPadding * 2 + 52.0f +
                        kChoiceRowCount * kChoiceSizeRowH +
                        (kChoiceRowCount - 1) * kChoiceRowGap + 26.0f;
  const float panel_w = std::min(kChoicePanelW, window_w - 48.0f);
  layout.panel = Rect::MakeXYWH((window_w - panel_w) * 0.5f,
                                (window_h - panel_h) * 0.5f, panel_w, panel_h);
  float y = layout.panel.top + kPadding + 52.0f;
  for (int i = 0; i < 6; ++i) {
    if (i < preset_count || i == 5) {
      layout.rows[i] =
          Rect::MakeXYWH(layout.panel.left + kPadding, y,
                         panel_w - kPadding * 2.0f, kChoiceSizeRowH);
    } else {
      layout.rows[i] = Rect::MakeEmpty();
    }
    y += kChoiceSizeRowH + kChoiceRowGap;
  }
  return layout;
}

int DocSizeChoiceHitRow(const DocSizeChoiceLayout& layout, float mx, float my) {
  for (int i = 0; i < 6; ++i) {
    if (!layout.rows[i].isEmpty() && layout.rows[i].contains(mx, my)) return i;
  }
  return -1;
}

std::string FormatDocSize(uint64_t bytes) {
  char buf[32];
  if (bytes < 1024) {
    std::snprintf(buf, sizeof(buf), "%llu B",
                  static_cast<unsigned long long>(bytes));
  } else if (bytes < 1024ull * 1024ull) {
    std::snprintf(buf, sizeof(buf), "%llu KB",
                  static_cast<unsigned long long>((bytes + 512) / 1024));
  } else {
    const double mb = bytes / (1024.0 * 1024.0);
    std::snprintf(buf, sizeof(buf), "%.1f MB", mb);
  }
  return buf;
}

// Short modification date for a tile: time today, day+month this year,
// full date otherwise. Takes unix seconds (see ListDocuments).
std::string FormatDocDate(uint64_t unix_mtime) {
  if (unix_mtime == 0) return "--";
  std::time_t t = static_cast<std::time_t>(unix_mtime);
  std::time_t now_t = std::time(nullptr);
  std::tm tm_now{};
  std::tm tm{};
  ToLocalTime(now_t, &tm_now);
  ToLocalTime(t, &tm);
  char buf[32];
  if (tm.tm_year == tm_now.tm_year && tm.tm_yday == tm_now.tm_yday) {
    std::strftime(buf, sizeof(buf), "%H:%M", &tm);
  } else if (tm.tm_year == tm_now.tm_year) {
    std::strftime(buf, sizeof(buf), "%d %b", &tm);
  } else {
    std::strftime(buf, sizeof(buf), "%d %b %Y", &tm);
  }
  return buf;
}

void DrawMainMenu(Canvas* canvas, const InputManager& input,
                  const std::shared_ptr<Typeface>& typeface, int window_width,
                  int window_height, const std::string& open_name,
                  bool open_dirty, double time_seconds) {
  const auto& docs = input.GetMainMenuDocs();
  const std::vector<size_t> filt = input.GetMainMenuFilteredIndices();
  const int selected = input.GetMainMenuSelected();
  const int total_rows = static_cast<int>(filt.size()) + 1;
  const MainMenuLayout layout =
      ComputeMainMenuLayout(window_width, window_height, total_rows, selected);

  PruneThumbCache(docs);

  Paint bg = Paint::Fill(MakeARGB(255, 18, 18, 20));
  canvas->drawRect(Rect::MakeXYWH(0, 0, window_width, window_height), bg);

  Paint panel_paint = Paint::Fill(MakeARGB(255, 28, 28, 30));
  canvas->drawRect(layout.panel, panel_paint);
  Paint border = Paint::Stroke(MakeARGB(255, 70, 70, 74), 1.0f);
  canvas->drawRect(layout.panel, border);

  Font title_font(typeface, 28);
  Font sub_font(typeface, 12);
  Font body_font(typeface, 14);
  Font small_font(typeface, 12);
  Font big_font(typeface, 40);
  Paint title_paint = Paint::Fill(MakeARGB(255, 235, 235, 235));
  Paint text_paint = Paint::Fill(MakeARGB(255, 220, 220, 220));
  Paint dim_paint = Paint::Fill(MakeARGB(255, 140, 140, 140));
  Paint tile_paint = Paint::Fill(MakeARGB(255, 40, 40, 44));
  Paint active_paint = Paint::Fill(MakeARGB(255, 58, 58, 66));
  Paint active_border = Paint::Stroke(MakeARGB(255, 110, 150, 220), 1.5f);
  Paint thumb_bg = Paint::Fill(MakeARGB(255, 22, 22, 24));

  const float title_x = layout.panel.left + kPadding;
  const float title_y = layout.panel.top + kPadding + 26.0f;
  canvas->drawString("Documents", title_x, title_y, title_font, title_paint);

  // Search bar (always focused: typing filters from anywhere in the menu).
  Paint search_bg = Paint::Fill(MakeARGB(255, 36, 36, 40));
  canvas->drawRect(layout.search_bar, search_bg);
  Paint search_border = Paint::Stroke(MakeARGB(255, 70, 70, 76), 1.0f);
  canvas->drawRect(layout.search_bar, search_border);
  const std::string& query = input.GetMainMenuSearch();
  const float search_text_y = layout.search_bar.top + 22.0f;
  const float search_text_x = layout.search_bar.left + 10.0f;
  const float search_max_w = layout.search_bar.width() - 20.0f;
  const bool caret_on = std::fmod(time_seconds, 1.0) < 0.5;  // 0.5s blink
  Paint caret_paint = Paint::Fill(MakeARGB(255, 220, 220, 220));
  const float caret_top = layout.search_bar.top + 8.0f;
  constexpr float kCaretH = 18.0f;
  if (query.empty()) {
    canvas->drawString("Search documents...", search_text_x, search_text_y,
                       body_font, dim_paint);
    if (caret_on) {
      canvas->drawRect(Rect::MakeXYWH(search_text_x, caret_top, 2.0f, kCaretH),
                       caret_paint);
    }
  } else {
    const std::string shown = Ellipsize(query, body_font, search_max_w - 12.0f);
    canvas->drawString(shown.c_str(), search_text_x, search_text_y, body_font,
                       text_paint);
    if (caret_on) {
      const float qw = body_font.measureText(shown.c_str(), shown.size());
      canvas->drawRect(
          Rect::MakeXYWH(search_text_x + qw + 3.0f, caret_top, 2.0f, kCaretH),
          caret_paint);
    }
  }

  // "..." button: opens a native file picker for .nano documents.
  canvas->drawRect(layout.browse_button, tile_paint);
  canvas->drawRect(layout.browse_button, search_border);
  {
    const char* dots = "...";
    const float dw = body_font.measureText(dots, 3);
    canvas->drawString(dots, layout.browse_button.centerX() - dw * 0.5f,
                       layout.browse_button.top + 22.0f, body_font, text_paint);
  }

  // Type filter chips (0 = All).
  {
    uint64_t counts[4] = {0, 0, 0, 0};
    for (const auto& d : docs) {
      counts[0]++;
      counts[static_cast<int>(d.type) + 1]++;
    }
    const char* names[4] = {"All", "Infinite", "Bounded", "Slideshow"};
    for (int i = 0; i < 4; ++i) {
      const bool chip_sel = (input.GetMainMenuTypeFilter() == i);
      canvas->drawRect(layout.chips[i], chip_sel ? active_paint : tile_paint);
      if (chip_sel) canvas->drawRect(layout.chips[i], active_border);
      char raw[48];
      std::snprintf(raw, sizeof(raw), "%s (%llu)", names[i],
                    static_cast<unsigned long long>(counts[i]));
      const std::string shown =
          Ellipsize(raw, small_font, layout.chips[i].width() - 8.0f);
      const float tw = small_font.measureText(shown.c_str(), shown.size());
      canvas->drawString(shown.c_str(), layout.chips[i].centerX() - tw * 0.5f,
                         layout.chips[i].top + 17.0f, small_font,
                         chip_sel ? text_paint : dim_paint);
    }
  }

  for (const auto& cell : layout.cells) {
    const bool is_sel = (cell.row == selected);
    canvas->drawRect(cell.rect, is_sel ? active_paint : tile_paint);
    if (is_sel) canvas->drawRect(cell.rect, active_border);
    canvas->drawRect(cell.thumb, thumb_bg);
    if (cell.row == 0) {
      std::string plus = "+";
      const float pw = big_font.measureText(plus.c_str(), plus.size());
      canvas->drawString(plus.c_str(), cell.thumb.centerX() - pw * 0.5f,
                         cell.thumb.centerY() + 14.0f, big_font, text_paint);
      const std::string label = "New document";
      const float lw = small_font.measureText(label.c_str(), label.size());
      canvas->drawString(label.c_str(), cell.rect.centerX() - lw * 0.5f,
                         cell.rect.bottom - 24.0f, small_font, text_paint);
      canvas->drawString("Ctrl+N", cell.rect.centerX() - 22.0f,
                         cell.rect.bottom - 10.0f, small_font, dim_paint);
      continue;
    }
    const size_t fi = static_cast<size_t>(cell.row - 1);
    if (fi >= filt.size() || filt[fi] >= docs.size()) continue;
    const DocEntry& doc = docs[filt[fi]];
    if (const Bitmap* thumb = ThumbnailFor(doc, typeface)) {
      if (!thumb->empty()) {
        canvas->save();
        canvas->translate(cell.thumb.left, cell.thumb.top);
        const float sx = cell.thumb.width() / thumb->width();
        const float sy = cell.thumb.height() / thumb->height();
        canvas->scale(sx, sy);
        canvas->drawImage(*thumb, 0, 0);
        canvas->restore();
      }
    }
    // × delete button. Armed (clicked once) shows red: click again to
    // confirm, Esc cancels.
    const bool armed = input.IsDeleteArmedForPath(doc.path);
    Paint del_bg = armed ? Paint::Fill(MakeARGB(255, 190, 60, 60))
                         : Paint::Fill(MakeARGB(255, 52, 52, 56));
    canvas->drawRect(cell.del, del_bg);
    Paint cross_paint = armed
                            ? Paint::Stroke(MakeARGB(255, 245, 245, 245), 2.0f)
                            : Paint::Stroke(MakeARGB(255, 150, 150, 150), 1.5f);
    const float dx0 = cell.del.left + 5.0f;
    const float dy0 = cell.del.top + 5.0f;
    const float dx1 = cell.del.right - 5.0f;
    const float dy1 = cell.del.bottom - 5.0f;
    canvas->drawLine(dx0, dy0, dx1, dy1, cross_paint);
    canvas->drawLine(dx0, dy1, dx1, dy0, cross_paint);
    const float label_y = cell.rect.bottom - 26.0f;
    const float date_y = cell.rect.bottom - 11.0f;
    const float max_name_w = cell.rect.width() - 16.0f;
    std::string label = Ellipsize(doc.name, small_font, max_name_w);
    canvas->drawString(label.c_str(), cell.rect.left + 8.0f, label_y,
                       small_font, text_paint);
    if (doc.name == open_name) {
      std::string tag = std::string("(open") + (open_dirty ? " *)" : ")");
      const float nw = small_font.measureText(label.c_str(), label.size());
      canvas->drawString(tag.c_str(), cell.rect.left + 12.0f + nw, label_y,
                         small_font, dim_paint);
    }
    const std::string date = FormatDocDate(doc.mtime);
    canvas->drawString(date.c_str(), cell.rect.left + 8.0f, date_y, small_font,
                       dim_paint);
    const std::string type_tag = DocTypeName(doc.type);
    const float tag_w =
        small_font.measureText(type_tag.c_str(), type_tag.size());
    canvas->drawString(type_tag.c_str(), cell.rect.right - 8.0f - tag_w, date_y,
                       small_font, dim_paint);
  }

  if (total_rows <= 1) {
    const float hint_y = layout.search_bar.bottom + kSearchGap + 20.0f;
    if (query.empty()) {
      canvas->drawString("No documents yet \xe2\x80\x94 press Enter or Ctrl+N",
                         title_x, hint_y, body_font, dim_paint);
    } else {
      canvas->drawString(
          "No matches \xe2\x80\x94 Enter makes new, "
          "Esc clears",
          title_x, hint_y, body_font, dim_paint);
    }
  }

  const float footer_y = layout.panel.bottom - 10.0f;
  std::string footer =
      "Type to filter   Tab type   Arrows select   Enter open   "
      "x/dd delete   Ctrl+N new   Esc back";
  float fw = small_font.measureText(footer.c_str(), footer.size());
  float fx = title_x;
  if (fx + fw > layout.panel.right - kPadding) {
    footer = "Filter | Tab | Enter open | x/dd del | Ctrl+N | Esc";
  }
  canvas->drawString(footer.c_str(), fx, footer_y, small_font, dim_paint);

  // New-document chooser overlay(s).
  if (input.IsChoosingDocType()) {
    const DocTypeChoiceLayout choice =
        ComputeDocTypeChoiceLayout(window_width, window_height);
    const DocSizeChoiceLayout sizes =
        ComputeDocSizeChoiceLayout(window_width, window_height);
    const bool sizing = input.IsChoosingDocSize();
    const Rect& cpanel = sizing ? sizes.panel : choice.panel;
    Paint dim = Paint::Fill(MakeARGB(160, 0, 0, 0));
    canvas->drawRect(Rect::MakeXYWH(0, 0, window_width, window_height), dim);
    canvas->drawRect(cpanel, panel_paint);
    canvas->drawRect(cpanel, border);
    Font choice_title(typeface, 20);
    if (!sizing) {
      canvas->drawString("New document", choice.panel.left + kPadding,
                         choice.panel.top + kPadding + 22.0f, choice_title,
                         title_paint);
      const char* names[3] = {"Infinite canvas", "Bounded canvas", "Slideshow"};
      const char* descs[3] = {"Endless free drawing", "Fixed-size page",
                              "Steppable slide deck"};
      for (int i = 0; i < 3; ++i) {
        const bool opt_sel = (input.GetDocTypeChoice() == i);
        canvas->drawRect(choice.rows[i], opt_sel ? active_paint : tile_paint);
        if (opt_sel) canvas->drawRect(choice.rows[i], active_border);
        char key[8];
        std::snprintf(key, sizeof(key), "%d", i + 1);
        canvas->drawString(key, choice.rows[i].left + 12.0f,
                           choice.rows[i].top + 32.0f, body_font, dim_paint);
        canvas->drawString(names[i], choice.rows[i].left + 34.0f,
                           choice.rows[i].top + 22.0f, body_font, text_paint);
        canvas->drawString(descs[i], choice.rows[i].left + 34.0f,
                           choice.rows[i].top + 40.0f, small_font, dim_paint);
      }
      canvas->drawString("Arrows/1-3 pick   Enter creates   Esc cancels",
                         choice.panel.left + kPadding,
                         choice.panel.bottom - 10.0f, small_font, dim_paint);
    } else {
      canvas->drawString("Canvas size", sizes.panel.left + kPadding,
                         sizes.panel.top + kPadding + 22.0f, choice_title,
                         title_paint);
      int preset_count = 0;
      const PageSizePreset* presets = PageSizePresets(&preset_count);
      for (int i = 0; i < 5 && i < preset_count; ++i) {
        const bool opt_sel =
            (input.GetDocSizeChoice() == i && !input.IsEditingCustomSize());
        canvas->drawRect(sizes.rows[i], opt_sel ? active_paint : tile_paint);
        if (opt_sel) canvas->drawRect(sizes.rows[i], active_border);
        char key[8];
        std::snprintf(key, sizeof(key), "%d", i + 1);
        canvas->drawString(key, sizes.rows[i].left + 12.0f,
                           sizes.rows[i].top + 28.0f, body_font, dim_paint);
        canvas->drawString(presets[i].label, sizes.rows[i].left + 34.0f,
                           sizes.rows[i].top + 28.0f, body_font, text_paint);
      }
      // Custom size row: click or arrows+Enter to type "W x H".
      {
        const bool opt_sel =
            (input.GetDocSizeChoice() == 5 || input.IsEditingCustomSize());
        canvas->drawRect(sizes.rows[5], opt_sel ? active_paint : tile_paint);
        if (opt_sel) canvas->drawRect(sizes.rows[5], active_border);
        std::string label = "Custom: ";
        if (!input.GetCustomSizeText().empty()) {
          label += input.GetCustomSizeText();
        } else if (!input.IsEditingCustomSize()) {
          label += "type W x H";
        }
        const std::string shown =
            Ellipsize(label.c_str(), body_font, sizes.rows[5].width() - 40.0f);
        canvas->drawString(
            shown.c_str(), sizes.rows[5].left + 34.0f,
            sizes.rows[5].top + 28.0f, body_font,
            input.IsEditingCustomSize() ? text_paint : dim_paint);
        if (input.IsEditingCustomSize() && caret_on) {
          const float qw = body_font.measureText(shown.c_str(), shown.size());
          canvas->drawRect(
              Rect::MakeXYWH(sizes.rows[5].left + 34.0f + qw + 3.0f,
                             sizes.rows[5].top + 6.0f, 2.0f, kCaretH),
              caret_paint);
        }
      }
      canvas->drawString("Arrows/1-5 pick   Enter creates   Esc back",
                         sizes.panel.left + kPadding,
                         sizes.panel.bottom - 10.0f, small_font, dim_paint);
    }
  }
}

bool HandleMainMenuClick(float mx, float my, int window_width,
                         int window_height, InputManager& input) {
  if (input.IsChoosingDocType()) {
    if (input.IsChoosingDocSize()) {
      const DocSizeChoiceLayout sizes =
          ComputeDocSizeChoiceLayout(window_width, window_height);
      const int hit = DocSizeChoiceHitRow(sizes, mx, my);
      if (hit >= 0) {
        input.SetDocSizeChoice(hit);
        input.ActivateDocSizeChoice();
      } else if (!sizes.panel.contains(mx, my)) {
        input.CancelDocSizeChoice();
      }
      return true;
    }
    const DocTypeChoiceLayout choice =
        ComputeDocTypeChoiceLayout(window_width, window_height);
    const int hit = DocTypeChoiceHitRow(choice, mx, my);
    if (hit >= 0) {
      input.SetDocTypeChoice(hit);
      input.ActivateDocTypeChoice();
    } else if (!choice.panel.contains(mx, my)) {
      input.CancelDocTypeChoice();
    }
    return true;
  }
  const int total_rows = input.GetMainMenuRowCount();
  const MainMenuLayout layout = ComputeMainMenuLayout(
      window_width, window_height, total_rows, input.GetMainMenuSelected());
  // × delete buttons take precedence over tile selection.
  const int del_hit = MainMenuHitDeleteRow(layout, mx, my);
  if (del_hit >= 0) {
    input.SetMainMenuSelected(del_hit);
    input.ActivateMainMenuDelete(del_hit);
    return true;
  }
  if (MainMenuHitBrowse(layout, mx, my)) {
    input.RequestBrowseForDoc();
    return true;
  }
  const int filter_hit = MainMenuHitFilter(layout, mx, my);
  if (filter_hit >= 0) {
    input.SetMainMenuTypeFilter(filter_hit);
    return true;
  }
  if (MainMenuHitSearch(layout, mx, my)) {
    return true;  // focus is implicit; typing always filters
  }
  const int hit = MainMenuHitRow(layout, mx, my);
  if (hit < 0) return true;  // modal: consume clicks outside tiles too
  input.SetMainMenuSelected(hit);
  input.ActivateMainMenuSelection();
  return true;
}

}  // namespace nanote
