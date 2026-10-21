#include "app/app.h"

// SDL entry-point abstraction (WinMain etc.): SDL injects the platform
// entry point and calls main(). Include exactly once, in this file.
// Pairs with WIN32_EXECUTABLE in CMakeLists.txt.
#include <SDL3/SDL_main.h>

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  nanote::App app;
  return app.Run();
}
