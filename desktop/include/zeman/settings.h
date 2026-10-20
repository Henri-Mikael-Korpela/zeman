#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdbool.h>
#include <stdint.h>

#include <zeman/text_box.h>

enum {
    // A path the user types or pastes, in UTF-8; the Settings view edits it in a text box.
    SETTINGS_PATH_SIZE = TEXT_BOX_SIZE_MAX,
    // Room for a sentence with a file name in it.
    SETTINGS_ERROR_SIZE = 192,
};

// The desktop app's settings, kept in settings.ini beside the executable, so that they travel
// with the folder the app is copied in, as the release folder's database does.
typedef struct Settings {
    char database_path[SETTINGS_PATH_SIZE];  // Empty for the build's default database
    char error[SETTINGS_ERROR_SIZE];         // Why the last load or save failed
} Settings;

// Reads settings.ini from directory, which ends with a path separator. A missing file is not an
// error: every setting then has its default. Returns false, with error set, when the file
// exists but cannot be read or holds a setting that does not fit.
bool settings_load(Settings *settings, const char *directory);
// Writes every setting to settings.ini in directory. Returns false, with error set, on failure.
bool settings_save(Settings *settings, const char *directory);

#endif
