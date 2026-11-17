#ifndef FILE_ACCESS_H
#define FILE_ACCESS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zeman/array_slice.h>
#include <zeman/str.h>

enum {
    // Bytes including the terminator of a file's path, in UTF-8. Windows allows paths of 32,767
    // characters, but the dialogs and most programs stop at about a quarter of this, and a
    // path of more is a mistake, not a place the user keeps files.
    FILE_PATH_SIZE = 1024,
};

typedef enum File_Read_Status {
    FILE_READ_OK,
    FILE_READ_FAILED,     // Not there, not a file, or not readable
    FILE_READ_TOO_LARGE,  // More bytes than the buffer holds
} File_Read_Status;

// What the user interface needs of the file system, which differs by platform: the desktop app
// has the system's dialogs and reads and writes files by their path, and a phone has neither,
// so the shared code is given these by the app, or none (see App_Config.files).
//
// Paths are C strings in UTF-8, because they go to the operating system, which wants them; the
// implementation turns them into whatever the system takes.
typedef struct File_Access {
    // Shows the system's dialog for choosing an existing file, and puts its path in path, a char
    // array of path_size bytes. Returns false when the user cancels or the dialog fails. Blocks
    // until the dialog closes.
    bool (*choose)(char *path, size_t path_size);
    // The same for the dialog that asks where to save a file, offering name, which is a file's
    // name without its folder. The path may be of a file that does not exist yet; the dialog has
    // already asked about replacing one that does.
    bool (*save_as)(Str name, char *path, size_t path_size);
    // Reads the file at path into buffer, a slice of bytes, and sets *size to how many. A file
    // that is larger than the buffer is not read, and neither is anything but a file.
    File_Read_Status (*read)(char const *path, Array_Slice buffer, int32_t *size);
    // Writes bytes, a slice of bytes, to a file at path, replacing it. Returns false when it
    // cannot be written in full.
    bool (*write)(char const *path, Array_Slice bytes);
} File_Access;

#endif
