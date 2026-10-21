cmake_minimum_required(VERSION 3.24)
project(nanote CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- SDL3 (system; >= 3.2 for SDL_GetUserFolder) ---
find_package(SDL3 3.2 REQUIRED)

# --- Blend2D + AsmJit via FetchContent ---
include(FetchContent)

# AsmJit must be available for Blend2D (Blend2D looks in 3rdparty/asmjit,
# but FetchContent lets us provide it explicitly).
FetchContent_Declare(
  asmjit
  GIT_REPOSITORY https://github.com/asmjit/asmjit.git
  GIT_TAG master
  GIT_SHALLOW TRUE
)
FetchContent_GetProperties(asmjit)
if (NOT asmjit_POPULATED)
  FetchContent_Populate(asmjit)
endif()
set(ASMJIT_DIR "${asmjit_SOURCE_DIR}" CACHE PATH "" FORCE)

set(BLEND2D_STATIC TRUE CACHE BOOL "" FORCE)
set(BLEND2D_TEST FALSE CACHE BOOL "" FORCE)

FetchContent_Declare(
  blend2d
  GIT_REPOSITORY https://github.com/blend2d/blend2d.git
  GIT_TAG master
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(blend2d)

# --- Nanote sources ---
file(GLOB_RECURSE NANOTE_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

add_executable(nanote ${NANOTE_SOURCES})

target_include_directories(nanote PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(nanote PRIVATE
  SDL3::SDL3
  blend2d::blend2d
)

# GUI subsystem on Windows (no console window). SDL provides WinMain and
# calls main(); this requires <SDL3/SDL_main.h> (see main.cpp).
if (WIN32)
  set_target_properties(nanote PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

# Blend2D static needs BL_STATIC define (handled by blend2d target),
# but ensure C++20 and warnings.
if (MSVC)
  target_compile_options(nanote PRIVATE /W4)
else()
  target_compile_options(nanote PRIVATE -Wall -Wextra)
endif()
