# Architecture

How the Zeman source code is organised, what it depends on, and the tools that help develop it. For building and running the apps, see [Getting started](../README.md#getting-started) in the README; for using them, see the [instruction manual](./instruction-manual.md). For how the code is written, see the [programming mindset](./programming-mindset.md) and the [C programming style guide](./c-programming-style-guide.md).

## Contents

- [Overview](#overview)
- [Directory structure](#directory-structure)
- [Headers](#headers)
- [Dependencies and their management](#dependencies-and-their-management)
- [Data storage](#data-storage)
- [Development tools: Code Digger](#development-tools-code-digger)

## Overview

This repository contains the source code for both the desktop and Android apps, which are written in C using raylib and raygui. User data is stored in an SQLite database.

The desktop app owns the data. It keeps the events, tasks and problems in its database and serves them over the local network. The Android app keeps no data of its own: it shows and edits the desktop app's calendar through the same network protocol, and the desktop app can also run as a client of another desktop app (`zeman.exe --connect`). Both apps draw the same user interface from the shared code, through a `Backend` interface that is implemented either by the local database (desktop) or by the network client (phone, or a desktop run with `--connect`).

The Android app is a native activity: all of its logic is the C library `libmain.so`. A small Java class, `MainActivity`, exists only to receive the on-screen keyboard's text (such as a-umlaut on a Finnish keyboard), which native code cannot receive on its own, and hands it to the C code.

## Directory structure

- `desktop/` The desktop app (Windows): the database, the network server, the views for problems, tasks and connected devices, and the CMake build.
- `android/` The Android app: the C code for the phone's layout, input, camera and QR scanning, the Java activity (`java/`), the manifest, and the build script `build.ps1` (with `build.bat` to start it). The build downloads its tools into `android/tools/`, and writes its result to `android/build/`.
- `shared/` Code both apps compile: the user interface, dates, events, and the network protocol and client.
- `assets/` App icons.
- `cmake/` `dependencies.cmake`, which downloads the third-party code.
- `external/` The downloaded third-party code (not committed; see below).
- `tests/` Tests of shared code that need no window or database (`str_test`, `wire_test`).
- `tools/` Small programs that help with developing the project and are not shipped with the apps; see [Code Digger](#development-tools-code-digger).
- `docs/` This documentation.

## Headers

Each of `desktop/`, `android/` and `shared/` has `src/` for its .c files and `include/zeman/` for its headers, which every file includes as `<zeman/app.h>`, `<zeman/server.h>` and so on, so that project headers are told apart from raylib's and the system's. The desktop build (`desktop/CMakeLists.txt`) and the Android build (`android/build.ps1`) each put their own `include` and `shared/include` on the compiler's include path; a new header goes in one of those folders.

## Dependencies and their management

`cmake/dependencies.cmake` downloads

- raylib 5.5,
- raygui 4.0,
- SQLite 3.51.0,
- the [Rubik](https://fonts.google.com/specimen/Rubik) font (with its SIL Open Font License),
- Nayuki's [QR code generator](https://github.com/nayuki/QR-Code-generator) 1.8.0 and the
- [quirc](https://github.com/dlbeer/quirc) 1.2 QR decoder

into `external/`, once; after that both builds are offline. Both builds run this script themselves, so there is nothing to download by hand.

The Android build also downloads its own build tools into `android/tools/`: a JDK (Eclipse Temurin 21), the Android SDK (platform 35, build tools 35.0.0) and the NDK 27.2. Nothing is installed system-wide.

## Data storage

Events are stored in `zeman.db`, an SQLite database in the repository root, whichever build directory the executable is in. CMake gives the executable that absolute path, so deleting a build directory does not delete the events; moving the repository means building again. The file is not committed. Databases from earlier versions are upgraded automatically when opened.

A `desktop/release/` build is the exception: it is built with `ZEMAN_DATABASE_NEXT_TO_EXECUTABLE`, so `zeman.db` sits next to `zeman.exe` and the whole folder can be copied elsewhere as one self-contained unit. That executable is statically linked against raylib, raygui and SQLite; its only external dependencies are Windows system DLLs and the Universal CRT, which Windows 10 and 11 already ship.

The phone saves only the address of the desktop it uses, in a small text file in the app's own data folder.

## Development tools: Code Digger

`tools/code_digger.c` is a small command-line tool that analyses the project's C code. By default it counts the calls of C standard library functions, so that you can see which ones the code leans on. It counts a name from the C11 standard library (`assert` and `va_start` included) when it is followed by an opening parenthesis; calls of raylib, SQLite and the project's own functions are not counted. Comments, string and character literals and `#include` lines are skipped, and so is a call through a struct member, such as `ops.remove(`. It reads the `.c` and `.h` files of the project, except those in `external/`, `.git`, build folders, `release/` and the downloaded `android/tools`.

It is built on request, with the same compiler and CMake as the desktop app, and is not part of the normal build:

```sh
cmake --build desktop/build/debug-win32 --target code_digger
desktop/build/debug-win32/code_digger.exe .
```

The argument is the project's folder and may be left out when running from it. Use whichever build folder you have configured instead of `debug-win32`; it must exist already, from the build steps in the README. The output has one line per function, the most called first and by name among equals, as `name (count)`, followed by how many functions were called and in how many files, for example:

```
assert (707)
snprintf (174)
strcmp (25)
...

33 standard library functions called, in 50 files.
```

### Dead code: `--dead-code`

```sh
desktop/build/debug-win32/code_digger.exe --dead-code .
```

With `--dead-code` it lists what nothing uses, under a heading for each kind with the number found: **Unused functions**, **Unused constants** (constants, constant tables and enumeration constants), **Unused macros**, **Unused variables** (those at the top level of a file) and **Unused local variables**. Each line is `file:line  name`, in file order, and a local variable also says which function it is in:

```
Unused functions (3):
  shared/include/zeman/bound.h:28  int32_clamp
  shared/include/zeman/bound.h:41  float_max
  shared/include/zeman/bound.h:51  float_clamp

6 unused names in 50 files.
```

A name counts as used when it is mentioned anywhere other than where it is defined or declared, in a body or initializer that is itself needed, so a function that is only called by another unused function is reported too, and so are two functions that only call each other. What is needed is whatever is mentioned outside every function, constant, enumeration and macro (such as in a type or a `static_assert`), `main`, and everything those use in turn. A local variable is reported when its function never mentions its name again.

The tool reads the text, with a simple parser, not the compiler's view of it, so treat the list as leads to check, not as proof:

- A name used only through a macro, or only by code the tool does not read (`external/`, a platform's own code), is reported although it is needed.
- A macro that only renames a name, such as `#define GetCharPressed android_input_char_pressed`, and a macro with no body, such as an include guard, are never reported, and what the first one names counts as used.
- Two names alike in different files, such as `view_names`, are one name to the tool, so one use hides an unused twin.
- Scopes are not told apart: a local variable mentioned again in a nested block counts as used.
- Types, structure fields and function parameters are not checked.

The compiler's `-Wunused-variable` and `-Wunused-function` already cover the code that is built; this adds constants, enumeration constants, macros, chains of unused functions, and the code that only the other platform builds.

A file that cannot be read, or is larger than 4 MB, is left out and the last line says how many. The tool exits with 1 and a message when a folder cannot be read.
