# Calendar

## Getting Started

### Prerequisites

- C compiler (tested with MinGW-w64 GCC 16)
- CMake 3.21 or newer (tested on version 4.4)

raylib 5.5, raygui 4.0, SQLite 3.51.0 and the [Rubik](https://fonts.google.com/specimen/Rubik) font (regular and medium, from Google Fonts, with its SIL Open Font License) are downloaded into `external/` on the first configure; after that the project builds offline. The build copies the fonts into a `fonts` folder next to the executable, which the app needs at run time; without it, the app falls back to raylib's default font.

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

Select a day to see its events and tasks in the panel on the right, and use **New event** or **New task** to add one. A task works like an event (dates, optional times, repeats, editing and deleting) and can also be checked off: tick its checkbox on its card. For a repeating task, each occurrence is checked off separately. In the calendar, tasks are outlined bars with a checkbox; done tasks are faded and struck through. The Event/Task toggle in the form switches an item between the two. Start and end dates are `YYYY-MM-DD`; times are `HH:MM` and optional (leave both empty for an all-day event).

An event can repeat every week, every X days, every month or every year. Monthly and yearly events keep the same date, so an event on the 31st skips months without one, and an event on 29 February appears only in leap years. Each repetition must end before the next one starts. A repeating event can have an optional **Repeat until** date: it is inclusive, so an occurrence that starts on that date still happens, in full, and none start after it. Leave it empty to repeat forever.

To edit an event, click its card: the form opens with its details, and **Save** updates it. A repeating event is edited as a whole, so the form shows the dates of its first occurrence. Editing keeps the original creation time.

To delete an event, press **Delete** on its card and confirm; deleting a repeating event removes all of its repeats. Escape cancels the confirmation.

Each event records when it was created:

- A colored dot in a day cell marks the day an event was created.
- Selecting an event tints the days from its creation to its start (for a repeating event, to the occurrence on the selected day), and its card shows how long that lead time was.

Keys: arrows move by a day or a week, Page Up/Page Down change month, Home jumps to today, Tab moves between form fields, and Escape cancels the form.

Events are stored in `calendar.db`, an SQLite database next to the executable (for example `build/calendar.db`). Deleting the build directory deletes the events. Databases from earlier versions are upgraded automatically when opened.
