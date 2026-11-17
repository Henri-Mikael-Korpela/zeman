#include <zeman/file_access_system.h>

#include <assert.h>
#include <stdio.h>

// Includes the Windows headers but not raylib's, which define Rectangle, CloseWindow and
// ShowCursor too, so that the two cannot share a file.
#include <windows.h>

enum {
    // Characters of a path as Windows takes it: UTF-16, so never more than the bytes of the
    // UTF-8 path, which is what FILE_PATH_SIZE counts.
    WIDE_PATH_SIZE = FILE_PATH_SIZE,
};

static bool file_access_choose(char *path, size_t path_size);
static bool file_access_save_as(Str name, char *path, size_t path_size);
static File_Read_Status file_access_read(char const *path, Array_Slice buffer, int32_t *size);
static bool file_access_write(char const *path, Array_Slice bytes);
static bool utf8_to_wide(char const *text, wchar_t *wide, int32_t wide_size);
static bool wide_to_utf8(wchar_t const *wide, char *text, size_t text_size);

File_Access file_access_system(void) {
    return (File_Access){
        .choose = file_access_choose,
        .save_as = file_access_save_as,
        .read = file_access_read,
        .write = file_access_write,
    };
}

static bool file_access_choose(char *path, size_t path_size) {
    assert(path != NULL);
    assert(path_size > 0);

    wchar_t wide[WIDE_PATH_SIZE] = L"";
    OPENFILENAMEW dialog = { 0 };
    dialog.lStructSize = sizeof(dialog);
    // The window that has the focus, which the dialog then belongs to: it stays over the app and
    // the app waits for it.
    dialog.hwndOwner = GetActiveWindow();
    dialog.lpstrFile = wide;
    dialog.nMaxFile = WIDE_PATH_SIZE;
    dialog.lpstrFilter = L"All files\0*.*\0";
    // The folder is not changed: the app finds its fonts by a path that is relative to it.
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY |
                   OFN_EXPLORER;
    if (!GetOpenFileNameW(&dialog)) {
        return false; // Cancelled, or the dialog failed; either way there is no file to add.
    }
    return wide_to_utf8(wide, path, path_size);
}

static bool file_access_save_as(Str name, char *path, size_t path_size) {
    assert(path != NULL);
    assert(path_size > 0);

    wchar_t wide[WIDE_PATH_SIZE] = L"";
    char name_text[FILE_PATH_SIZE] = "";
    // A name too long for the dialog is left out, and the user types one.
    if (str_to_cstr(name, name_text, sizeof(name_text))) {
        if (!utf8_to_wide(name_text, wide, WIDE_PATH_SIZE)) {
            wide[0] = L'\0';
        }
    }
    OPENFILENAMEW dialog = { 0 };
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = GetActiveWindow();
    dialog.lpstrFile = wide;
    dialog.nMaxFile = WIDE_PATH_SIZE;
    dialog.lpstrFilter = L"All files\0*.*\0";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_HIDEREADONLY |
                   OFN_EXPLORER;
    if (!GetSaveFileNameW(&dialog)) {
        return false;
    }
    return wide_to_utf8(wide, path, path_size);
}

static File_Read_Status file_access_read(char const *path, Array_Slice buffer, int32_t *size) {
    assert(path != NULL);
    assert(buffer.count > 0);
    assert(size != NULL);

    *size = 0;
    wchar_t wide[WIDE_PATH_SIZE] = L"";
    if (!utf8_to_wide(path, wide, WIDE_PATH_SIZE)) {
        return FILE_READ_FAILED;
    }
    // _wfopen takes the UTF-16 path, so that names with letters outside the system's code page
    // open; fopen would not find them. It fails on a folder, so a dropped folder is no file.
    FILE *file = _wfopen(wide, L"rb");
    if (file == NULL) {
        return FILE_READ_FAILED;
    }
    size_t const count = fread(buffer.items, 1, (size_t)buffer.count, file);
    bool const failed = ferror(file) != 0;
    // One more byte tells a file that fills the buffer exactly from one that is too large.
    bool const too_large = !failed && count == (size_t)buffer.count && fgetc(file) != EOF;
    fclose(file);
    if (failed) {
        return FILE_READ_FAILED;
    }
    if (too_large) {
        return FILE_READ_TOO_LARGE;
    }
    *size = (int32_t)count;
    return FILE_READ_OK;
}

static bool file_access_write(char const *path, Array_Slice bytes) {
    assert(path != NULL);
    assert(bytes.count >= 0);

    wchar_t wide[WIDE_PATH_SIZE] = L"";
    if (!utf8_to_wide(path, wide, WIDE_PATH_SIZE)) {
        return false;
    }
    FILE *file = _wfopen(wide, L"wb");
    if (file == NULL) {
        return false;
    }
    size_t const written = fwrite(bytes.items, 1, (size_t)bytes.count, file);
    // Closing writes what is buffered, so a full disk can show up only here.
    bool const closed = fclose(file) == 0;
    return closed && written == (size_t)bytes.count;
}

/**
 * Converts text, which must be valid UTF-8, to UTF-16 in wide, of wide_size characters.
 */
static bool utf8_to_wide(char const *text, wchar_t *wide, int32_t wide_size) {
    assert(text != NULL);
    assert(wide_size > 0);

    int const count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wide,
                                          (int)wide_size);
    return count > 0;
}

static bool wide_to_utf8(wchar_t const *wide, char *text, size_t text_size) {
    assert(wide != NULL);
    assert(text_size > 0);

    int const count = WideCharToMultiByte(CP_UTF8, 0, wide, -1, text, (int)text_size, NULL, NULL);
    return count > 0;
}
