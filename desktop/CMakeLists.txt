cmake_minimum_required(VERSION 3.18)
project(calendar C)

# C11 for static_assert.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Downloads raylib, raygui, SQLite and Rubik into external/ at the repository root, once.
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/dependencies.cmake")
set(SHARED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../shared/src")

# raylib: build as a static library, skip examples/games.
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory("${EXTERNAL_DIR}/raylib" "${CMAKE_CURRENT_BINARY_DIR}/raylib" EXCLUDE_FROM_ALL)

# raygui is header-only; its implementation is compiled in src/raygui_impl.c.
add_library(raygui INTERFACE)
target_include_directories(raygui INTERFACE "${EXTERNAL_DIR}/raygui/src")

# SQLite amalgamation, compiled as a static library. The app is single-threaded and loads no
# extensions, so both are compiled out; this also avoids linking pthread and dl on Linux.
add_library(sqlite3 STATIC "${EXTERNAL_DIR}/sqlite/sqlite3.c")
target_include_directories(sqlite3 PUBLIC "${EXTERNAL_DIR}/sqlite")
target_compile_definitions(sqlite3 PRIVATE
    SQLITE_THREADSAFE=0
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_DEFAULT_MEMSTATUS=0
    SQLITE_DQS=0)
if(NOT MSVC)
    target_compile_options(sqlite3 PRIVATE -w)
endif()

# ---------------------------------------------------------------------------
# Application: the shared user interface and network code, plus the desktop's database,
# server and entry point.
# ---------------------------------------------------------------------------
add_executable(calendar
    src/main.c
    src/event_store.c
    src/local_backend.c
    src/server.c
    src/raygui_impl.c
    "${SHARED_DIR}/app.c"
    "${SHARED_DIR}/calendar.c"
    "${SHARED_DIR}/client.c"
    "${SHARED_DIR}/discovery.c"
    "${SHARED_DIR}/event.c"
    "${SHARED_DIR}/net.c"
    "${SHARED_DIR}/protocol.c"
)
target_include_directories(calendar PRIVATE src "${SHARED_DIR}")
target_link_libraries(calendar PRIVATE raylib raygui sqlite3)
if(WIN32)
    target_link_libraries(calendar PRIVATE ws2_32)
endif()

# The app loads its fonts from a fonts folder beside the executable, so that it runs offline.
add_custom_command(TARGET calendar POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${FONTS_DIR}"
            "$<TARGET_FILE_DIR:calendar>/fonts"
    COMMENT "Copying fonts next to the executable")

# Third-party implementation: don't report its warnings as ours.
if(NOT MSVC)
    set_source_files_properties(src/raygui_impl.c PROPERTIES COMPILE_OPTIONS "-w")
endif()

# Assertions stay on in every build type, as in TIGER_STYLE: they catch bugs in use, not only
# in development. CMake's Release flags define NDEBUG; undefine it for our own code only.
if(MSVC)
    target_compile_options(calendar PRIVATE /W4 /WX /UNDEBUG)
    # Use main() as the entry point without opening a console window in Release.
    set_target_properties(calendar PROPERTIES
        LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
else()
    target_compile_options(calendar PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
