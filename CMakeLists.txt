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

# Rubik from Google Fonts, regular (400) and medium (500). The CSS API answers a non-browser
# user agent with static TrueType files, one per weight, instead of the WOFF2 and variable font
# that browsers get and raylib cannot load. The font file URLs change between font versions,
# so they are read from the CSS instead of being written here.
function(vendor_google_font family weights_css dest)
    set(css_url "https://fonts.googleapis.com/css2?family=${family}:wght@${weights_css}")
    set(css_file "${CMAKE_CURRENT_BINARY_DIR}/${family}.css")
    file(DOWNLOAD "${css_url}" "${css_file}" STATUS status HTTPHEADER "User-Agent: Mozilla/4.0")
    list(GET status 0 code)
    if(NOT code EQUAL 0)
        list(GET status 1 msg)
        message(FATAL_ERROR "Failed to download the ${family} CSS: ${msg}")
    endif()
    file(READ "${css_file}" css)
    file(MAKE_DIRECTORY "${dest}")
    # ARGN pairs each weight with the file name to save it as.
    set(pairs ${ARGN})
    list(LENGTH pairs pair_length)
    math(EXPR last "${pair_length} - 1")
    foreach(i RANGE 0 ${last} 2)
        math(EXPR j "${i} + 1")
        list(GET pairs ${i} weight)
        list(GET pairs ${j} file_name)
        string(REGEX MATCH "font-weight: ${weight};[^}]*url\\(([^)]+\\.ttf)\\)" match "${css}")
        if(NOT match)
            message(FATAL_ERROR "No TrueType file for ${family} weight ${weight} in ${css_url}")
        endif()
        message(STATUS "Downloading ${family} ${weight} into ${dest}/${file_name}")
        file(DOWNLOAD "${CMAKE_MATCH_1}" "${dest}/${file_name}" STATUS status)
        list(GET status 0 code)
        if(NOT code EQUAL 0)
            list(GET status 1 msg)
            message(FATAL_ERROR "Failed to download ${family} ${weight}: ${msg}")
        endif()
    endforeach()
endfunction()

set(FONTS_DIR "${EXTERNAL_DIR}/fonts")
if(NOT EXISTS "${FONTS_DIR}/Rubik-Regular.ttf" OR NOT EXISTS "${FONTS_DIR}/Rubik-Medium.ttf")
    vendor_google_font(Rubik "400;500" "${FONTS_DIR}"
        400 Rubik-Regular.ttf
        500 Rubik-Medium.ttf)
    # The SIL Open Font License asks for its text to travel with the font.
    file(DOWNLOAD "https://raw.githubusercontent.com/google/fonts/main/ofl/rubik/OFL.txt"
        "${FONTS_DIR}/OFL.txt")
endif()

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
