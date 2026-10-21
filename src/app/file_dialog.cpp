#include "app/file_dialog.h"

#include <mutex>

namespace nanote {
namespace {

std::mutex& DialogMutex() {
  static std::mutex m;
  return m;
}

std::vector<std::string>& DialogPendingPaths() {
  static std::vector<std::string> v;
  return v;
}

}  // namespace

void SDLCALL OnOpenDocDialog(void* userdata, const char* const* filelist,
                             int filter) {
  (void)userdata;
  (void)filter;
  if (filelist == nullptr) return;
  std::lock_guard<std::mutex> lock(DialogMutex());
  for (int i = 0; filelist[i] != nullptr; ++i) {
    DialogPendingPaths().push_back(filelist[i]);
    break;  // single selection
  }
}

std::vector<std::string> TakePendingDialogPaths() {
  std::lock_guard<std::mutex> lock(DialogMutex());
  std::vector<std::string> picked;
  picked.swap(DialogPendingPaths());
  return picked;
}

}  // namespace nanote
