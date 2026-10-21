# Nanote

A fast and lightweight modal, keyboard-driven 2D sketching app.

This is not meant to be a replacement for professional digital painting software. We want to give you a fast and free digital alternative to your pocket notebook.

## Features

- UI designed for fast hotkey adoption.
- Modal commands inspired by Vim/Emacs.
- Multi-layer support, where each layer can be a vector or a raster layer.
- Infinite and bounded canvas documents.
- Slideshow support.
- Cross-platform (Windows/macOS/Linux releases)
- Lightweight and fast: no long startup times, just a canvas to start drawing right away.

## Requirements

- CMake ≥ 3.24, a C++20 compiler (GCC, Clang, or MSVC), `git`
- SDL3 ≥ 3.2 (system package, or built from source as CI does)
- Blend2D + AsmJit are fetched and built automatically by CMake (requires network access on first configure)
- System fonts at runtime for UI text (UI text is disabled if none are found)

## Building

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/nanote
```

Continuous integration builds on Linux, Windows, and macOS (see `.github/workflows/build.yml`).
