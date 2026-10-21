#pragma once

#include "canvas/viewport.h"
#include "color/color_manager.h"
#include "core/types.h"
#include "doc/document.h"
#include "input/input_manager.h"
#include "layer/layer.h"

#include <string>
#include <vector>

namespace nanote {

class CommandExecutor {
 public:
  void Execute(const std::string& command, LayerStack& layer_stack,
               InputManager& input, Viewport& viewport,
               ColorManager& color_manager, const Vec2& cursor_canvas,
               OpenDocument& doc);

  [[nodiscard]] static std::vector<CommandCompletion> GetCompletions(
      const std::string& prefix);

 private:
  void ExecuteLayer(const std::vector<std::string>& tokens,
                    LayerStack& layer_stack, InputManager& input,
                    OpenDocument& doc);
  void ExecuteMode(const std::vector<std::string>& tokens, InputManager& input);
  void ExecuteTool(const std::vector<std::string>& tokens, InputManager& input);
  void ExecuteZoom(const std::vector<std::string>& tokens, Viewport& viewport,
                   OpenDocument& doc);
  void ExecuteView(const std::vector<std::string>& tokens,
                   LayerStack& layer_stack, Viewport& viewport,
                   OpenDocument& doc);
  void ExecuteSlide(const std::vector<std::string>& tokens,
                    const std::string& command, Viewport& viewport,
                    OpenDocument& doc);
  void ExecuteColor(const std::string& command, LayerStack& layer_stack,
                    ColorManager& color_manager, const Vec2& cursor_canvas);
  void ExecuteStroke(const std::string& command, ColorManager& color_manager);
  void ExecuteFill(const std::string& command, ColorManager& color_manager);
  void ExecutePalette(const std::string& command,
                      const std::vector<std::string>& tokens,
                      InputManager& input, ColorManager& color_manager);
  void ExecuteDelete(LayerStack& layer_stack, OpenDocument& doc);
  void ExecuteSelect(const std::vector<std::string>& tokens,
                     LayerStack& layer_stack);
  void ExecuteTransform(const std::vector<std::string>& tokens,
                        LayerStack& layer_stack, OpenDocument& doc);
  void ExecuteGroup(LayerStack& layer_stack, OpenDocument& doc);
  void ExecuteUngroup(LayerStack& layer_stack, OpenDocument& doc);
  void ExecuteFile(const std::string& command,
                   const std::vector<std::string>& tokens,
                   LayerStack& layer_stack, InputManager& input,
                   Viewport& viewport, OpenDocument& doc);
};

}  // namespace nanote
