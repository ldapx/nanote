#include "color/palette.h"

#include <algorithm>

namespace nanote {

namespace {

constexpr Color kDefaultPalette[Palette::kMaxRows][Palette::kMaxTones] = {
    {MakeARGB(255, 0, 0, 0), MakeARGB(255, 28, 28, 28),
     MakeARGB(255, 56, 56, 56), MakeARGB(255, 96, 96, 96),
     MakeARGB(255, 140, 140, 140), MakeARGB(255, 184, 184, 184),
     MakeARGB(255, 220, 220, 220), MakeARGB(255, 255, 255, 255)},
    {MakeARGB(255, 43, 0, 0), MakeARGB(255, 76, 8, 8),
     MakeARGB(255, 112, 16, 16), MakeARGB(255, 150, 26, 26),
     MakeARGB(255, 190, 42, 42), MakeARGB(255, 224, 78, 78),
     MakeARGB(255, 242, 130, 130), MakeARGB(255, 255, 188, 188)},
    {MakeARGB(255, 52, 24, 0), MakeARGB(255, 85, 40, 0),
     MakeARGB(255, 118, 55, 0), MakeARGB(255, 158, 74, 0),
     MakeARGB(255, 198, 100, 0), MakeARGB(255, 226, 138, 31),
     MakeARGB(255, 243, 179, 91), MakeARGB(255, 255, 219, 156)},
    {MakeARGB(255, 52, 42, 0), MakeARGB(255, 84, 68, 0),
     MakeARGB(255, 120, 95, 0), MakeARGB(255, 160, 124, 0),
     MakeARGB(255, 201, 157, 0), MakeARGB(255, 226, 187, 42),
     MakeARGB(255, 241, 214, 104), MakeARGB(255, 252, 238, 168)},
    {MakeARGB(255, 0, 46, 12), MakeARGB(255, 0, 75, 18),
     MakeARGB(255, 0, 105, 24), MakeARGB(255, 16, 140, 39),
     MakeARGB(255, 42, 176, 64), MakeARGB(255, 88, 206, 105),
     MakeARGB(255, 143, 227, 151), MakeARGB(255, 198, 244, 202)},
    {MakeARGB(255, 0, 44, 46), MakeARGB(255, 0, 71, 76),
     MakeARGB(255, 0, 99, 108), MakeARGB(255, 0, 133, 143),
     MakeARGB(255, 12, 170, 181), MakeARGB(255, 69, 202, 212),
     MakeARGB(255, 127, 225, 232), MakeARGB(255, 188, 244, 247)},
    {MakeARGB(255, 0, 23, 61), MakeARGB(255, 0, 40, 98),
     MakeARGB(255, 0, 61, 137), MakeARGB(255, 0, 86, 179),
     MakeARGB(255, 26, 117, 214), MakeARGB(255, 82, 154, 235),
     MakeARGB(255, 140, 193, 246), MakeARGB(255, 199, 226, 252)},
    {MakeARGB(255, 36, 0, 62), MakeARGB(255, 58, 0, 98),
     MakeARGB(255, 81, 0, 136), MakeARGB(255, 109, 21, 176),
     MakeARGB(255, 142, 52, 210), MakeARGB(255, 177, 101, 229),
     MakeARGB(255, 211, 153, 241), MakeARGB(255, 236, 205, 250)},
};

int ClampRows(int row_count) {
  return std::clamp(row_count, Palette::kMinRows, Palette::kMaxRows);
}

int ClampTones(int tone_count) {
  return std::clamp(tone_count, Palette::kMinTones, Palette::kMaxTones);
}

}  // namespace

Palette::Palette() {
  Resize(kMaxRows, kMaxTones);
}

Palette::Palette(int row_count, int tone_count) {
  Resize(row_count, tone_count);
}

Color Palette::GetColor(int row, int tone) const {
  if (!IsValidCell(row, tone)) {
    return MakeARGB(255, 128, 128, 128);
  }
  return colors_[CellIndex(row, tone)];
}

void Palette::SetColor(int row, int tone, Color color) {
  if (!IsValidCell(row, tone)) {
    return;
  }
  colors_[CellIndex(row, tone)] = color;
}

void Palette::SetActiveCell(int row, int tone) {
  if (!IsValidCell(row, tone)) {
    return;
  }
  active_row_ = row;
  active_tone_ = tone;
}

void Palette::Resize(int row_count, int tone_count) {
  row_count_ = ClampRows(row_count);
  tone_count_ = ClampTones(tone_count);
  colors_.assign(row_count_ * tone_count_, kColorBlack);

  for (int row = 0; row < row_count_; ++row) {
    for (int tone = 0; tone < tone_count_; ++tone) {
      colors_[CellIndex(row, tone)] = kDefaultPalette[row][tone];
    }
  }

  active_row_ = std::clamp(active_row_, 0, row_count_ - 1);
  active_tone_ = std::clamp(active_tone_, 0, tone_count_ - 1);
}

bool Palette::IsValidCell(int row, int tone) const {
  return row >= 0 && row < row_count_ && tone >= 0 && tone < tone_count_;
}

int Palette::CellIndex(int row, int tone) const {
  return row * tone_count_ + tone;
}

}  // namespace nanote
