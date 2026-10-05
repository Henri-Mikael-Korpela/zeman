cmake_minimum_required(VERSION 3.18)
project(calendar C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Dependencies: kept local to the project under external/.
# If a dependency folder is missing it is downloaded once and extracted there,
# after which builds work fully offline.
# ---------------------------------------------------------------------------
set(RAYLIB_VERSION 5.5)
set(RAYGUI_VERSION 4.0)
set(EXTERNAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external")

function(vendor_dependency name url archive_root dest)
    if(EXISTS "${dest}")
        return()
    endif()
    message(STATUS "Downloading ${name} into ${dest}")
    set(archive "${CMAKE_CURRENT_BINARY_DIR}/${name}.tar.gz")
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

# raylib: build as a static library, skip examples/games.
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory("${EXTERNAL_DIR}/raylib" "${CMAKE_CURRENT_BINARY_DIR}/raylib" EXCLUDE_FROM_ALL)

# raygui is header-only; its implementation is compiled in src/raygui_impl.c.
add_library(raygui INTERFACE)
target_include_directories(raygui INTERFACE "${EXTERNAL_DIR}/raygui/src")

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
add_executable(calendar
    src/main.c
    src/calendar.c
    src/raygui_impl.c
)
target_link_libraries(calendar PRIVATE raylib raygui)

# Third-party implementation: don't report its warnings as ours.
if(NOT MSVC)
    set_source_files_properties(src/raygui_impl.c PROPERTIES COMPILE_OPTIONS "-w")
endif()

if(MSVC)
    target_compile_options(calendar PRIVATE /W4)
    # Use main() as the entry point without opening a console window in Release.
    set_target_properties(calendar PROPERTIES
        LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
else()
    target_compile_options(calendar PRIVATE -Wall -Wextra)
endif()
