#ifndef FILE_ACCESS_SYSTEM_H
#define FILE_ACCESS_SYSTEM_H

#include <zeman/file_access.h>

/**
 * The desktop app's file access: Windows' open and save dialogs, and reading and writing files
 * by UTF-8 paths, which the C library's own functions on Windows do not take. The returned
 * functions never change global state, and may be called from the user interface thread only.
 */
File_Access file_access_system(void);

#endif
