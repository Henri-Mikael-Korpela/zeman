cmake_minimum_required(VERSION 3.18)
project(zeman C)
# The icon is a Windows resource, which needs the resource compiler (windres with MinGW).
if(WIN32)
    enable_language(RC)
endif()

# C11 for static_assert.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Downloads raylib, raygui, SQLite and Rubik into external/ at the repository root, once.
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/dependencies.cmake")
set(SHARED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../shared/src")
# Our own headers are included as <zeman/app.h>, from an include directory that holds a zeman
# folder, so that they cannot be mistaken for third-party or system headers of the same name.
# The shared ones are also used by the Android build (android/build.ps1).
set(SHARED_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../shared/include")

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
# Nayuki's QR code generator, for the code that phones scan to connect.
set(QRCODEGEN_DIR "${EXTERNAL_DIR}/qrcodegen/c")

add_executable(zeman
    src/main.c
    src/devices_view.c
    src/event_store.c
    src/local_backend.c
    src/problems_view.c
    src/server.c
    src/settings.c
    src/settings_view.c
    src/raygui_impl.c
    "${SHARED_DIR}/app.c"
    "${SHARED_DIR}/calendar.c"
    "${SHARED_DIR}/client.c"
    "${SHARED_DIR}/discovery.c"
    "${SHARED_DIR}/event.c"
    "${SHARED_DIR}/net.c"
    "${SHARED_DIR}/protocol.c"
    "${SHARED_DIR}/text_box.c"
    "${QRCODEGEN_DIR}/qrcodegen.c"
)
target_include_directories(zeman PRIVATE include "${SHARED_INCLUDE_DIR}" "${QRCODEGEN_DIR}")
# raylib builds GLFW into itself; main.c calls glfwWaitEventsTimeout from it, which raylib does
# not wrap, to sleep while idle yet wake at once on input (see desktop_wait in main.c).
target_include_directories(zeman SYSTEM PRIVATE "${EXTERNAL_DIR}/raylib/src/external/glfw/include")

# Off (the default, for the mingw-debug and mingw-release presets): the database lives in the
# repository root, not beside the executable, so that deleting a build directory (which is
# routine) never deletes the events. On (the mingw-package preset): the database goes beside the
# executable instead, at run time (see database_path in main.c), so that the release folder
# install() builds below is self-contained and works wherever it is copied to.
option(ZEMAN_DATABASE_NEXT_TO_EXECUTABLE
       "Put the database beside the executable, for a release folder meant to be copied elsewhere"
       OFF)
if(ZEMAN_DATABASE_NEXT_TO_EXECUTABLE)
    target_compile_definitions(zeman PRIVATE ZEMAN_DATABASE_NEXT_TO_EXECUTABLE)
else()
    # An absolute path, so that every build directory, wherever it is, uses the same file.
    set(DATABASE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../zeman.db")
    cmake_path(NORMAL_PATH DATABASE_FILE)
    target_compile_definitions(zeman PRIVATE "ZEMAN_DATABASE_PATH=\"${DATABASE_FILE}\"")
endif()

# The application icon, shared with the Android app's launcher icon (assets/zeman-app.png).
if(WIN32)
    set(APP_ICON_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../assets/zeman-app.ico")
    cmake_path(NORMAL_PATH APP_ICON_FILE)
    configure_file(app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/app.rc" @ONLY)
    target_sources(zeman PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
    # Rebuild the resource when the icon changes, which CMake does not see inside the .rc file.
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/app.rc"
                                PROPERTIES OBJECT_DEPENDS "${APP_ICON_FILE}")
endif()
target_link_libraries(zeman PRIVATE raylib raygui sqlite3)
if(WIN32)
    target_link_libraries(zeman PRIVATE ws2_32)
endif()

# The app loads its fonts from a fonts folder beside the executable, so that it runs offline.
add_custom_command(TARGET zeman POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${FONTS_DIR}"
            "$<TARGET_FILE_DIR:zeman>/fonts"
    COMMENT "Copying fonts next to the executable")

# Third-party implementation: don't report its warnings as ours.
if(NOT MSVC)
    set_source_files_properties(src/raygui_impl.c "${QRCODEGEN_DIR}/qrcodegen.c"
                                PROPERTIES COMPILE_OPTIONS "-w")
endif()

# Assertions stay on in every build type, as in TIGER_STYLE: they catch bugs in use, not only
# in development. CMake's Release flags define NDEBUG; undefine it for our own code only.
if(MSVC)
    target_compile_options(zeman PRIVATE /W4 /WX /UNDEBUG)
    # Use main() as the entry point without opening a console window in Release.
    set_target_properties(zeman PROPERTIES
        LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
else()
    target_compile_options(zeman PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    # Debug keeps the console, since raylib's own TraceLog messages (and a crash's stderr) print
    # there; a Release build hides it, as a desktop app's window is the only thing meant to show.
    set_target_properties(zeman PROPERTIES LINK_FLAGS_RELEASE "-mwindows")
endif()

# Lets `cmake --install` (see the mingw-package preset and README.md) collect everything a
# release folder needs into one place: the executable and the fonts it loads at run time. Not
# the database, which local_backend_open() creates next to the executable on first run.
install(TARGETS zeman RUNTIME DESTINATION .)
install(DIRECTORY "${FONTS_DIR}/" DESTINATION fonts)
