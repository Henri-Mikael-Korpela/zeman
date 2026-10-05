# Calendar

## Getting Started

### Prerequisites

- C compiler (tested with MinGW-w64 GCC 16)
- CMake 3.21 or newer (tested on version 4.4)

raylib and raygui are downloaded into `external/` on the first configure; after that the project builds offline.

### Building

To build the project with MinGW, run the following CMake commands:

```sh
cmake --preset mingw-release
cmake --build --preset mingw-release
```

The executable is written to `build/calendar.exe`. Use the `mingw-debug` preset for a debug build in `build-debug/`.

Without presets, pass the generator explicitly, otherwise CMake on Windows falls back to NMake:

```sh
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```
