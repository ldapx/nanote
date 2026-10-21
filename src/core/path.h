#pragma once

#include <blend2d/blend2d.h>

#include "core/geometry.h"

#include <vector>

namespace nanote {

enum class PathDirection { kCW, kCCW };

class Path {
 public:
  Path() = default;
  Path(const Path&) = default;
  Path& operator=(const Path&) = default;

  void moveTo(float x, float y);
  void lineTo(float x, float y);
  void quadTo(float cx, float cy, float x, float y);
  void cubicTo(float c0x, float c0y, float c1x, float c1y, float x, float y);
  void close();

  void addRect(const Rect& r, PathDirection dir = PathDirection::kCW);
  void addCircle(float cx, float cy, float radius,
                 PathDirection dir = PathDirection::kCW);
  void addOval(const Rect& r, PathDirection dir = PathDirection::kCW);
  // Approximate circular/elliptical arc defined by oval bounds.
  void arcTo(const Rect& oval, float startDeg, float sweepDeg,
             bool forceMoveTo);

  void clear();
  bool isEmpty() const {
    return cmds_.empty();
  }

  Rect getBounds() const;
  bool contains(float x, float y) const;

  // Returns transformed copy.
  Path transformed(const Matrix& m) const;
  void transform(const Matrix& m);

  // Convert to Blend2D path for rendering.
  BLPath toBLPath() const;

  struct IterCmd {
    int verb;  // 0=move,1=line,2=quad,3=cubic,4=close
    float pts[6];
  };
  const std::vector<IterCmd>& cmds() const {
    return cmds_;
  }

 private:
  std::vector<IterCmd> cmds_;
};

}  // namespace nanote
