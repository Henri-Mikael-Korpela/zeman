// Code Digger: counts the calls of C standard library functions in this project's C code.
// Built and run on request, see desktop/CMakeLists.txt:
//   cmake --build <build directory> --target code_digger, then run code_digger from it with the
//   project's folder as its argument, or from the project's folder without one.
#include <zeman/str.h>

#include <assert.h>
#include <dirent.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

enum {
    // The largest source file read whole. The biggest in the project is under 1 MB, so this
    // leaves room to grow; a larger file is reported rather than read partly.
    FILE_SIZE_MAX = 4 * 1024 * 1024,
    PATH_SIZE = 1024,
    // Folders waiting to be read. The project's tree is a few dozen folders; the bound makes a
    // runaway tree, such as a link back to a parent folder, stop with a message.
    FOLDER_COUNT_MAX = 512,
};

// Every function, and function-like macro, of the C11 standard library that this tool counts,
// by header: a call is an identifier from here followed by an opening parenthesis.
static Str const LIBRARY_NAMES[] = {
    // <assert.h>, <setjmp.h>, <signal.h>, <stdarg.h>, <stddef.h>, <locale.h>
    STR_INIT("assert"), STR_INIT("setjmp"), STR_INIT("longjmp"), STR_INIT("signal"),
    STR_INIT("raise"), STR_INIT("va_start"), STR_INIT("va_arg"), STR_INIT("va_end"),
    STR_INIT("va_copy"), STR_INIT("offsetof"), STR_INIT("setlocale"), STR_INIT("localeconv"),
    // <stdio.h>
    STR_INIT("remove"), STR_INIT("rename"), STR_INIT("tmpfile"), STR_INIT("tmpnam"),
    STR_INIT("fclose"), STR_INIT("fflush"), STR_INIT("fopen"), STR_INIT("freopen"),
    STR_INIT("setbuf"), STR_INIT("setvbuf"), STR_INIT("fprintf"), STR_INIT("fscanf"),
    STR_INIT("printf"), STR_INIT("scanf"), STR_INIT("snprintf"), STR_INIT("sprintf"),
    STR_INIT("sscanf"), STR_INIT("vfprintf"), STR_INIT("vfscanf"), STR_INIT("vprintf"),
    STR_INIT("vscanf"), STR_INIT("vsnprintf"), STR_INIT("vsprintf"), STR_INIT("vsscanf"),
    STR_INIT("fgetc"), STR_INIT("fgets"), STR_INIT("fputc"), STR_INIT("fputs"),
    STR_INIT("getc"), STR_INIT("getchar"), STR_INIT("putc"), STR_INIT("putchar"),
    STR_INIT("puts"), STR_INIT("ungetc"), STR_INIT("fread"), STR_INIT("fwrite"),
    STR_INIT("fgetpos"), STR_INIT("fseek"), STR_INIT("fsetpos"), STR_INIT("ftell"),
    STR_INIT("rewind"), STR_INIT("clearerr"), STR_INIT("feof"), STR_INIT("ferror"),
    STR_INIT("perror"),
    // <stdlib.h>
    STR_INIT("atof"), STR_INIT("atoi"), STR_INIT("atol"), STR_INIT("atoll"),
    STR_INIT("strtod"), STR_INIT("strtof"), STR_INIT("strtold"), STR_INIT("strtol"),
    STR_INIT("strtoll"), STR_INIT("strtoul"), STR_INIT("strtoull"), STR_INIT("rand"),
    STR_INIT("srand"), STR_INIT("aligned_alloc"), STR_INIT("calloc"), STR_INIT("free"),
    STR_INIT("malloc"), STR_INIT("realloc"), STR_INIT("abort"), STR_INIT("atexit"),
    STR_INIT("at_quick_exit"), STR_INIT("exit"), STR_INIT("_Exit"), STR_INIT("getenv"),
    STR_INIT("quick_exit"), STR_INIT("system"), STR_INIT("bsearch"), STR_INIT("qsort"),
    STR_INIT("abs"), STR_INIT("labs"), STR_INIT("llabs"), STR_INIT("div"), STR_INIT("ldiv"),
    STR_INIT("lldiv"), STR_INIT("mblen"), STR_INIT("mbtowc"), STR_INIT("wctomb"),
    STR_INIT("mbstowcs"), STR_INIT("wcstombs"),
    // <string.h>
    STR_INIT("memcpy"), STR_INIT("memmove"), STR_INIT("strcpy"), STR_INIT("strncpy"),
    STR_INIT("strcat"), STR_INIT("strncat"), STR_INIT("memcmp"), STR_INIT("strcmp"),
    STR_INIT("strcoll"), STR_INIT("strncmp"), STR_INIT("strxfrm"), STR_INIT("memchr"),
    STR_INIT("strchr"), STR_INIT("strcspn"), STR_INIT("strpbrk"), STR_INIT("strrchr"),
    STR_INIT("strspn"), STR_INIT("strstr"), STR_INIT("strtok"), STR_INIT("memset"),
    STR_INIT("strerror"), STR_INIT("strlen"),
    // <ctype.h>
    STR_INIT("isalnum"), STR_INIT("isalpha"), STR_INIT("isblank"), STR_INIT("iscntrl"),
    STR_INIT("isdigit"), STR_INIT("isgraph"), STR_INIT("islower"), STR_INIT("isprint"),
    STR_INIT("ispunct"), STR_INIT("isspace"), STR_INIT("isupper"), STR_INIT("isxdigit"),
    STR_INIT("tolower"), STR_INIT("toupper"),
    // <math.h>
    STR_INIT("fabs"), STR_INIT("fabsf"), STR_INIT("fmod"), STR_INIT("fmodf"),
    STR_INIT("remainder"), STR_INIT("fma"), STR_INIT("fmax"), STR_INIT("fmaxf"),
    STR_INIT("fmin"), STR_INIT("fminf"), STR_INIT("fdim"), STR_INIT("exp"), STR_INIT("expf"),
    STR_INIT("exp2"), STR_INIT("expm1"), STR_INIT("log"), STR_INIT("logf"), STR_INIT("log10"),
    STR_INIT("log2"), STR_INIT("log1p"), STR_INIT("pow"), STR_INIT("powf"), STR_INIT("sqrt"),
    STR_INIT("sqrtf"), STR_INIT("cbrt"), STR_INIT("hypot"), STR_INIT("sin"), STR_INIT("sinf"),
    STR_INIT("cos"), STR_INIT("cosf"), STR_INIT("tan"), STR_INIT("tanf"), STR_INIT("asin"),
    STR_INIT("acos"), STR_INIT("atan"), STR_INIT("atan2"), STR_INIT("atan2f"),
    STR_INIT("sinh"), STR_INIT("cosh"), STR_INIT("tanh"), STR_INIT("asinh"),
    STR_INIT("acosh"), STR_INIT("atanh"), STR_INIT("erf"), STR_INIT("tgamma"),
    STR_INIT("lgamma"), STR_INIT("ceil"), STR_INIT("ceilf"), STR_INIT("floor"),
    STR_INIT("floorf"), STR_INIT("trunc"), STR_INIT("round"), STR_INIT("roundf"),
    STR_INIT("lround"), STR_INIT("llround"), STR_INIT("nearbyint"), STR_INIT("rint"),
    STR_INIT("lrint"), STR_INIT("llrint"), STR_INIT("frexp"), STR_INIT("ldexp"),
    STR_INIT("modf"), STR_INIT("scalbn"), STR_INIT("ilogb"), STR_INIT("logb"),
    STR_INIT("nextafter"), STR_INIT("copysign"), STR_INIT("nan"), STR_INIT("isnan"),
    STR_INIT("isinf"), STR_INIT("isfinite"), STR_INIT("isnormal"), STR_INIT("fpclassify"),
    STR_INIT("signbit"),
    // <time.h>
    STR_INIT("clock"), STR_INIT("difftime"), STR_INIT("mktime"), STR_INIT("time"),
    STR_INIT("timespec_get"), STR_INIT("asctime"), STR_INIT("ctime"), STR_INIT("gmtime"),
    STR_INIT("localtime"), STR_INIT("strftime"),
};

enum { LIBRARY_NAMES_COUNT = sizeof(LIBRARY_NAMES) / sizeof(LIBRARY_NAMES[0]) };

// Everything the tool keeps while it works, passed to its functions instead of being global.
typedef struct Digger {
    // The folders still to read, as paths: a stack, so that the tree is walked without recursion.
    char folder_paths[FOLDER_COUNT_MAX][PATH_SIZE];
    // How often each name of LIBRARY_NAMES was called, in the same order.
    uint32_t calls[LIBRARY_NAMES_COUNT];
    uint32_t file_count;
    uint32_t file_skipped_count;
    int32_t folder_count;
    // One source file at a time, read whole.
    char file_text[FILE_SIZE_MAX];
} Digger;

static bool folder_is_skipped(char const *name, bool parent_is_root);
static bool path_join(char *buffer, char const *folder, char const *name);
static bool path_is_source(char const *name);
static bool digger_folder_push(Digger *digger, char const *path);
static bool digger_folder_read(Digger *digger, char const *folder, bool at_root);
static void digger_file_count(Digger *digger, char const *path);
static void digger_text_count(Digger *digger, Str text);
static size_t comment_or_literal_end(Str text, size_t index);
static size_t library_name_find(Str name);
static bool name_is_before(Str first, Str second);
static bool is_identifier_start(char c);
static bool is_identifier_part(char c);
static void digger_report_print(Digger const *digger);

int main(int argc, char **argv) {
    char const *root = ".";
    if (argc > 2) {
        fprintf(stderr, "Usage: code_digger [PROJECT_FOLDER]\n");
        return 1;
    }
    if (argc == 2) {
        root = argv[1];
    }
    // Static because it holds a whole source file, which is too large for the stack.
    static Digger digger;
    if (!digger_folder_push(&digger, root)) {
        fprintf(stderr, "code_digger: the folder name is too long.\n");
        return 1;
    }

    // Each folder read pushes its subfolders, at most FOLDER_COUNT_MAX of them waiting at once.
    // The root is only the first folder read, so that the project's tools folder is read but
    // the downloaded tools of android are not.
    bool at_root = true;
    while (digger.folder_count > 0) {
        digger.folder_count--;
        char path[PATH_SIZE] = "";
        snprintf(path, sizeof(path), "%s", digger.folder_paths[digger.folder_count]);
        if (!digger_folder_read(&digger, path, at_root)) {
            return 1;
        }
        at_root = false;
    }
    digger_report_print(&digger);
    return 0;
}

/**
 * Folders that hold no code of the project: the libraries in external, version control, build
 * output and the release folder, and the downloaded tools below android. The project's own
 * tools folder is the one directly in the root folder, which is read; parent_is_root says that
 * the folder being read is the root.
 */
static bool folder_is_skipped(char const *name, bool parent_is_root) {
    assert(name != NULL);
    assert(name[0] != '\0');

    if (strcmp(name, "external") == 0 || strcmp(name, ".git") == 0) {
        return true;
    }
    if (strcmp(name, "release") == 0 || strcmp(name, "keystore") == 0) {
        return true;
    }
    if (strncmp(name, "build", 5) == 0) {
        return true;
    }
    return strcmp(name, "tools") == 0 && !parent_is_root;
}

static bool path_join(char *buffer, char const *folder, char const *name) {
    assert(buffer != NULL);
    assert(folder != NULL);
    assert(name != NULL);

    int const written = snprintf(buffer, PATH_SIZE, "%s/%s", folder, name);
    return written > 0 && written < PATH_SIZE;
}

static bool path_is_source(char const *name) {
    assert(name != NULL);

    Str const text = str_from_cstr(name);
    if (text.length < 3) {
        return false;
    }
    Str const extension = str_slice(text, text.length - 2, text.length);
    return str_eq(extension, STR(".c")) || str_eq(extension, STR(".h"));
}

static bool digger_folder_push(Digger *digger, char const *path) {
    assert(path != NULL);
    assert(digger->folder_count >= 0);
    assert(digger->folder_count <= FOLDER_COUNT_MAX);

    if (digger->folder_count == FOLDER_COUNT_MAX) {
        return false;
    }
    int const written = snprintf(digger->folder_paths[digger->folder_count], PATH_SIZE, "%s",
                                 path);
    if (written < 0 || written >= PATH_SIZE) {
        return false;
    }
    digger->folder_count++;
    return true;
}

/**
 * Counts the source files of the folder and pushes its subfolders. Returns false, with the
 * reason printed, when the folder cannot be read at all, which makes the totals untrustworthy.
 */
static bool digger_folder_read(Digger *digger, char const *folder, bool at_root) {
    assert(folder != NULL);

    DIR *directory = opendir(folder);
    if (directory == NULL) {
        fprintf(stderr, "code_digger: cannot read the folder %s.\n", folder);
        return false;
    }
    bool ok = true;
    // Ends at the last entry; a folder cannot have more entries than the file system holds.
    for (struct dirent *entry = readdir(directory); entry != NULL; entry = readdir(directory)) {
        char const *name = entry->d_name;
        char path[PATH_SIZE] = "";
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            continue;
        }
        if (!path_join(path, folder, name)) {
            fprintf(stderr, "code_digger: the path %s/%s is too long.\n", folder, name);
            ok = false;
            break;
        }
        struct stat status;
        if (stat(path, &status) != 0) {
            continue;
        }
        if (S_ISDIR(status.st_mode)) {
            if (!folder_is_skipped(name, at_root) && !digger_folder_push(digger, path)) {
                fprintf(stderr, "code_digger: too many folders, is there a loop of links?\n");
                ok = false;
                break;
            }
        } else if (path_is_source(name)) {
            digger_file_count(digger, path);
        }
    }
    // A folder that was only read: closing it loses nothing, so its result does not matter.
    (void)closedir(directory);
    return ok;
}

/**
 * Reads the whole file and counts its calls. A file that cannot be read, or is too large, is
 * left out of the totals and counted as skipped, which the report says.
 */
static void digger_file_count(Digger *digger, char const *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        digger->file_skipped_count++;
        return;
    }
    size_t const size = fread(digger->file_text, 1, FILE_SIZE_MAX, file);
    bool const failed = ferror(file) != 0;
    // A full buffer may hide more: one byte more tells.
    bool const too_large = !failed && size == FILE_SIZE_MAX && fgetc(file) != EOF;
    // Closing a file that was only read loses nothing, so its result does not matter.
    (void)fclose(file);
    if (failed || too_large) {
        digger->file_skipped_count++;
        return;
    }
    digger->file_count++;
    digger_text_count(digger, str_make(digger->file_text, size));
}

/**
 * Counts the calls in a C text: an identifier of the standard library followed, after any white
 * space, by an opening parenthesis, and not preceded by "." or "->", which make it a member of
 * a struct. Comments, string and character literals and #include lines are skipped, so that
 * text such as "printf" in a message does not count.
 */
static void digger_text_count(Digger *digger, Str text) {
    size_t index = 0;
    // Every pass moves index forward by at least one byte, so the loop ends at the text's end.
    while (index < text.length) {
        size_t const skipped_to = comment_or_literal_end(text, index);
        if (skipped_to > index) {
            index = skipped_to;
            continue;
        }
        if (!is_identifier_start(text.data[index])) {
            index++;
            continue;
        }
        size_t end = index + 1;
        while (end < text.length && is_identifier_part(text.data[end])) {
            end++;
        }
        size_t after = end;
        while (after < text.length && (text.data[after] == ' ' || text.data[after] == '\t')) {
            after++;
        }
        bool const is_call = after < text.length && text.data[after] == '(';
        bool is_member = false;
        if (index > 0 && text.data[index - 1] == '.') {
            is_member = true;
        }
        if (index > 1 && text.data[index - 1] == '>' && text.data[index - 2] == '-') {
            is_member = true;
        }
        if (is_call && !is_member) {
            size_t const found = library_name_find(str_slice(text, index, end));
            if (found != STR_NOT_FOUND) {
                digger->calls[found]++;
            }
        }
        index = end;
    }
}

/**
 * Returns the index just past the comment, literal or #include line that starts at index, or
 * index itself when none starts there.
 */
static size_t comment_or_literal_end(Str text, size_t index) {
    assert(index < text.length);

    char const c = text.data[index];
    char next = '\0';
    if (index + 1 < text.length) {
        next = text.data[index + 1];
    }
    size_t end = index;
    if (c == '/' && next == '/') {
        end = index + 2;
        while (end < text.length && text.data[end] != '\n') {
            end++;
        }
    } else if (c == '/' && next == '*') {
        end = index + 2;
        while (end + 1 < text.length && !(text.data[end] == '*' && text.data[end + 1] == '/')) {
            end++;
        }
        end += 2;
        if (end > text.length) {
            end = text.length;
        }
    } else if (c == '"' || c == '\'') {
        end = index + 1;
        // A backslash escapes the next byte; an unclosed literal ends at its line.
        while (end < text.length && text.data[end] != c && text.data[end] != '\n') {
            if (text.data[end] == '\\') {
                end++;
            }
            end++;
        }
        if (end < text.length) {
            end++;
        } else {
            end = text.length;
        }
    } else if (c == '#' && str_starts_with(str_slice(text, index, text.length),
                                           STR("#include"))) {
        end = index + 1;
        while (end < text.length && text.data[end] != '\n') {
            end++;
        }
    }
    assert(end >= index);
    assert(end <= text.length);
    return end;
}

static size_t library_name_find(Str name) {
    for (size_t i = 0; i < LIBRARY_NAMES_COUNT; i++) {
        if (str_eq(name, LIBRARY_NAMES[i])) {
            return i;
        }
    }
    return STR_NOT_FOUND;
}

/**
 * Whether first sorts before second, by bytes, a prefix before the longer name.
 */
static bool name_is_before(Str first, Str second) {
    size_t length = first.length;
    if (second.length < length) {
        length = second.length;
    }
    int const order = memcmp(first.data, second.data, length);
    if (order != 0) {
        return order < 0;
    }
    return first.length < second.length;
}

static bool is_identifier_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool is_identifier_part(char c) {
    return is_identifier_start(c) || (c >= '0' && c <= '9');
}

/**
 * Prints the called functions, the most called first and by name among equals, as "name (count)"
 * one to a line, and the number of files read.
 */
static void digger_report_print(Digger const *digger) {
    // Selection by the greatest count left, so that no sorting library or allocation is needed;
    // a name printed is marked by clearing its count, after which it no longer wins.
    uint32_t remaining[LIBRARY_NAMES_COUNT] = { 0 };
    memcpy(remaining, digger->calls, sizeof(remaining));
    size_t function_count = 0;
    // At most one pass per name in the table, and each pass prints one name or ends the loop.
    for (size_t pass = 0; pass < LIBRARY_NAMES_COUNT; pass++) {
        size_t best = STR_NOT_FOUND;
        for (size_t i = 0; i < LIBRARY_NAMES_COUNT; i++) {
            if (remaining[i] == 0) {
                continue;
            }
            if (best == STR_NOT_FOUND || remaining[i] > remaining[best]) {
                best = i;
            } else if (remaining[i] == remaining[best] &&
                       name_is_before(LIBRARY_NAMES[i], LIBRARY_NAMES[best])) {
                best = i;
            }
        }
        if (best == STR_NOT_FOUND) {
            break;
        }
        printf("%.*s (%" PRIu32 ")\n", STR_ARGUMENTS(LIBRARY_NAMES[best]), remaining[best]);
        remaining[best] = 0;
        function_count++;
    }
    printf("\n%zu standard library functions called, in %" PRIu32 " files", function_count,
           digger->file_count);
    if (digger->file_skipped_count > 0) {
        printf(" (%" PRIu32 " files could not be read or were too large)",
               digger->file_skipped_count);
    }
    printf(".\n");
}
