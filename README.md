# Calendar

## Project structure

- `desktop/` - the desktop app (Windows). It keeps the calendar in an SQLite database and serves it to phones on the same local network.
- `android/` - the Android app. It shows and edits the desktop app's calendar over the network; it keeps no data of its own.
- `shared/` - code both apps compile: the user interface, dates, events, and the network protocol and client.
- `cmake/dependencies.cmake` - downloads raylib 5.5, raygui 4.0, SQLite 3.51.0 and the [Rubik](https://fonts.google.com/specimen/Rubik) font (with its SIL Open Font License) into `external/`, once; after that both builds are offline.

## Desktop app

### Prerequisites

- C compiler (tested with MinGW-w64 GCC 16)
- CMake 3.21 or newer (tested on version 4.4)

### Building

```sh
cd desktop
cmake --preset mingw-release
cmake --build --preset mingw-release
```

The executable is written to `desktop/build/calendar.exe`, with the fonts it needs in `desktop/build/fonts`. Use the `mingw-debug` preset for a debug build in `desktop/build-debug/`. Without presets, pass the generator explicitly, otherwise CMake on Windows falls back to NMake:

```sh
cmake -S desktop -B desktop/build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build desktop/build
```

`calendar.exe --connect ADDRESS[:PORT]` runs the desktop app as a client of another desktop app, exactly as the phone does; it is a way to try the network side on one computer.

## Android app

### Building

From the `android` folder, on Windows:

```bat
build.bat
```

The first run downloads a JDK, the Android SDK and the NDK into `android/tools` (about 1.5 GB) and asks you to accept the Android SDK licenses (`build.bat -AcceptLicenses` accepts them without asking). Nothing is installed system-wide; CMake, which the desktop build needs anyway, is the only outside tool. Later builds take seconds.

The result is `android/build/calendar.apk`, for 64-bit ARM phones running Android 7.0 or newer. Copy it to the phone and open it there; Android asks to allow installing apps from that source. The APK is signed with a key made on the first build and kept in `android/keystore/` (not committed); keep it, because Android installs an update only when it is signed with the same key.

### Connecting

The phone and the desktop talk directly, within the same local network; nothing goes through the internet.

1. Start the desktop app. Its status bar shows "Phone access:" and its address on the network. The first time, Windows asks whether to allow it through the firewall: allow it on private networks.
2. Open the app on the phone, on the same Wi-Fi network. It looks for the desktop app and connects by itself; if it cannot find it, type the address from the desktop's status bar.

The phone remembers the desktop and connects to it at the next start; the **Server** button in its status bar goes back to the connection screen. Changes made on either side appear on the other within about a second.

The desktop app accepts connections only from local network addresses (10.x.x.x, 172.16-31.x.x, 192.168.x.x and 169.254.x.x) and has no password, so anyone on the same local network can use it while it runs.

On the phone, swipe sideways over the calendar to change month; Back closes the form. Typing on the phone supports letters, digits and common punctuation, not letters such as a-umlaut.

## Usage

Select a day to see its events and tasks in the panel on the right, and use **New event** or **New task** to add one. A task works like an event (dates, optional times, repeats, editing and deleting) and can also be checked off: tick its checkbox on its card. For a repeating task, each occurrence is checked off separately. In the calendar, tasks are outlined bars with a checkbox; done tasks are faded and struck through. The Event/Task toggle in the form switches an item between the two. Start and end dates are `YYYY-MM-DD`; times are `HH:MM` and optional (leave both empty for an all-day event).

An event can repeat every week, every X days, every month or every year. Monthly and yearly events keep the same date, so an event on the 31st skips months without one, and an event on 29 February appears only in leap years. Each repetition must end before the next one starts. A repeating event can have an optional **Repeat until** date: it is inclusive, so an occurrence that starts on that date still happens, in full, and none start after it. Leave it empty to repeat forever.

An event or task can have a location: fill in its address, its city, or both. Locations are shared, so every item saved with the same address and city links to the same location, and the card shows it below the dates.

Events and tasks also have an optional one-line description (up to 199 characters). Cards show it under the dates and location, wrapped over up to three lines.

Use **New problem** to record a problem: something that went wrong, with a name and an optional description. A problem is always recorded on the day it is created, whichever day is selected, and keeps that day when edited; it has no times, location or repeat. Problems show in dark red, marked with "!", and an event or task cannot be turned into a problem or back.

To edit an event, click its card: the form opens with its details, and **Save** updates it. A repeating event is edited as a whole, so the form shows the dates of its first occurrence. Editing keeps the original creation time.

To delete an event, press **Delete** on its card and confirm; deleting a repeating event removes all of its repeats. Escape cancels the confirmation.

Each event records when it was created:

- A colored dot in a day cell marks the day an event was created.
- The dot of the selected event or task is ringed, and its card shows how long before its start it was created (for a repeating event, before the occurrence on the selected day).

Keys: arrows move by a day or a week, Page Up/Page Down change month, Home jumps to today, Tab moves between form fields, and Escape cancels the form.

Events are stored in `calendar.db`, an SQLite database next to the desktop executable (`desktop/build/calendar.db`). Deleting the build directory deletes the events. Databases from earlier versions are upgraded automatically when opened.
