#include "command/command_executor.h"

#include "core/string_utils.h"
#include "drawing/drawing.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <optional>
#include <sstream>
#include <string_view>

namespace nanote {

namespace {

std::vector<std::string> Tokenize(const std::string& cmd) {
  std::vector<std::string> tokens;
  std::istringstream stream(cmd);
  std::string token;
  while (stream >> token) {
    tokens.push_back(token);
  }
  return tokens;
}

Tool ParseTool(const std::string& name) {
  if (name == "pencil") return Tool::kPencil;
  if (name == "pen") return Tool::kPen;
  if (name == "brush") return Tool::kBrush;
  if (name == "marker") return Tool::kMarker;
  if (name == "spray") return Tool::kSpray;
  if (name == "line") return Tool::kLine;
  if (name == "rectangle") return Tool::kRectangle;
  if (name == "circle") return Tool::kCircle;
  if (name == "ellipse") return Tool::kEllipse;
  if (name == "polygon") return Tool::kPolygon;
  if (name == "star") return Tool::kStar;
  if (name == "arc") return Tool::kArc;
  if (name == "bezier") return Tool::kBezier;
  if (name == "distance") return Tool::kDistance;
  if (name == "angle") return Tool::kAngle;
  if (name == "perpendicular") return Tool::kPerpendicular;
  if (name == "tangent") return Tool::kTangent;
  if (name == "grid") return Tool::kGridTool;
  if (name == "stroke") return Tool::kStroke;
  if (name == "area") return Tool::kArea;
  if (name == "point") return Tool::kPoint;
  if (name == "single") return Tool::kSingle;
  if (name == "lasso") return Tool::kLasso;
  if (name == "magic") return Tool::kMagic;
  if (name == "all") return Tool::kAll;
  if (name == "solid") return Tool::kSolid;
  if (name == "gradient") return Tool::kGradient;
  if (name == "pattern") return Tool::kPattern;
  if (name == "move") return Tool::kMove;
  if (name == "scale") return Tool::kScale;
  if (name == "rotate") return Tool::kRotate;
  if (name == "skew") return Tool::kSkew;
  if (name == "mirror") return Tool::kMirror;
  if (name == "edit") return Tool::kEdit;
  if (name == "smooth") return Tool::kSmooth;
  if (name == "corner") return Tool::kCorner;
  if (name == "break") return Tool::kBreak;
  if (name == "join") return Tool::kJoin;
  return Tool::kNone;
}

[[nodiscard]] std::optional<Mode> ParseMode(const std::string& name) {
  if (name == "navigate" || name == "nav") return Mode::kNavigate;
  if (name == "draw") return Mode::kDraw;
  if (name == "shape") return Mode::kShape;
  if (name == "ruler") return Mode::kRuler;
  if (name == "erase") return Mode::kErase;
  if (name == "select" || name == "sel") return Mode::kSelect;
  if (name == "text") return Mode::kText;
  if (name == "fill") return Mode::kFill;
  if (name == "transform" || name == "trans") return Mode::kTransform;
  if (name == "node") return Mode::kNode;
  return std::nullopt;
}

std::string RemainderAfterWords(const std::string& command, int word_count) {
  size_t pos = 0;
  int seen_words = 0;

  while (pos < command.size()) {
    pos = command.find_first_not_of(" \t\n\r", pos);
    if (pos == std::string::npos) {
      return "";
    }

    const size_t end = command.find_first_of(" \t\n\r", pos);
    ++seen_words;
    if (seen_words == word_count) {
      const size_t remainder_pos =
          (end == std::string::npos) ? command.size() : end;
      return Trim(std::string_view(command).substr(remainder_pos));
    }

    if (end == std::string::npos) {
      return "";
    }
    pos = end;
  }

  return "";
}

struct CommandSpec {
  const char* insert_text;
  const char* display_text;
};

constexpr CommandSpec kCommandSpecs[] = {
    {"layer", "layer <index|subcommand>"},
    {"layer new raster", "layer new raster <name>"},
    {"layer new vector", "layer new vector <name>"},
    {"layer delete", "layer delete <index>"},
    {"layer up", "layer up"},
    {"layer down", "layer down"},
    {"layer flatten", "layer flatten"},
    {"layer opacity", "layer opacity <0-1|0%-100%>"},
    {"mode", "mode <name>"},
    {"mode navigate", "mode navigate"},
    {"mode draw", "mode draw"},
    {"mode shape", "mode shape"},
    {"mode ruler", "mode ruler"},
    {"mode erase", "mode erase"},
    {"mode select", "mode select"},
    {"mode text", "mode text"},
    {"mode fill", "mode fill"},
    {"mode transform", "mode transform"},
    {"mode node", "mode node"},
    {"tool", "tool <name>"},
    {"tool pencil", "tool pencil"},
    {"tool pen", "tool pen"},
    {"tool brush", "tool brush"},
    {"tool marker", "tool marker"},
    {"tool spray", "tool spray"},
    {"tool line", "tool line"},
    {"tool rectangle", "tool rectangle"},
    {"tool circle", "tool circle"},
    {"tool ellipse", "tool ellipse"},
    {"tool polygon", "tool polygon"},
    {"tool star", "tool star"},
    {"tool arc", "tool arc"},
    {"tool bezier", "tool bezier"},
    {"zoom", "zoom <in|out|fit|percent>"},
    {"zoom in", "zoom in"},
    {"zoom out", "zoom out"},
    {"zoom fit", "zoom fit"},
    {"move", "move <x> <y>"},
    {"pan", "pan <dx> <dy>"},
    {"grid", "grid"},
    {"origin", "origin"},
    {"color", "color <hex code>"},
    {"color pick", "color pick"},
    {"stroke", "stroke <hex code>"},
    {"fill", "fill <hex code|none>"},
    {"palette", "palette"},
    {"palette new", "palette new <name> <rows> [tones]"},
    {"palette add", "palette add <name> <rows> [tones]"},
    {"palette load", "palette load <name>"},
    {"palette remove", "palette remove <name>"},
    {"palette set", "palette set <row> <tone> <hex code>"},
    {"palette cycle", "palette cycle"},
    {"palette prev", "palette prev"},
    {"scale", "scale <factor> | scale <sx> <sy>"},
    {"rotate", "rotate <degrees> [<cx> <cy>]"},
    {"flip", "flip h|v"},
    {"align", "align left|right|center|top|bottom|middle"},
    {"distribute", "distribute h|v"},
    {"group", "group"},
    {"ungroup", "ungroup"},
    {"undo", "undo"},
    {"redo", "redo"},
    {"save", "save [<name>]"},
    {"saveas", "saveas <name>"},
    {"open", "open <name>"},
    {"new", "new [infinite|bounded|slideshow]"},
    {"new infinite", "new infinite"},
    {"new bounded", "new bounded"},
    {"new slideshow", "new slideshow"},
    {"page", "page <w> <h>"},
    {"slide", "slide <new|goto|next|prev|delete>"},
    {"slide new", "slide new [name]"},
    {"slide goto", "slide goto <n|name>"},
    {"slide next", "slide next"},
    {"slide prev", "slide prev"},
    {"slide delete", "slide delete <n>"},
};

}  // namespace

std::vector<CommandCompletion> CommandExecutor::GetCompletions(
    const std::string& prefix) {
  std::vector<CommandCompletion> matches;
  for (const CommandSpec& spec : kCommandSpecs) {
    const std::string_view insert_text(spec.insert_text);
    if (insert_text.size() >= prefix.size() &&
        insert_text.substr(0, prefix.size()) == prefix) {
      const auto duplicate =
          std::find_if(matches.begin(), matches.end(),
                       [&](const CommandCompletion& completion) {
                         return completion.insert_text == spec.insert_text;
                       });
      if (duplicate == matches.end()) {
        matches.push_back({spec.insert_text, spec.display_text});
      }
    }
  }
  return matches;
}

void CommandExecutor::Execute(const std::string& command,
                              LayerStack& layer_stack, InputManager& input,
                              Viewport& viewport, ColorManager& color_manager,
                              const Vec2& cursor_canvas, OpenDocument& doc) {
  const auto tokens = Tokenize(command);
  if (tokens.empty()) {
    return;
  }

  const std::string& verb = tokens[0];
  if (verb == "layer") {
    ExecuteLayer(tokens, layer_stack, input, doc);
  } else if (verb == "mode") {
    ExecuteMode(tokens, input);
  } else if (verb == "tool") {
    ExecuteTool(tokens, input);
  } else if (verb == "zoom") {
    ExecuteZoom(tokens, viewport, doc);
  } else if (verb == "move" || verb == "pan") {
    ExecuteView(tokens, layer_stack, viewport, doc);
  } else if (verb == "grid" || verb == "origin" || verb == "page") {
    ExecuteView(tokens, layer_stack, viewport, doc);
  } else if (verb == "slide") {
    ExecuteSlide(tokens, command, viewport, doc);
  } else if (verb == "color") {
    ExecuteColor(command, layer_stack, color_manager, cursor_canvas);
  } else if (verb == "stroke") {
    ExecuteStroke(command, color_manager);
  } else if (verb == "fill") {
    ExecuteFill(command, color_manager);
  } else if (verb == "palette") {
    ExecutePalette(command, tokens, input, color_manager);
  } else if (verb == "delete") {
    ExecuteDelete(layer_stack, doc);
  } else if (verb == "select") {
    ExecuteSelect(tokens, layer_stack);
  } else if (verb == "scale" || verb == "rotate" || verb == "flip" ||
             verb == "align" || verb == "distribute") {
    ExecuteTransform(tokens, layer_stack, doc);
  } else if (verb == "group") {
    ExecuteGroup(layer_stack, doc);
  } else if (verb == "ungroup") {
    ExecuteUngroup(layer_stack, doc);
  } else if (verb == "undo") {
    input.RequestUndo();
  } else if (verb == "redo") {
    input.RequestRedo();
  } else if (verb == "save" || verb == "saveas" || verb == "open" ||
             verb == "new") {
    ExecuteFile(command, tokens, layer_stack, input, viewport, doc);
    return;  // save clears dirty itself; open/new defer to the main loop
  }

  // Content-mutating commands dirty the document (over-marking on no-ops
  // is harmless: autosave then just rewrites identical bytes).
  const bool mutated = verb == "delete" || verb == "group" ||
                       verb == "ungroup" || verb == "scale" ||
                       verb == "rotate" || verb == "flip" || verb == "align" ||
                       verb == "distribute" ||
                       (verb == "layer" && tokens.size() >= 2 &&
                        (tokens[1] == "new" || tokens[1] == "delete" ||
                         tokens[1] == "up" || tokens[1] == "down" ||
                         tokens[1] == "flatten" || tokens[1] == "opacity"));
  if (mutated) doc.dirty = true;
}

namespace {

// Strips one pair of surrounding double quotes, if present.
std::string Dequote(std::string s) {
  s = Trim(s);
  if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
    return s.substr(1, s.size() - 2);
  }
  return s;
}

}  // namespace

namespace {

std::string ResolveDocPath(const std::string& arg) {
  if (arg.empty()) return "";
  if (arg.find('/') != std::string::npos ||
      arg.find('\\') != std::string::npos) {
    return arg;  // literal filesystem path
  }
  return PathForDocName(arg);
}

}  // namespace

void CommandExecutor::ExecuteFile(const std::string& command,
                                  const std::vector<std::string>& tokens,
                                  LayerStack& layer_stack, InputManager& input,
                                  Viewport& viewport, OpenDocument& doc) {
  const std::string& verb = tokens[0];
  if (verb == "new") {
    DocType type = DocType::kInfinite;
    if (tokens.size() >= 2) {
      const std::string kind = Lower(tokens[1]);
      if (kind == "bounded" || kind == "page") {
        type = DocType::kBounded;
      } else if (kind == "slideshow" || kind == "slides") {
        type = DocType::kSlideshow;
      }
    }
    input.RequestNewDoc(type);
    return;
  }
  if (verb == "open") {
    if (tokens.size() < 2) return;
    const std::string arg = Dequote(RemainderAfterWords(command, 1));
    const std::string path = ResolveDocPath(arg);
    if (path.empty()) return;
    input.RequestOpenPath(path);
    return;
  }
  // save / saveas: optional new name, otherwise keep the current one.
  std::string name = doc.name;
  if (tokens.size() >= 2) {
    const std::string arg = Dequote(RemainderAfterWords(command, 1));
    if (!arg.empty()) name = arg;
  }
  if (name.find('/') == std::string::npos &&
      name.find('\\') == std::string::npos) {
    name = SanitizeDocName(name);
    if (name.empty()) return;
  }
  const std::string path = ResolveDocPath(name);
  if (path.empty()) return;
  if (SaveDocument(path, layer_stack, viewport, doc.props)) {
    doc.name = DocNameFromPath(path);
    doc.dirty = false;
  }
}

void CommandExecutor::ExecuteLayer(const std::vector<std::string>& tokens,
                                   LayerStack& layer_stack, InputManager& input,
                                   OpenDocument& doc) {
  (void)doc;
  (void)input;
  if (tokens.size() < 2) {
    return;
  }

  if (tokens[1] == "new" && tokens.size() >= 3) {
    const std::string& type = tokens[2];
    std::string name =
        "Layer " + std::to_string(layer_stack.GetLayers().size() + 1);
    if (tokens.size() >= 4) {
      name = tokens[3];
    }
    if (type == "raster" || type == "r") {
      layer_stack.AddRasterLayer(name, 800, 600);
    } else if (type == "vector" || type == "v") {
      layer_stack.AddVectorLayer(name);
    }
  } else if (tokens[1] == "delete" && tokens.size() >= 3) {
    try {
      const int index = std::stoi(tokens[2]) - 1;
      layer_stack.DeleteLayer(index);
    } catch (...) {
      return;
    }
  } else if (tokens[1] == "up") {
    const int idx = layer_stack.GetActiveLayerIndex();
    if (idx < static_cast<int>(layer_stack.GetLayers().size()) - 1) {
      layer_stack.SetActiveLayerIndex(idx + 1);
    }
  } else if (tokens[1] == "down") {
    const int idx = layer_stack.GetActiveLayerIndex();
    if (idx > 0) {
      layer_stack.SetActiveLayerIndex(idx - 1);
    }
  } else if (tokens[1] == "flatten") {
    layer_stack.Flatten();
  } else if (tokens[1] == "opacity" && tokens.size() >= 3) {
    try {
      float v = std::stof(tokens[2]);
      if (!tokens[2].empty() && tokens[2].back() == '%') {
        v /= 100.0f;
      } else if (v > 1.0f) {
        v /= 100.0f;  // tolerate "50" for 50%
      }
      const int idx = layer_stack.GetActiveLayerIndex();
      if (idx >= 0 && idx < static_cast<int>(layer_stack.GetLayers().size())) {
        layer_stack.GetLayers()[idx]->opacity = std::clamp(v, 0.0f, 1.0f);
      }
    } catch (...) {
      return;
    }
  } else {
    try {
      const int index = std::stoi(tokens[1]) - 1;
      layer_stack.SetActiveLayerIndex(index);
    } catch (...) {
      return;
    }
  }
}

void CommandExecutor::ExecuteMode(const std::vector<std::string>& tokens,
                                  InputManager& input) {
  if (tokens.size() < 2) {
    return;
  }
  const std::optional<Mode> mode = ParseMode(tokens[1]);
  if (!mode.has_value()) {
    return;
  }
  input.SetMode(*mode);
  input.SetTool(DefaultToolForMode(*mode));
}

void CommandExecutor::ExecuteTool(const std::vector<std::string>& tokens,
                                  InputManager& input) {
  if (tokens.size() < 2) {
    return;
  }
  const Tool tool = ParseTool(tokens[1]);
  if (tool != Tool::kNone) {
    input.SetTool(tool);
  }
}

void CommandExecutor::ExecuteZoom(const std::vector<std::string>& tokens,
                                  Viewport& viewport, OpenDocument& doc) {
  if (tokens.size() < 2) {
    return;
  }
  const std::string& val = tokens[1];
  if (val == "in") {
    viewport.ZoomIn();
  } else if (val == "out") {
    viewport.ZoomOut();
  } else if (val == "fit" || val == "100%") {
    if (doc.props.has_page()) {
      // Frame the page.
      viewport.center_.x = 0.0f;
      viewport.center_.y = 0.0f;
      viewport.SetZoom(std::min(viewport.window_width_ / doc.props.page_w,
                                viewport.window_height_ / doc.props.page_h));
    } else {
      viewport.ResetZoom();
    }
  } else {
    try {
      float pct = std::stof(val);
      if (!val.empty() && val.back() == '%') {
        pct /= 100.0f;
      }
      viewport.SetZoom(std::clamp(pct, 0.05f, 32.0f));
    } catch (...) {
      return;
    }
  }
}

void CommandExecutor::ExecuteView(const std::vector<std::string>& tokens,
                                  LayerStack& layer_stack, Viewport& viewport,
                                  OpenDocument& doc) {
  if (tokens.empty()) {
    return;
  }
  const std::string& verb = tokens[0];
  if (verb == "move" && tokens.size() >= 3) {
    try {
      viewport.center_.x = std::stof(tokens[1]);
      viewport.center_.y = std::stof(tokens[2]);
    } catch (...) {
      return;
    }
  } else if (verb == "pan" && tokens.size() >= 3) {
    try {
      viewport.Pan(std::stof(tokens[1]), std::stof(tokens[2]));
    } catch (...) {
      return;
    }
  } else if (verb == "origin") {
    viewport.center_.x = 0;
    viewport.center_.y = 0;
    viewport.ResetZoom();
  } else if (verb == "grid") {
    if (tokens.size() < 2) {
      return;
    }
    if (tokens[1] == "on") {
      viewport.SetGridVisible(true);
    } else if (tokens[1] == "off") {
      viewport.SetGridVisible(false);
    } else {
      try {
        viewport.SetGridSize(std::stof(tokens[1]));
      } catch (...) {
        return;
      }
    }
  } else if (verb == "page") {
    // Bounded-canvas page size in canvas units. Ignored for other types
    // (the type is fixed when the document is created). Shrinking clears
    // raster marks left outside the new page; vector objects are kept.
    if (!doc.props.has_page() || tokens.size() < 3) {
      return;
    }
    try {
      const float w = std::clamp(std::stof(tokens[1]), 1.0f, 16384.0f);
      const float h = std::clamp(std::stof(tokens[2]), 1.0f, 16384.0f);
      doc.props.page_w = w;
      doc.props.page_h = h;
      const Rect page = BoundedPageRect(doc.props);
      for (auto& layer : layer_stack.GetLayers()) {
        if (layer != nullptr && layer->type == LayerType::kRaster) {
          ConstrainRasterToPage(static_cast<RasterLayer*>(layer.get()), page);
        }
      }
      doc.dirty = true;
    } catch (...) {
      return;
    }
  }
}

void CommandExecutor::ExecuteSlide(const std::vector<std::string>& tokens,
                                   const std::string& command,
                                   Viewport& viewport, OpenDocument& doc) {
  if (doc.props.type != DocType::kSlideshow || tokens.size() < 2) {
    return;
  }
  auto& slides = doc.props.slides;
  const std::string& sub = tokens[1];
  auto goto_slide = [&](int idx) {
    if (idx < 0 || idx >= static_cast<int>(slides.size())) return;
    doc.props.current_slide = idx;
    viewport.center_.x = slides[idx].cx;
    viewport.center_.y = slides[idx].cy;
    viewport.SetZoom(std::clamp(slides[idx].zoom, 0.05f, 32.0f));
    doc.dirty = true;
  };
  if (sub == "new") {
    std::string name = Trim(RemainderAfterWords(command, 2));
    if (name.empty()) {
      name = "Slide " + std::to_string(slides.size() + 1);
    }
    slides.push_back({Dequote(name), viewport.center_.x, viewport.center_.y,
                      viewport.zoom_});
    goto_slide(static_cast<int>(slides.size()) - 1);
  } else if (sub == "next" || sub == "prev") {
    if (slides.empty()) return;
    const int n = static_cast<int>(slides.size());
    int cur = doc.props.current_slide;
    if (cur < 0 || cur >= n) cur = 0;
    goto_slide(sub == "next" ? (cur + 1) % n : (cur - 1 + n) % n);
  } else if (sub == "goto" && tokens.size() >= 3) {
    const std::string arg = Trim(Dequote(RemainderAfterWords(command, 2)));
    try {
      goto_slide(std::stoi(arg) - 1);  // 1-based index
      return;
    } catch (...) {
    }
    for (size_t i = 0; i < slides.size(); ++i) {
      if (slides[i].name == arg) {
        goto_slide(static_cast<int>(i));
        return;
      }
    }
  } else if (sub == "delete" && tokens.size() >= 3) {
    try {
      const int idx = std::stoi(tokens[2]) - 1;  // 1-based index
      if (idx < 0 || idx >= static_cast<int>(slides.size())) return;
      slides.erase(slides.begin() + idx);
      if (slides.empty()) {
        doc.props.current_slide = -1;
      } else {
        doc.props.current_slide = std::clamp(
            doc.props.current_slide, 0, static_cast<int>(slides.size()) - 1);
      }
      doc.dirty = true;
    } catch (...) {
    }
  }
}

void CommandExecutor::ExecuteColor(const std::string& command,
                                   LayerStack& layer_stack,
                                   ColorManager& color_manager,
                                   const Vec2& cursor_canvas) {
  const std::string value = RemainderAfterWords(command, 1);
  if (value.empty()) {
    return;
  }

  if (Lower(value) == "pick") {
    color_manager.SetCurrentColor(
        color_manager.PickColor(cursor_canvas.x, cursor_canvas.y, layer_stack));
    return;
  }

  if (const auto color = TryParseColor(value); color.has_value()) {
    color_manager.SetCurrentColor(*color);
  }
}

void CommandExecutor::ExecuteStroke(const std::string& command,
                                    ColorManager& color_manager) {
  const std::string value = RemainderAfterWords(command, 1);
  if (value.empty()) {
    return;
  }

  if (const auto color = TryParseColor(value); color.has_value()) {
    color_manager.SetStrokeColor(*color);
  }
}

void CommandExecutor::ExecuteFill(const std::string& command,
                                  ColorManager& color_manager) {
  const std::string value = RemainderAfterWords(command, 1);
  if (value.empty()) {
    return;
  }

  const std::string lower = Lower(value);
  if (lower == "none" || lower == "clear" || lower == "transparent") {
    color_manager.SetFillColor(kColorTransparent);
    return;
  }

  if (const auto color = TryParseColor(value); color.has_value()) {
    color_manager.SetFillColor(*color);
  }
}

void CommandExecutor::ExecutePalette(const std::string& command,
                                     const std::vector<std::string>& tokens,
                                     InputManager& input,
                                     ColorManager& color_manager) {
  if (tokens.size() == 1) {
    input.SetPaletteBrowserOpen(!input.IsPaletteBrowserOpen());
    if (!input.IsPaletteBrowserOpen()) {
      input.SetPaletteBrowserExpandedName("");
    }
    return;
  }

  const std::string sub = Lower(tokens[1]);
  if (sub == "new" || sub == "add") {
    if (tokens.size() < 4) {
      return;
    }
    const std::string name = tokens[2];
    try {
      const int row_count = std::stoi(tokens[3]);
      const int tone_count =
          tokens.size() >= 5 ? std::stoi(tokens[4])
                             : color_manager.GetActivePalette().GetToneCount();
      const bool created =
          color_manager.NewPalette(name, row_count, tone_count);
      (void)created;
    } catch (...) {
      return;
    }
    return;
  }
  if (sub == "load") {
    if (color_manager.LoadPalette(RemainderAfterWords(command, 2))) {
      input.SetPaletteBrowserOpen(false);
      input.SetPaletteBrowserExpandedName("");
    }
    return;
  }
  if (sub == "remove") {
    if (tokens.size() < 3) {
      return;
    }
    const std::string name = RemainderAfterWords(command, 2);
    if (color_manager.RemovePalette(name) &&
        input.GetPaletteBrowserExpandedName() == name) {
      input.SetPaletteBrowserExpandedName("");
    }
    return;
  }
  if (sub == "set") {
    if (tokens.size() < 5) {
      return;
    }
    try {
      const int row_index = std::stoi(tokens[2]) - 1;
      const int tone_index = std::stoi(tokens[3]) - 1;
      if (const auto color = TryParseColor(RemainderAfterWords(command, 4));
          color.has_value()) {
        const bool updated = color_manager.SetPaletteColor(
            color_manager.GetCurrentPaletteIndex(), row_index, tone_index,
            *color);
        (void)updated;
      }
    } catch (...) {
      return;
    }
    return;
  }
  if (sub == "cycle" || sub == "next") {
    color_manager.CycleColor();
    return;
  }
  if (sub == "prev" || sub == "previous") {
    color_manager.PrevColor();
  }
}

void CommandExecutor::ExecuteDelete(LayerStack& layer_stack,
                                    OpenDocument& doc) {
  (void)doc;
  auto& selection = layer_stack.GetSelection();
  if (!selection.active) {
    return;
  }

  Layer* active =
      layer_stack.GetLayers()[layer_stack.GetActiveLayerIndex()].get();
  if (active->type == LayerType::kRaster) {
    auto* raster = static_cast<RasterLayer*>(active);
    EraseRasterArea(raster, selection.canvas_rect);
  } else if (active->type == LayerType::kVector) {
    auto* vector = static_cast<VectorLayer*>(active);
    std::vector<int> indices = selection.object_indices;
    std::sort(indices.begin(), indices.end(), std::greater<int>());
    for (int idx : indices) {
      if (idx >= 0 && idx < static_cast<int>(vector->objects.size())) {
        vector->objects.erase(vector->objects.begin() + idx);
      }
    }
  }
  selection.Clear();
}

void CommandExecutor::ExecuteSelect(const std::vector<std::string>& tokens,
                                    LayerStack& layer_stack) {
  auto& selection = layer_stack.GetSelection();
  if (tokens.size() < 2) {
    return;
  }

  const std::string& sub = tokens[1];
  Layer* active =
      layer_stack.GetLayers()[layer_stack.GetActiveLayerIndex()].get();
  if (sub == "all") {
    if (active->type == LayerType::kVector) {
      auto* vector = static_cast<VectorLayer*>(active);
      selection.Clear();
      selection.active = true;
      for (int i = 0; i < static_cast<int>(vector->objects.size()); ++i) {
        selection.object_indices.push_back(i);
      }
      if (!vector->objects.empty()) {
        selection.canvas_rect = vector->objects[0]->GetBounds();
        for (const auto& obj : vector->objects) {
          selection.canvas_rect.join(obj->GetBounds());
        }
      }
    } else if (active->type == LayerType::kRaster) {
      auto* raster = static_cast<RasterLayer*>(active);
      selection.Clear();
      selection.active = true;
      selection.canvas_rect =
          Rect::MakeXYWH(static_cast<float>(raster->offset_x),
                         static_cast<float>(raster->offset_y),
                         static_cast<float>(raster->bitmap.width()),
                         static_cast<float>(raster->bitmap.height()));
    }
  } else if (sub == "none") {
    selection.Clear();
  } else if (sub == "invert" && active->type == LayerType::kVector) {
    auto* vector = static_cast<VectorLayer*>(active);
    std::vector<int> new_selection;
    for (int i = 0; i < static_cast<int>(vector->objects.size()); ++i) {
      if (std::find(selection.object_indices.begin(),
                    selection.object_indices.end(),
                    i) == selection.object_indices.end()) {
        new_selection.push_back(i);
      }
    }

    selection.object_indices = std::move(new_selection);
    selection.active = !selection.object_indices.empty();
    if (selection.active) {
      selection.canvas_rect =
          vector->objects[selection.object_indices[0]]->GetBounds();
      for (int idx : selection.object_indices) {
        selection.canvas_rect.join(vector->objects[idx]->GetBounds());
      }
    }
  }
}

void CommandExecutor::ExecuteTransform(const std::vector<std::string>& tokens,
                                       LayerStack& layer_stack,
                                       OpenDocument& doc) {
  auto& selection = layer_stack.GetSelection();
  Layer* active =
      layer_stack.GetLayers()[layer_stack.GetActiveLayerIndex()].get();
  if (active == nullptr) return;

  const std::string& verb = tokens[0];
  if (verb == "scale") {
    if (tokens.size() >= 3) {
      try {
        float sx = std::stof(tokens[1]);
        float sy = std::stof(tokens[2]);
        float cx = selection.canvas_rect.centerX();
        float cy = selection.canvas_rect.centerY();
        TransformSelectionScale(selection, active, cx, cy, sx, sy);
      } catch (...) {
      }
    } else if (tokens.size() >= 2) {
      try {
        float s = std::stof(tokens[1]);
        float cx = selection.canvas_rect.centerX();
        float cy = selection.canvas_rect.centerY();
        TransformSelectionScale(selection, active, cx, cy, s, s);
      } catch (...) {
      }
    }
  } else if (verb == "rotate") {
    if (tokens.size() >= 4) {
      try {
        float deg = std::stof(tokens[1]);
        float cx = std::stof(tokens[2]);
        float cy = std::stof(tokens[3]);
        TransformSelectionRotate(selection, active, cx, cy, deg);
      } catch (...) {
      }
    } else if (tokens.size() >= 2) {
      try {
        float deg = std::stof(tokens[1]);
        float cx = selection.canvas_rect.centerX();
        float cy = selection.canvas_rect.centerY();
        TransformSelectionRotate(selection, active, cx, cy, deg);
      } catch (...) {
      }
    }
  } else if (verb == "flip") {
    if (tokens.size() >= 2) {
      float cx = selection.canvas_rect.centerX();
      float cy = selection.canvas_rect.centerY();
      if (tokens[1] == "h") {
        TransformSelectionFlipH(selection, active, cx);
      } else if (tokens[1] == "v") {
        TransformSelectionFlipV(selection, active, cy);
      }
    }
  } else if (verb == "align") {
    if (tokens.size() >= 2) {
      AlignSelection(selection, active, tokens[1]);
    }
  } else if (verb == "distribute") {
    if (tokens.size() >= 2) {
      DistributeSelection(selection, active, tokens[1]);
    }
  }
  // Bounded canvas: transforms must not push pixels off the page.
  if (doc.props.has_page() && active->type == LayerType::kRaster) {
    ConstrainRasterToPage(static_cast<RasterLayer*>(active),
                          BoundedPageRect(doc.props));
  }
}

void CommandExecutor::ExecuteGroup(LayerStack& layer_stack, OpenDocument& doc) {
  (void)doc;
  Layer* active =
      layer_stack.GetLayers()[layer_stack.GetActiveLayerIndex()].get();
  if (active == nullptr || active->type != LayerType::kVector) return;
  auto* vector = static_cast<VectorLayer*>(active);
  GroupSelection(vector, layer_stack.GetSelection());
}

void CommandExecutor::ExecuteUngroup(LayerStack& layer_stack,
                                     OpenDocument& doc) {
  (void)doc;
  Layer* active =
      layer_stack.GetLayers()[layer_stack.GetActiveLayerIndex()].get();
  if (active == nullptr || active->type != LayerType::kVector) return;
  auto* vector = static_cast<VectorLayer*>(active);
  UngroupSelection(vector, layer_stack.GetSelection());
}

}  // namespace nanote
