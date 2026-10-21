#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <vector>

namespace nanote {

// Native .nano open-dialog results arrive on another thread, so the SDL
// callback only queues paths; the main loop drains them via
// TakePendingDialogPaths and opens through the usual RequestOpenPath flow.
void SDLCALL OnOpenDocDialog(void* userdata, const char* const* filelist,
                             int filter);
std::vector<std::string> TakePendingDialogPaths();

}  // namespace nanote
