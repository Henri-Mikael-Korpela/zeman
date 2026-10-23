#include <zeman/settings.h>

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zeman/str.h>

enum {
    // settings.ini holds one short line per setting, so a file near this size is not ours, and
    // the whole file fits in one buffer on the stack.
    SETTINGS_FILE_SIZE_MAX = 8192,
    // Every line takes at least its terminator, so the file cannot hold more lines than this.
    SETTINGS_LINE_COUNT_MAX = SETTINGS_FILE_SIZE_MAX + 1,
    // Room for the executable's folder, as long as any path the user gives, and the file name.
    SETTINGS_FILE_PATH_SIZE = SETTINGS_PATH_SIZE + 32,
};

static const char FILE_NAME[] = "settings.ini";
static const char KEY_DATABASE_PATH[] = "database_path";

static bool settings_file_path(char *buffer, size_t buffer_size, const char *directory);
static bool settings_parse(Settings *settings, Str text);
static bool settings_parse_line(Settings *settings, Str line);
static bool settings_line_is_skipped(Str line);

bool settings_load(Settings *settings, const char *directory) {
    assert(settings != NULL);
    assert(directory != NULL);
    *settings = (Settings){ 0 };
    char path[SETTINGS_FILE_PATH_SIZE] = "";
    if (!settings_file_path(path, sizeof(path), directory)) {
        snprintf(settings->error, sizeof(settings->error), "The app's folder path is too long.");
        return false;
    }
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        if (errno == ENOENT) {
            return true; // Nothing saved yet: every setting has its default.
        }
        snprintf(settings->error, sizeof(settings->error), "Cannot open %s: %s.", FILE_NAME,
                 strerror(errno));
        return false;
    }
    char text[SETTINGS_FILE_SIZE_MAX] = "";
    const size_t size = fread(text, 1, SETTINGS_FILE_SIZE_MAX, file);
    const bool failed = ferror(file) != 0;
    // A full buffer may hide more: one byte more tells.
    const bool too_large = !failed && size == SETTINGS_FILE_SIZE_MAX && fgetc(file) != EOF;
    // Closing a file that was only read loses nothing, so its result does not matter.
    (void)fclose(file);
    if (failed) {
        snprintf(settings->error, sizeof(settings->error), "Cannot read %s.", FILE_NAME);
        return false;
    }
    if (too_large) {
        snprintf(settings->error, sizeof(settings->error), "%s is larger than %d bytes.",
                 FILE_NAME, SETTINGS_FILE_SIZE_MAX);
        return false;
    }
    assert(size <= SETTINGS_FILE_SIZE_MAX);
    return settings_parse(settings, str_make(text, size));
}

bool settings_save(Settings *settings, const char *directory) {
    assert(settings != NULL);
    assert(directory != NULL);
    // A line break would end the value early and make the rest read as another line. The path
    // comes from a one-line text box, whose pasting turns line breaks into spaces.
    assert(strchr(settings->database_path, '\n') == NULL);
    assert(strchr(settings->database_path, '\r') == NULL);
    settings->error[0] = '\0';
    char path[SETTINGS_FILE_PATH_SIZE] = "";
    if (!settings_file_path(path, sizeof(path), directory)) {
        snprintf(settings->error, sizeof(settings->error), "The app's folder path is too long.");
        return false;
    }
    // Written in place rather than to a temporary file renamed over it: Windows cannot rename
    // over an existing file in one step either, and the file is a few bytes in one write.
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        snprintf(settings->error, sizeof(settings->error), "Cannot write %s: %s.", FILE_NAME,
                 strerror(errno));
        return false;
    }
    // Windows line breaks, so that any text editor shows the file as lines.
    const int written = fprintf(file, "; Zeman desktop settings, written by the app.\r\n"
                                      "%s=%s\r\n",
                                KEY_DATABASE_PATH, settings->database_path);
    // Closing flushes the write, so its failure is a failed write too.
    const bool closed = fclose(file) == 0;
    if (written < 0 || !closed) {
        snprintf(settings->error, sizeof(settings->error), "Cannot write %s: %s.", FILE_NAME,
                 strerror(errno));
        return false;
    }
    return true;
}

static bool settings_file_path(char *buffer, size_t buffer_size, const char *directory) {
    assert(buffer != NULL);
    assert(buffer_size > 0);
    const int written = snprintf(buffer, buffer_size, "%s%s", directory, FILE_NAME);
    return written > 0 && (size_t)written < buffer_size;
}

static bool settings_parse(Settings *settings, Str text) {
    // Notepad may start a file it saves with a UTF-8 byte order mark, which is no part of the
    // first key.
    const Str byte_order_mark = STR("\xEF\xBB\xBF");
    if (str_starts_with(text, byte_order_mark)) {
        text = str_slice(text, byte_order_mark.length, text.length);
    }
    Str rest = text;
    for (int32_t i = 0; i < SETTINGS_LINE_COUNT_MAX; i++) {
        Str line = rest;
        const bool has_more = str_split_once(rest, STR("\n"), &line, &rest);
        if (!settings_parse_line(settings, line)) {
            return false;
        }
        if (!has_more) {
            return true;
        }
    }
    assert(false); // Each line takes at least one byte, so the text ends within the bound.
    return true;
}

/**
 * Reads one "key=value" line. Keys this version does not know are skipped, so that settings
 * written by a later version still let this one start.
 */
static bool settings_parse_line(Settings *settings, Str line) {
    // Spaces, tabs and the carriage return of a Windows line break are not part of any text.
    const Str blanks = STR(" \t\r");
    const Str trimmed = str_trim_any(line, blanks);
    if (settings_line_is_skipped(trimmed)) {
        return true;
    }
    Str key = trimmed;
    Str value = trimmed;
    if (!str_split_once(trimmed, STR("="), &key, &value)) {
        // At most 60 bytes of the line, so that a long one cannot fill the message.
        int32_t shown_length = 60;
        if (trimmed.length < 60) {
            shown_length = (int32_t)trimmed.length;
        }
        snprintf(settings->error, sizeof(settings->error),
                 "%s has a line without \"=\": %.*s", FILE_NAME, shown_length, trimmed.data);
        return false;
    }
    key = str_trim_any(key, blanks);
    value = str_trim_any(value, blanks);
    if (!str_eq(key, str_from_cstr(KEY_DATABASE_PATH))) {
        return true;
    }
    if (value.length >= sizeof(settings->database_path)) {
        snprintf(settings->error, sizeof(settings->error), "%s: %s is too long.", FILE_NAME,
                 KEY_DATABASE_PATH);
        return false;
    }
    // Fits, as just checked, so nothing is cut.
    const bool copied = str_to_cstr(value, settings->database_path,
                                    sizeof(settings->database_path));
    assert(copied);
    (void)copied; // Unused when assertions are compiled out
    return true;
}

/**
 * Blank lines, comments and [section] headers, which INI files may have, hold no setting.
 */
static bool settings_line_is_skipped(Str line) {
    if (line.length == 0) {
        return true;
    }
    // Any other character starts a key.
    return str_find_char(STR(";#["), line.data[0]) != STR_NOT_FOUND;
}
