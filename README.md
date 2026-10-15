# Calendar

## Getting Started

### Prerequisites

- C compiler (tested with MinGW-w64 GCC 16)
- CMake 3.21 or newer (tested on version 4.4)

raylib 5.5, raygui 4.0 and SQLite 3.51.0 are downloaded into `external/` on the first configure; after that the project builds offline.

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

## Usage

Select a day to see its events in the panel on the right, and use **New event** to add one. Start and end dates are `YYYY-MM-DD`; times are `HH:MM` and optional (leave both empty for an all-day event).

Each event records when it was created:

- A colored dot in a day cell marks the day an event was created.
- Selecting an event tints the days from its creation to its start, and its card shows how long that lead time was.

Keys: arrows move by a day or a week, Page Up/Page Down change month, Home jumps to today, Tab moves between form fields, and Escape cancels the form.

Events are stored in `calendar.db`, an SQLite database next to the executable (for example `build/calendar.db`). Deleting the build directory deletes the events.
