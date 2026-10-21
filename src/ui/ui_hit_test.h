#pragma once

#include "color/color_manager.h"
#include "core/font.h"
#include "input/input_manager.h"
#include "layer/layer.h"

#include <memory>

namespace nanote {

// Returns true if the click was consumed by UI.
bool HandleMouseClick(float mx, float my, int window_width, int window_height,
                      InputManager& input, LayerStack& layer_stack,
                      ColorManager& color_manager,
                      const std::shared_ptr<Typeface>& typeface);

// True when a point lies inside the collapsible palette strip.
bool IsPointInPaletteBounds(float x, float y, const InputManager& input,
                            const ColorManager& color_manager, int window_width,
                            int window_height);

}  // namespace nanote
