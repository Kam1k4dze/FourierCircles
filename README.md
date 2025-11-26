# Fourier Circles

> Epicycle visualization that draws arbitrary SVG paths using Fourier series.

## Overview
This project converts any SVG path into rotating circles (epicycles) that reconstruct the shape. Runs natively and in browser via WebAssembly.

## Requirements

- C++23 compiler
- CMake 3.25+
- Emscripten 4.0+ (web build only)

## Build
**Native:**
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel $(nproc)
./build/FourierCircles
```

**WebAssembly:**
```bash
emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release
cmake --build build-web --parallel $(nproc)
# Open build-web/FourierCircles.html
```