#pragma once

#include <SDL3/SDL.h>

#include <blend2d/blend2d.h>

namespace nanote {

// Owns the SDL window/renderer/streaming texture plus the CPU Blend2D image
// the frame is composed into. RAII teardown replaces the repeated destroy
// chains on every early return; Resize recreates targets with
// create-before-destroy so a transient failure can never leave a null
// texture behind (permanent black window). Zero-size configures (minimized /
// compositor-deferred sizing) are ignored.
class SdlHost {
 public:
  SdlHost();
  ~SdlHost();

  SdlHost(const SdlHost&) = delete;
  SdlHost& operator=(const SdlHost&) = delete;

  // Creates window, renderer, texture and image. False on failure (logs).
  [[nodiscard]] bool Init(int width, int height, const char* title);
  // Recreates texture + image for a new window size. No-op when nothing
  // changed or the size is not positive.
  void Resize(int width, int height);
  // Uploads the image to the texture and presents the frame.
  void Present();

  [[nodiscard]] SDL_Window* window() const {
    return window_;
  }
  [[nodiscard]] BLImage& image() {
    return image_;
  }

 private:
  [[nodiscard]] SDL_Texture* CreateTexture(int w, int h);

  SDL_Window* window_ = nullptr;
  SDL_Renderer* renderer_ = nullptr;
  SDL_Texture* texture_ = nullptr;
  BLImage image_;
  int width_ = 0;
  int height_ = 0;
};

}  // namespace nanote
