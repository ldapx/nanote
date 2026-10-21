#pragma once

#include "core/color.h"

#include <vector>

namespace nanote {

class Palette {
 public:
  static constexpr int kMinRows = 2;
  static constexpr int kMaxRows = 8;
  static constexpr int kMinTones = 1;
  static constexpr int kMaxTones = 8;

  Palette();
  Palette(int row_count, int tone_count);

  [[nodiscard]] int GetRowCount() const {
    return row_count_;
  }
  [[nodiscard]] int GetToneCount() const {
    return tone_count_;
  }

  [[nodiscard]] int GetDefaultTone() const {
    return default_tone_;
  }
  void SetDefaultTone(int tone) {
    default_tone_ = tone;
  }

  [[nodiscard]] Color GetColor(int row, int tone) const;
  void SetColor(int row, int tone, Color color);

  [[nodiscard]] int GetActiveRow() const {
    return active_row_;
  }
  [[nodiscard]] int GetActiveTone() const {
    return active_tone_;
  }
  void SetActiveCell(int row, int tone);

  void Resize(int row_count, int tone_count);

 private:
  [[nodiscard]] bool IsValidCell(int row, int tone) const;
  [[nodiscard]] int CellIndex(int row, int tone) const;

  int row_count_ = kMaxRows;
  int tone_count_ = kMaxTones;
  int default_tone_ = 0;
  int active_row_ = 0;
  int active_tone_ = 0;
  std::vector<Color> colors_;
};

}  // namespace nanote
