#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace nanote {

inline std::string Trim(std::string_view s) {
  const size_t start = s.find_first_not_of(" \t\n\r");
  if (start == std::string_view::npos) {
    return "";
  }
  const size_t end = s.find_last_not_of(" \t\n\r");
  return std::string(s.substr(start, end - start + 1));
}

inline std::string Lower(std::string_view s) {
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return out;
}

}  // namespace nanote
