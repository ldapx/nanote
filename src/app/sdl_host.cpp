#include "app/sdl_host.h"

namespace nanote {

SdlHost::SdlHost() {
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    SDL_Log("SDL_Init failed: %s", SDL_GetError());
  }
}

SdlHost::~SdlHost() {
  if (texture_ != nullptr) SDL_DestroyTexture(texture_);
  if (renderer_ != nullptr) SDL_DestroyRenderer(renderer_);
  if (window_ != nullptr) SDL_DestroyWindow(window_);
  SDL_Quit();
}

bool SdlHost::Init(int width, int height, const char* title) {
  window_ = SDL_CreateWindow(title, width, height, SDL_WINDOW_RESIZABLE);
  if (window_ == nullptr) {
    SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
    return false;
  }
  renderer_ = SDL_CreateRenderer(window_, nullptr);
  if (renderer_ == nullptr) {
    SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
    return false;
  }
  Resize(width, height);
  if (texture_ == nullptr || image_.is_empty()) {
    SDL_Log("Failed to create render targets");
    return false;
  }
  width_ = width;
  height_ = height;
  return true;
}

void SdlHost::Resize(int width, int height) {
  if (width <= 0 || height <= 0) return;
  if (texture_ != nullptr && width == width_ && height == height_) return;
  if (SDL_Texture* fresh = CreateTexture(width, height)) {
    if (texture_ != nullptr) SDL_DestroyTexture(texture_);
    texture_ = fresh;
    image_ = BLImage(width, height, BL_FORMAT_PRGB32);
    width_ = width;
    height_ = height;
  } else {
    SDL_Log("recreate texture %dx%d failed: %s", width, height, SDL_GetError());
  }
}

void SdlHost::Present() {
  BLImageData img_data;
  if (image_.get_data(&img_data) != BL_SUCCESS) return;
  void* pixels = nullptr;
  int pitch = 0;
  if (!SDL_LockTexture(texture_, nullptr, &pixels, &pitch)) return;
  const int row_bytes = static_cast<int>(img_data.stride);
  const int copy_bytes = (row_bytes < pitch) ? row_bytes : pitch;
  const auto* src = static_cast<const uint8_t*>(img_data.pixel_data);
  auto* dst = static_cast<uint8_t*>(pixels);
  const int height = image_.height();
  for (int y = 0; y < height; ++y) {
    SDL_memcpy(dst + y * pitch, src + y * row_bytes, copy_bytes);
  }
  SDL_UnlockTexture(texture_);
  SDL_RenderClear(renderer_);
  SDL_RenderTexture(renderer_, texture_, nullptr, nullptr);
  SDL_RenderPresent(renderer_);
}

SDL_Texture* SdlHost::CreateTexture(int w, int h) {
  if (w <= 0 || h <= 0 || renderer_ == nullptr) return nullptr;
  return SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888,
                           SDL_TEXTUREACCESS_STREAMING, w, h);
}

}  // namespace nanote
