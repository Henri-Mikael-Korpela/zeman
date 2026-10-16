# Third-party dependencies, kept local to the project under external/ at the repository root.
# A missing dependency is downloaded once and extracted there; after that, builds are offline.
#
# Shared by the desktop and Android builds. Use it with include() from a CMakeLists.txt, or run
# it on its own as a script:  cmake -P cmake/dependencies.cmake
cmake_minimum_required(VERSION 3.18)

set(RAYLIB_VERSION 5.5)
set(RAYGUI_VERSION 4.0)
set(SQLITE_VERSION 3510000) # 3.51.0, in the amalgamation's numeric form.
# QR codes: Nayuki's generator draws the desktop's code; quirc reads it from the phone's camera.
set(QRCODEGEN_VERSION 1.8.0)
set(QUIRC_VERSION 1.2)
get_filename_component(EXTERNAL_DIR "${CMAKE_CURRENT_LIST_DIR}/../external" ABSOLUTE)
set(FONTS_DIR "${EXTERNAL_DIR}/fonts")
# Archives are downloaded here and removed after extraction. A fixed folder rather than the
# build folder, because script mode has no build folder.
set(DEPENDENCY_DOWNLOAD_DIR "${EXTERNAL_DIR}/.download")

function(vendor_dependency name url archive_root dest)
    if(EXISTS "${dest}")
        return()
    endif()
    message(STATUS "Downloading ${name} into ${dest}")
    get_filename_component(archive_name "${url}" NAME)
    set(archive "${DEPENDENCY_DOWNLOAD_DIR}/${archive_name}")
    file(DOWNLOAD "${url}" "${archive}" STATUS status TLS_VERIFY ON)
    list(GET status 0 code)
    if(NOT code EQUAL 0)
        list(GET status 1 msg)
        message(FATAL_ERROR "Failed to download ${name}: ${msg}")
    endif()
    set(tmp "${DEPENDENCY_DOWNLOAD_DIR}/${name}-extract")
    file(REMOVE_RECURSE "${tmp}")
    file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${tmp}")
    file(MAKE_DIRECTORY "${EXTERNAL_DIR}")
    file(RENAME "${tmp}/${archive_root}" "${dest}")
    file(REMOVE_RECURSE "${tmp}")
    file(REMOVE "${archive}")
endfunction()

# Rubik from Google Fonts. The CSS API answers a non-browser user agent with static TrueType
# files, one per weight, instead of the WOFF2 and variable font that browsers get and raylib
# cannot load. The font file URLs change between font versions, so they are read from the CSS.
function(vendor_google_font family weights_css dest)
    set(css_url "https://fonts.googleapis.com/css2?family=${family}:wght@${weights_css}")
    set(css_file "${DEPENDENCY_DOWNLOAD_DIR}/${family}.css")
    file(DOWNLOAD "${css_url}" "${css_file}" STATUS status HTTPHEADER "User-Agent: Mozilla/4.0")
    list(GET status 0 code)
    if(NOT code EQUAL 0)
        list(GET status 1 msg)
        message(FATAL_ERROR "Failed to download the ${family} CSS: ${msg}")
    endif()
    file(READ "${css_file}" css)
    file(REMOVE "${css_file}")
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

vendor_dependency(qrcodegen
    "https://github.com/nayuki/QR-Code-generator/archive/refs/tags/v${QRCODEGEN_VERSION}.tar.gz"
    "QR-Code-generator-${QRCODEGEN_VERSION}"
    "${EXTERNAL_DIR}/qrcodegen")

vendor_dependency(quirc
    "https://github.com/dlbeer/quirc/archive/refs/tags/v${QUIRC_VERSION}.tar.gz"
    "quirc-${QUIRC_VERSION}"
    "${EXTERNAL_DIR}/quirc")

if(NOT EXISTS "${FONTS_DIR}/Rubik-Regular.ttf" OR NOT EXISTS "${FONTS_DIR}/Rubik-Medium.ttf")
    vendor_google_font(Rubik "400;500" "${FONTS_DIR}"
        400 Rubik-Regular.ttf
        500 Rubik-Medium.ttf)
    # The SIL Open Font License asks for its text to travel with the font.
    file(DOWNLOAD "https://raw.githubusercontent.com/google/fonts/main/ofl/rubik/OFL.txt"
        "${FONTS_DIR}/OFL.txt")
endif()
