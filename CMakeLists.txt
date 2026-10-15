cmake_minimum_required(VERSION 3.18)
project(calendar C)

# C11 for static_assert.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Dependencies: kept local to the project under external/.
# If a dependency folder is missing it is downloaded once and extracted there,
# after which builds work fully offline.
# ---------------------------------------------------------------------------
set(RAYLIB_VERSION 5.5)
set(RAYGUI_VERSION 4.0)
set(SQLITE_VERSION 3510000) # 3.51.0, in the amalgamation's numeric form.
set(EXTERNAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external")

function(vendor_dependency name url archive_root dest)
    if(EXISTS "${dest}")
        return()
    endif()
    message(STATUS "Downloading ${name} into ${dest}")
    get_filename_component(archive_name "${url}" NAME)
    set(archive "${CMAKE_CURRENT_BINARY_DIR}/${archive_name}")
    file(DOWNLOAD "${url}" "${archive}" STATUS status TLS_VERIFY ON)
    list(GET status 0 code)
    if(NOT code EQUAL 0)
        list(GET status 1 msg)
        message(FATAL_ERROR "Failed to download ${name}: ${msg}")
    endif()
    set(tmp "${CMAKE_CURRENT_BINARY_DIR}/${name}-extract")
    file(REMOVE_RECURSE "${tmp}")
    file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${tmp}")
    file(MAKE_DIRECTORY "${EXTERNAL_DIR}")
    file(RENAME "${tmp}/${archive_root}" "${dest}")
    file(REMOVE_RECURSE "${tmp}")
    file(REMOVE "${archive}")
endfunction()

vendor_dependency(raylib
    "https://github.com/raysan5/raylib/archive/refs/tags/${RAYLIB_VERSION}.tar.gz"
    "raylib-${RAYLIB_VERSION}"
    "${EXTERNAL_DIR}/raylib")

vendor_dependency(raygui
    "https://github.com/raysan5/raygui/archive/refs/tags/${RAYGUI_VERSION}.tar.gz"
    "raygui-${RAYGUI_VERSION}"
    "${EXTERNAL_DIR}/raygui")

vendor_dependency(sqlite
    "https://www.sqlite.org/2025/sqlite-amalgamation-${SQLITE_VERSION}.zip"
    "sqlite-amalgamation-${SQLITE_VERSION}"
    "${EXTERNAL_DIR}/sqlite")

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
# Application
# ---------------------------------------------------------------------------
add_executable(calendar
    src/main.c
    src/calendar.c
    src/event.c
    src/event_store.c
    src/raygui_impl.c
)
target_link_libraries(calendar PRIVATE raylib raygui sqlite3)

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
