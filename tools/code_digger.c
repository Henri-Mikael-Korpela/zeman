// Code Digger: analyses this project's C code. By default it counts the calls of C standard
// library functions; with --dead-code it lists functions, constants, macros and variables that
// nothing uses. Built and run on request, see desktop/CMakeLists.txt:
//   cmake --build <build directory> --target code_digger, then run code_digger [--dead-code]
//   from it with the project's folder as its argument, or from the project's folder without one.
#include <zeman/str.h>

#include <assert.h>
#include <dirent.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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
    // Source files listed. The project has about fifty; the bound is for a tree that has grown.
    FILE_COUNT_MAX = 1024,
    // The most tokens of one file. The largest source file has under 100000, so this leaves
    // room to grow; a file with more is reported rather than analysed partly.
    TOKEN_COUNT_MAX = 262144,
    // Names that dead code analysis keeps track of, and the table that finds them by name, which
    // is a power of two at least twice the count so that probing stays short.
    SYMBOL_COUNT_MAX = 4096,
    SYMBOL_TABLE_SIZE = 8192,
    NAME_POOL_SIZE = 256 * 1024,
    // Findings kept for the report: the unused names, and the unused local variables.
    FINDING_COUNT_MAX = 4096,
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

// What a token is, which is all that dead code analysis needs to know about the text.
typedef enum Token_Kind {
    TOKEN_IDENTIFIER,
    TOKEN_LITERAL,
    TOKEN_NUMBER,
    TOKEN_PUNCTUATION,
} Token_Kind;

// What a token defines when it is the name in a definition or a declaration.
typedef enum Definition {
    DEFINITION_NONE,
    DEFINITION_CONSTANT,
    DEFINITION_ENUM_CONSTANT,
    DEFINITION_FUNCTION,
    DEFINITION_FUNCTION_PROTOTYPE,
    DEFINITION_LOCAL,
    DEFINITION_MACRO,
    DEFINITION_VARIABLE,
} Definition;

typedef struct Token {
    Str text;
    int32_t line;
    // Index of the name token of the definition that this token is part of, such as the function
    // whose body it is in, or -1 outside every definition.
    int32_t owner;
    int32_t span_end;    // For a function's name token: the index of the closing brace of its body
    uint8_t kind;        // Token_Kind
    uint8_t definition;  // Definition
    bool starts_line;
    bool is_member;      // After "." or "->": a field, which is not one of the names analysed
} Token;

// The tokens of one file; their text points into the file's bytes, so it is only good until
// the next file is read.
typedef struct Token_List {
    Token items[TOKEN_COUNT_MAX];
    int32_t count;
} Token_List;

typedef struct Symbol {
    Str name;
    char const *path;     // Of the file that defines it
    int32_t line;
    uint8_t definition;   // Definition
    bool has_body;        // A function that is defined, not only declared
    bool reachable;
    bool used_outside;    // Used where no definition owns the use, so always needed
} Symbol;

typedef enum Finding_Kind {
    FINDING_CONSTANT,
    FINDING_FUNCTION,
    FINDING_LOCAL,
    FINDING_MACRO,
    FINDING_VARIABLE,
} Finding_Kind;

typedef struct Finding {
    Str name;
    Str scope;  // For a local variable: the function it is in
    char const *path;
    int32_t line;
    uint8_t kind;  // Finding_Kind
} Finding;

// What dead code analysis keeps: every name defined at the top level of a file, which of them
// use which, and what was found unused.
typedef struct Dead_Code {
    Symbol symbols[SYMBOL_COUNT_MAX];
    Finding findings[FINDING_COUNT_MAX];
    // A bit for each pair of symbols: row uses column, in the body or initializer of the row.
    uint8_t uses[SYMBOL_COUNT_MAX][SYMBOL_COUNT_MAX / 8];
    int32_t table[SYMBOL_TABLE_SIZE];  // Symbol index plus one, or 0 for a free slot
    char name_pool[NAME_POOL_SIZE];
    size_t name_pool_size;
    int32_t symbol_count;
    int32_t finding_count;
} Dead_Code;

// Everything the tool keeps while it works, passed to its functions instead of being global.
typedef struct Digger {
    // The folders still to read, as paths: a stack, so that the tree is walked without recursion.
    char folder_paths[FOLDER_COUNT_MAX][PATH_SIZE];
    char file_paths[FILE_COUNT_MAX][PATH_SIZE];
    Token_List tokens;
    Dead_Code dead_code;
    // How often each name of LIBRARY_NAMES was called, in the same order.
    uint32_t calls[LIBRARY_NAMES_COUNT];
    uint32_t file_count;
    uint32_t file_skipped_count;
    int32_t folder_count;
    int32_t file_path_count;
    // One source file at a time, read whole.
    char file_text[FILE_SIZE_MAX];
} Digger;

static Str const KEYWORDS[] = {
    STR_INIT("auto"), STR_INIT("break"), STR_INIT("case"), STR_INIT("const"),
    STR_INIT("continue"), STR_INIT("default"), STR_INIT("do"), STR_INIT("else"),
    STR_INIT("enum"), STR_INIT("extern"), STR_INIT("for"), STR_INIT("goto"), STR_INIT("if"),
    STR_INIT("inline"), STR_INIT("register"), STR_INIT("restrict"), STR_INIT("return"),
    STR_INIT("sizeof"), STR_INIT("static"), STR_INIT("struct"), STR_INIT("switch"),
    STR_INIT("typedef"), STR_INIT("union"), STR_INIT("volatile"), STR_INIT("while"),
};

static Str const BUILTIN_TYPES[] = {
    STR_INIT("_Bool"), STR_INIT("DIR"), STR_INIT("FILE"), STR_INIT("bool"), STR_INIT("char"),
    STR_INIT("double"), STR_INIT("float"), STR_INIT("int"), STR_INIT("long"),
    STR_INIT("short"), STR_INIT("signed"), STR_INIT("unsigned"), STR_INIT("void"),
};

// The functions that something other than the project's code calls.
static Str const ENTRY_NAMES[] = { STR_INIT("main") };

static bool folder_is_skipped(char const *name, bool parent_is_root);
static bool path_join(char *buffer, char const *folder, char const *name);
static bool path_is_source(char const *name);
static int path_compare(void const *first, void const *second);
static bool digger_folder_push(Digger *digger, char const *path);
static bool digger_folder_read(Digger *digger, char const *folder, bool at_root);
static bool digger_file_add(Digger *digger, char const *path);
static Str digger_file_read(Digger *digger, char const *path);
static void digger_calls_count(Digger *digger);
static void digger_text_count(Digger *digger, Str text);
static size_t comment_or_literal_end(Str text, size_t index);
static size_t library_name_find(Str name);
static bool name_is_before(Str first, Str second);
static bool is_identifier_start(char c);
static bool is_identifier_part(char c);
static void digger_report_print(Digger const *digger);
static bool dead_code_run(Digger *digger);
static bool dead_code_scan(Digger *digger, bool uses_pass);
static bool tokenize(Str text, Token_List *list);
static bool token_add(Token_List *list, Str text, Token_Kind kind, int32_t line, bool starts_line);
static int32_t newline_count(Str text);
static bool token_is_punctuation(Token const *tokens, int32_t count, int32_t index, char c);
static bool token_is_word(Token const *tokens, int32_t count, int32_t index, Str word);
static int32_t matching_close(Token const *tokens, int32_t count, int32_t open);
static bool name_is_in(Str name, Str const *names, size_t name_count);
static bool token_is_type(Token const *tokens, int32_t count, int32_t index);
static bool declares_name(Token const *tokens, int32_t count, int32_t index);
static void classify_file(Token_List *list);
static int32_t classify_top_level(Token_List *list, int32_t index, int32_t *statement_start);
static bool is_function_declarator(Token const *tokens, int32_t count, int32_t index);
static int32_t classify_function(Token_List *list, int32_t index, int32_t *statement_start);
static void classify_body(Token_List *list, int32_t open, int32_t close, int32_t owner);
static int32_t classify_object(Token_List *list, int32_t index, int32_t *statement_start);
static int32_t classify_enum(Token_List *list, int32_t index);
static int32_t classify_directive(Token_List *list, int32_t index);
static bool symbol_define(Dead_Code *dead, Token const *token, char const *path);
static int32_t symbol_find(Dead_Code const *dead, Str name);
static uint32_t name_hash(Str name);
static Str name_pool_copy(Dead_Code *dead, Str name);
static void uses_record(Dead_Code *dead, Token_List const *list);
static bool locals_check(Dead_Code *dead, Token_List const *list, char const *path);
static bool finding_add(Dead_Code *dead, Finding finding);
static void symbols_reach(Dead_Code *dead);
static bool symbols_collect(Dead_Code *dead);
static int finding_compare(void const *first, void const *second);
static void dead_code_report_print(Dead_Code const *dead, Digger const *digger);

int main(int argc, char **argv) {
    char const *root = ".";
    bool dead_code = false;
    bool root_given = false;
    // One pass for each argument given.
    for (int32_t i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dead-code") == 0) {
            dead_code = true;
        } else if (argv[i][0] != '-' && !root_given) {
            root = argv[i];
            root_given = true;
        } else {
            fprintf(stderr, "Usage: code_digger [--dead-code] [PROJECT_FOLDER]\n");
            return 1;
        }
    }
    // Static because it holds a whole source file and its tokens, which are too large for the
    // stack.
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
    // In path order, so that two runs print the same however the file system lists the files.
    qsort(digger.file_paths, (size_t)digger.file_path_count, PATH_SIZE, path_compare);
    if (dead_code) {
        if (!dead_code_run(&digger)) {
            return 1;
        }
        return 0;
    }
    digger_calls_count(&digger);
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

static int path_compare(void const *first, void const *second) {
    return strcmp(first, second);
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
 * Lists the source files of the folder and pushes its subfolders. Returns false, with the
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
        } else if (path_is_source(name) && !digger_file_add(digger, path)) {
            fprintf(stderr, "code_digger: more than %d source files.\n", FILE_COUNT_MAX);
            ok = false;
            break;
        }
    }
    // A folder that was only read: closing it loses nothing, so its result does not matter.
    (void)closedir(directory);
    return ok;
}

static bool digger_file_add(Digger *digger, char const *path) {
    assert(path != NULL);
    assert(digger->file_path_count >= 0);
    assert(digger->file_path_count <= FILE_COUNT_MAX);

    if (digger->file_path_count == FILE_COUNT_MAX) {
        return false;
    }
    // The path was made to fit PATH_SIZE by path_join.
    snprintf(digger->file_paths[digger->file_path_count], PATH_SIZE, "%s", path);
    digger->file_path_count++;
    return true;
}

/**
 * Reads the whole file into the digger's buffer. Returns a Str with a NULL data when the file
 * cannot be read or is too large, to be left out of the results.
 */
static Str digger_file_read(Digger *digger, char const *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return (Str){ NULL, 0 };
    }
    size_t const size = fread(digger->file_text, 1, FILE_SIZE_MAX, file);
    bool const failed = ferror(file) != 0;
    // A full buffer may hide more: one byte more tells.
    bool const too_large = !failed && size == FILE_SIZE_MAX && fgetc(file) != EOF;
    // Closing a file that was only read loses nothing, so its result does not matter.
    (void)fclose(file);
    if (failed || too_large) {
        return (Str){ NULL, 0 };
    }
    return str_make(digger->file_text, size);
}

static void digger_calls_count(Digger *digger) {
    for (int32_t i = 0; i < digger->file_path_count; i++) {
        Str const text = digger_file_read(digger, digger->file_paths[i]);
        if (text.data == NULL) {
            digger->file_skipped_count++;
            continue;
        }
        digger->file_count++;
        digger_text_count(digger, text);
    }
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

/**
 * Splits C text into tokens: identifiers, numbers, literals (one token each, without their
 * text) and single punctuation characters. Comments and #include lines are left out. Returns
 * false when the file has more tokens than the list holds.
 */
static bool tokenize(Str text, Token_List *list) {
    list->count = 0;
    int32_t line = 1;
    bool starts_line = true;
    size_t index = 0;
    // Every pass moves index forward by at least one byte, so the loop ends at the text's end.
    while (index < text.length) {
        char const c = text.data[index];
        if (c == '\n') {
            line++;
            index++;
            starts_line = true;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r') {
            index++;
            continue;
        }
        // A line continued with a backslash is still the same line to a macro.
        if (c == '\\' && index + 1 < text.length && text.data[index + 1] == '\n') {
            line++;
            index += 2;
            continue;
        }
        size_t const skipped_to = comment_or_literal_end(text, index);
        if (skipped_to > index) {
            if ((c == '"' || c == '\'') &&
                !token_add(list, str_slice(text, index, index + 1), TOKEN_LITERAL, line,
                           starts_line)) {
                return false;
            }
            if (c == '"' || c == '\'') {
                starts_line = false;
            }
            line += newline_count(str_slice(text, index, skipped_to));
            index = skipped_to;
            continue;
        }
        size_t end = index + 1;
        Token_Kind kind = TOKEN_PUNCTUATION;
        if (is_identifier_start(c)) {
            kind = TOKEN_IDENTIFIER;
            while (end < text.length && is_identifier_part(text.data[end])) {
                end++;
            }
        } else if (c >= '0' && c <= '9') {
            kind = TOKEN_NUMBER;
            // A sign belongs to the number after an exponent, as in 1e-5.
            while (end < text.length &&
                   (is_identifier_part(text.data[end]) || text.data[end] == '.' ||
                    ((text.data[end] == '+' || text.data[end] == '-') &&
                     (text.data[end - 1] == 'e' || text.data[end - 1] == 'E')))) {
                end++;
            }
        }
        if (!token_add(list, str_slice(text, index, end), kind, line, starts_line)) {
            return false;
        }
        starts_line = false;
        index = end;
    }
    return true;
}

static bool token_add(Token_List *list, Str text, Token_Kind kind, int32_t line,
                      bool starts_line) {
    assert(list->count >= 0);
    assert(list->count <= TOKEN_COUNT_MAX);
    assert(text.length > 0);

    if (list->count == TOKEN_COUNT_MAX) {
        return false;
    }
    bool is_member = false;
    if (kind == TOKEN_IDENTIFIER && list->count > 0) {
        Token const *previous = &list->items[list->count - 1];
        if (previous->kind == TOKEN_PUNCTUATION && previous->text.data[0] == '.') {
            is_member = true;
        }
        if (previous->kind == TOKEN_PUNCTUATION && previous->text.data[0] == '>' &&
            list->count > 1) {
            Token const *before = &list->items[list->count - 2];
            is_member = before->kind == TOKEN_PUNCTUATION && before->text.data[0] == '-';
        }
    }
    list->items[list->count] = (Token){
        .text = text,
        .line = line,
        .owner = -1,
        .span_end = -1,
        .kind = (uint8_t)kind,
        .definition = (uint8_t)DEFINITION_NONE,
        .starts_line = starts_line,
        .is_member = is_member,
    };
    list->count++;
    return true;
}

static int32_t newline_count(Str text) {
    int32_t count = 0;
    for (size_t i = 0; i < text.length; i++) {
        if (text.data[i] == '\n') {
            count++;
        }
    }
    return count;
}

static bool token_is_punctuation(Token const *tokens, int32_t count, int32_t index, char c) {
    if (index < 0 || index >= count) {
        return false;
    }
    return tokens[index].kind == TOKEN_PUNCTUATION && tokens[index].text.data[0] == c;
}

static bool token_is_word(Token const *tokens, int32_t count, int32_t index, Str word) {
    if (index < 0 || index >= count) {
        return false;
    }
    return tokens[index].kind == TOKEN_IDENTIFIER && str_eq(tokens[index].text, word);
}

/**
 * The index of the bracket that closes the one at open, of the same kind, or the last token
 * when the file ends first, so that a broken file is analysed as far as it goes.
 */
static int32_t matching_close(Token const *tokens, int32_t count, int32_t open) {
    assert(open >= 0);
    assert(open < count);

    char const opening = tokens[open].text.data[0];
    char closing = ')';
    if (opening == '{') {
        closing = '}';
    }
    if (opening == '[') {
        closing = ']';
    }
    int32_t depth = 0;
    for (int32_t i = open; i < count; i++) {
        if (token_is_punctuation(tokens, count, i, opening)) {
            depth++;
        } else if (token_is_punctuation(tokens, count, i, closing)) {
            depth--;
            if (depth == 0) {
                return i;
            }
        }
    }
    return count - 1;
}

static bool name_is_in(Str name, Str const *names, size_t name_count) {
    for (size_t i = 0; i < name_count; i++) {
        if (str_eq(name, names[i])) {
            return true;
        }
    }
    return false;
}

/**
 * Whether the token is the name of a type: a built-in one, one of the project's, in Ada case,
 * or ending in _t, or the tag after struct, union or enum. A multiplication such as "a * b" is
 * told from a declaration of a pointer by this, since the project's variables are lower case.
 */
static bool token_is_type(Token const *tokens, int32_t count, int32_t index) {
    assert(index >= 0);
    assert(index < count);

    Token const *token = &tokens[index];
    if (token->kind != TOKEN_IDENTIFIER) {
        return false;
    }
    Str const name = token->text;
    if (name_is_in(name, KEYWORDS, sizeof(KEYWORDS) / sizeof(KEYWORDS[0]))) {
        return false;
    }
    if (name_is_in(name, BUILTIN_TYPES, sizeof(BUILTIN_TYPES) / sizeof(BUILTIN_TYPES[0]))) {
        return true;
    }
    if (name.length > 2 && str_eq(str_slice(name, name.length - 2, name.length), STR("_t"))) {
        return true;
    }
    if (token_is_word(tokens, count, index - 1, STR("struct")) ||
        token_is_word(tokens, count, index - 1, STR("union")) ||
        token_is_word(tokens, count, index - 1, STR("enum"))) {
        return true;
    }
    bool has_lower_case = false;
    for (size_t i = 0; i < name.length; i++) {
        has_lower_case = has_lower_case || (name.data[i] >= 'a' && name.data[i] <= 'z');
    }
    return name.data[0] >= 'A' && name.data[0] <= 'Z' && has_lower_case;
}

/**
 * Whether the identifier at index is the name in a declaration: after a type, with any "const"
 * and "*" between them, as in "Event const *event". Whether a declaration is really there is
 * for the caller to tell from what follows the name.
 */
static bool declares_name(Token const *tokens, int32_t count, int32_t index) {
    assert(index >= 0);
    assert(index < count);

    Token const *token = &tokens[index];
    if (token->kind != TOKEN_IDENTIFIER || token->is_member) {
        return false;
    }
    if (name_is_in(token->text, KEYWORDS, sizeof(KEYWORDS) / sizeof(KEYWORDS[0]))) {
        return false;
    }
    int32_t previous = index - 1;
    // At most as many steps as there are tokens before the name.
    while (previous >= 0 && (token_is_punctuation(tokens, count, previous, '*') ||
                             token_is_word(tokens, count, previous, STR("const")))) {
        previous--;
    }
    return previous >= 0 && token_is_type(tokens, count, previous);
}

/**
 * Marks what each token of a file is part of: the names it defines, and for every other token
 * the definition that it is inside, such as the function whose body it is in.
 */
static void classify_file(Token_List *list) {
    int32_t statement_start = 0;
    int32_t index = 0;
    // Every pass moves index forward by at least one token.
    while (index < list->count) {
        int32_t const next = classify_top_level(list, index, &statement_start);
        assert(next > index);
        index = next;
    }
}

/**
 * Classifies what starts at index, outside every brace, and returns the index after it.
 * *statement_start is where the statement the index is in began.
 */
static int32_t classify_top_level(Token_List *list, int32_t index, int32_t *statement_start) {
    Token const *tokens = list->items;
    int32_t const count = list->count;
    if (token_is_punctuation(tokens, count, index, '#')) {
        int32_t const next = classify_directive(list, index);
        *statement_start = next;
        return next;
    }
    if (token_is_word(tokens, count, index, STR("enum"))) {
        return classify_enum(list, index);
    }
    if (token_is_punctuation(tokens, count, index, ';')) {
        *statement_start = index + 1;
        return index + 1;
    }
    // A struct, union or compound literal body: nothing in it is defined at the top level.
    if (token_is_punctuation(tokens, count, index, '{')) {
        return matching_close(tokens, count, index) + 1;
    }
    if (tokens[index].kind != TOKEN_IDENTIFIER || tokens[index].is_member) {
        return index + 1;
    }
    if (is_function_declarator(tokens, count, index)) {
        return classify_function(list, index, statement_start);
    }
    bool const next_starts_object = token_is_punctuation(tokens, count, index + 1, '=') ||
                                    token_is_punctuation(tokens, count, index + 1, ';') ||
                                    token_is_punctuation(tokens, count, index + 1, '[');
    if (next_starts_object && declares_name(tokens, count, index)) {
        return classify_object(list, index, statement_start);
    }
    return index + 1;
}

/**
 * A name followed by parameters, after the type it returns: not a function pointer, whose
 * parenthesis holds a star, nor a type's name such as the "bool" in a typedef of one.
 */
static bool is_function_declarator(Token const *tokens, int32_t count, int32_t index) {
    assert(index >= 0);
    assert(index < count);

    if (index == 0 || !token_is_punctuation(tokens, count, index + 1, '(')) {
        return false;
    }
    if (token_is_punctuation(tokens, count, index + 2, '*')) {
        return false;
    }
    Str const name = tokens[index].text;
    if (name_is_in(name, KEYWORDS, sizeof(KEYWORDS) / sizeof(KEYWORDS[0])) ||
        name_is_in(name, BUILTIN_TYPES, sizeof(BUILTIN_TYPES) / sizeof(BUILTIN_TYPES[0]))) {
        return false;
    }
    Token const *previous = &tokens[index - 1];
    if (previous->kind == TOKEN_PUNCTUATION) {
        return previous->text.data[0] == '*';
    }
    return previous->kind == TOKEN_IDENTIFIER;
}

/**
 * A function's name token, its parameters and, if it has one, its body. Returns the index
 * after the declaration.
 */
static int32_t classify_function(Token_List *list, int32_t index, int32_t *statement_start) {
    Token *tokens = list->items;
    int32_t const count = list->count;
    int32_t const close = matching_close(tokens, count, index + 1);
    tokens[index].definition = (uint8_t)DEFINITION_FUNCTION_PROTOTYPE;
    for (int32_t i = index + 1; i <= close; i++) {
        tokens[i].owner = index;
    }
    int32_t next = close + 1;
    // Past an attribute or a comment-like macro, to what ends the declaration.
    while (next < count && !token_is_punctuation(tokens, count, next, '{') &&
           !token_is_punctuation(tokens, count, next, ';')) {
        next++;
    }
    if (next < count && token_is_punctuation(tokens, count, next, '{')) {
        int32_t const body_close = matching_close(tokens, count, next);
        tokens[index].definition = (uint8_t)DEFINITION_FUNCTION;
        tokens[index].span_end = body_close;
        classify_body(list, next, body_close, index);
        next = body_close;
    }
    *statement_start = next + 1;
    return next + 1;
}

/**
 * Every token in a function's body belongs to the function. A local variable is a name that
 * is declared and then followed by "=", ";" or "[".
 */
static void classify_body(Token_List *list, int32_t open, int32_t close, int32_t owner) {
    Token *tokens = list->items;
    int32_t const count = list->count;
    assert(open <= close);
    assert(close < count);

    for (int32_t i = open + 1; i < close; i++) {
        tokens[i].owner = owner;
        bool const next_starts_variable = token_is_punctuation(tokens, count, i + 1, '=') ||
                                          token_is_punctuation(tokens, count, i + 1, ';') ||
                                          token_is_punctuation(tokens, count, i + 1, '[');
        if (next_starts_variable && declares_name(tokens, count, i)) {
            tokens[i].definition = (uint8_t)DEFINITION_LOCAL;
        }
    }
}

/**
 * A variable or constant at the top level: constant when "const" comes after the last "*" of
 * the declaration, since a pointer to constant text can still be pointed elsewhere. Its
 * initializer, to the semicolon, belongs to it. A typedef declares no object, so it is skipped.
 */
static int32_t classify_object(Token_List *list, int32_t index, int32_t *statement_start) {
    Token *tokens = list->items;
    int32_t const count = list->count;
    int32_t last_star = -1;
    bool is_typedef = false;
    for (int32_t i = *statement_start; i < index; i++) {
        is_typedef = is_typedef || token_is_word(tokens, count, i, STR("typedef"));
        if (token_is_punctuation(tokens, count, i, '*')) {
            last_star = i;
        }
    }
    bool is_constant = false;
    // Only this statement's own tokens: from the last star, or else from its start.
    int32_t constant_search_start = *statement_start;
    if (last_star >= 0) {
        constant_search_start = last_star + 1;
    }
    for (int32_t i = constant_search_start; i < index; i++) {
        is_constant = is_constant || token_is_word(tokens, count, i, STR("const"));
    }
    if (is_typedef) {
        return index + 1;
    }
    if (is_constant) {
        tokens[index].definition = (uint8_t)DEFINITION_CONSTANT;
    } else {
        tokens[index].definition = (uint8_t)DEFINITION_VARIABLE;
    }
    int32_t end = index + 1;
    // Past brackets as a whole, so that a semicolon inside an initializer does not end it.
    while (end < count && !token_is_punctuation(tokens, count, end, ';')) {
        if (token_is_punctuation(tokens, count, end, '{') ||
            token_is_punctuation(tokens, count, end, '(') ||
            token_is_punctuation(tokens, count, end, '[')) {
            end = matching_close(tokens, count, end);
        }
        end++;
    }
    for (int32_t i = index + 1; i < end && i < count; i++) {
        tokens[i].owner = index;
    }
    *statement_start = end + 1;
    return end + 1;
}

/**
 * The enumeration constants of an enum with a body: each name starts an entry, and what follows
 * it up to the comma belongs to it. Returns the index after the body, or after the word enum
 * when it only names a type.
 */
static int32_t classify_enum(Token_List *list, int32_t index) {
    Token *tokens = list->items;
    int32_t const count = list->count;
    int32_t open = index + 1;
    if (open < count && tokens[open].kind == TOKEN_IDENTIFIER) {
        open++;
    }
    if (!token_is_punctuation(tokens, count, open, '{')) {
        return index + 1;
    }
    int32_t const close = matching_close(tokens, count, open);
    int32_t entry = open + 1;
    // Each pass handles one entry, which is at least one token.
    while (entry < close) {
        if (tokens[entry].kind != TOKEN_IDENTIFIER) {
            entry++;
            continue;
        }
        tokens[entry].definition = (uint8_t)DEFINITION_ENUM_CONSTANT;
        int32_t end = entry + 1;
        while (end < close && !token_is_punctuation(tokens, count, end, ',')) {
            if (token_is_punctuation(tokens, count, end, '(')) {
                end = matching_close(tokens, count, end);
            }
            tokens[end].owner = entry;
            end++;
        }
        entry = end + 1;
    }
    return close + 1;
}

/**
 * A preprocessor line. A macro with a body is defined, and its body belongs to it; one without
 * a body, such as an include guard or a setting for a library, is not counted, since its use is
 * somewhere this tool does not look. The same goes for a macro that only renames another name,
 * which is there to redirect a library's calls, as "#define GetCharPressed android_input_char"
 * does for raygui; what it names is then used outside every definition. Returns the index of
 * the first token after the line.
 */
static int32_t classify_directive(Token_List *list, int32_t index) {
    Token *tokens = list->items;
    int32_t const count = list->count;
    int32_t end = index + 1;
    while (end < count && !tokens[end].starts_line) {
        end++;
    }
    bool const is_define = token_is_word(tokens, count, index + 1, STR("define"));
    int32_t const name = index + 2;
    bool const is_rename = end == name + 2 && name + 1 < count &&
                           tokens[name + 1].kind == TOKEN_IDENTIFIER;
    if (is_define && name < count && tokens[name].kind == TOKEN_IDENTIFIER && end > name + 1 &&
        !is_rename) {
        tokens[name].definition = (uint8_t)DEFINITION_MACRO;
        for (int32_t i = name + 1; i < end; i++) {
            tokens[i].owner = name;
        }
    }
    return end;
}

/**
 * Lists what nothing uses: functions, constants, macros and variables defined at the top level
 * of a file, and local variables. Two passes over the files: the first finds every name that is
 * defined, the second finds which names use which, since a use means a reference anywhere but
 * at a definition. A name is dead unless it is reachable from the uses outside every
 * definition, or from main, so that two functions that only call each other are both reported.
 * The analysis reads the text, not the compiler's view of it, so it is a guide: a name used only
 * through a macro, or by code this tool does not read, is reported although it is needed.
 * Returns false, with the reason printed, when a table is full or a file has too many tokens.
 */
static bool dead_code_run(Digger *digger) {
    Dead_Code *dead = &digger->dead_code;
    if (!dead_code_scan(digger, false) || !dead_code_scan(digger, true)) {
        return false;
    }
    symbols_reach(dead);
    if (!symbols_collect(dead)) {
        fprintf(stderr, "code_digger: more than %d findings.\n", FINDING_COUNT_MAX);
        return false;
    }
    qsort(dead->findings, (size_t)dead->finding_count, sizeof(Finding), finding_compare);
    dead_code_report_print(dead, digger);
    return true;
}

/**
 * One pass over every file: with uses_pass false it defines the symbols, true it records their
 * uses and checks the local variables.
 */
static bool dead_code_scan(Digger *digger, bool uses_pass) {
    Dead_Code *dead = &digger->dead_code;
    Token_List *list = &digger->tokens;
    digger->file_count = 0;
    digger->file_skipped_count = 0;
    for (int32_t file = 0; file < digger->file_path_count; file++) {
        char const *path = digger->file_paths[file];
        Str const text = digger_file_read(digger, path);
        if (text.data == NULL) {
            digger->file_skipped_count++;
            continue;
        }
        if (!tokenize(text, list)) {
            fprintf(stderr, "code_digger: %s has more than %d tokens.\n", path,
                    TOKEN_COUNT_MAX);
            return false;
        }
        digger->file_count++;
        classify_file(list);
        if (uses_pass) {
            uses_record(dead, list);
            if (!locals_check(dead, list, path)) {
                fprintf(stderr, "code_digger: more than %d findings.\n", FINDING_COUNT_MAX);
                return false;
            }
            continue;
        }
        for (int32_t i = 0; i < list->count; i++) {
            if (!symbol_define(dead, &list->items[i], path)) {
                fprintf(stderr, "code_digger: more than %d names, or too long names.\n",
                        SYMBOL_COUNT_MAX);
                return false;
            }
        }
    }
    return true;
}

/**
 * Adds the name the token defines, if it defines one at the top level. A name defined more than
 * once, as a function is by its prototype and its definition, stays one symbol, located at the
 * definition with a body. Returns false when the tables are full.
 */
static bool symbol_define(Dead_Code *dead, Token const *token, char const *path) {
    assert(token->text.length > 0);

    Definition const definition = (Definition)token->definition;
    if (definition == DEFINITION_NONE || definition == DEFINITION_LOCAL) {
        return true;
    }
    bool const has_body = definition == DEFINITION_FUNCTION;
    int32_t const found = symbol_find(dead, token->text);
    if (found >= 0) {
        Symbol *symbol = &dead->symbols[found];
        if (has_body && !symbol->has_body) {
            symbol->path = path;
            symbol->line = token->line;
            symbol->definition = (uint8_t)definition;
            symbol->has_body = true;
        }
        return true;
    }
    if (dead->symbol_count == SYMBOL_COUNT_MAX) {
        return false;
    }
    Str const name = name_pool_copy(dead, token->text);
    if (name.data == NULL) {
        return false;
    }
    int32_t const index = dead->symbol_count;
    dead->symbols[index] = (Symbol){
        .name = name,
        .path = path,
        .line = token->line,
        .definition = (uint8_t)definition,
        .has_body = has_body,
    };
    dead->symbol_count++;
    uint32_t slot = name_hash(name) & (SYMBOL_TABLE_SIZE - 1);
    // The table has more slots than symbols, so a free one is found.
    while (dead->table[slot] != 0) {
        slot = (slot + 1) & (SYMBOL_TABLE_SIZE - 1);
    }
    dead->table[slot] = index + 1;
    return true;
}

/**
 * Returns the index of the symbol of that name, or -1.
 */
static int32_t symbol_find(Dead_Code const *dead, Str name) {
    uint32_t slot = name_hash(name) & (SYMBOL_TABLE_SIZE - 1);
    // At most one probe per slot; a free slot ends the search sooner.
    for (int32_t probe = 0; probe < SYMBOL_TABLE_SIZE; probe++) {
        int32_t const entry = dead->table[slot];
        if (entry == 0) {
            return -1;
        }
        if (str_eq(dead->symbols[entry - 1].name, name)) {
            return entry - 1;
        }
        slot = (slot + 1) & (SYMBOL_TABLE_SIZE - 1);
    }
    return -1;
}

/**
 * FNV-1a, which spreads identifiers well enough for a table this size.
 */
static uint32_t name_hash(Str name) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < name.length; i++) {
        hash ^= (uint8_t)name.data[i];
        hash *= 16777619u;
    }
    return hash;
}

/**
 * Keeps a name beyond the file it was read from. Returns a Str with NULL data when the pool is
 * full.
 */
static Str name_pool_copy(Dead_Code *dead, Str name) {
    if (name.length >= NAME_POOL_SIZE - dead->name_pool_size) {
        return (Str){ NULL, 0 };
    }
    char *copy = &dead->name_pool[dead->name_pool_size];
    memcpy(copy, name.data, name.length);
    copy[name.length] = '\0';
    dead->name_pool_size += name.length + 1;
    return str_make(copy, name.length);
}

/**
 * Records each use of a symbol in the file: a name that is not a definition and not a field. A
 * use inside a definition is an edge from that symbol to the one used; a use outside every
 * definition, as in a type or an assertion at the top level, makes the used symbol needed.
 */
static void uses_record(Dead_Code *dead, Token_List const *list) {
    for (int32_t i = 0; i < list->count; i++) {
        Token const *token = &list->items[i];
        if (token->kind != TOKEN_IDENTIFIER || token->is_member) {
            continue;
        }
        if (token->definition != (uint8_t)DEFINITION_NONE) {
            continue;
        }
        int32_t const used = symbol_find(dead, token->text);
        if (used < 0) {
            continue;
        }
        int32_t user = -1;
        if (token->owner >= 0) {
            user = symbol_find(dead, list->items[token->owner].text);
        }
        if (user < 0) {
            dead->symbols[used].used_outside = true;
        } else if (user != used) {
            dead->uses[user][used / 8] |= (uint8_t)(1u << (used % 8));
        }
    }
}

/**
 * Reports each local variable that its function never mentions again. The compiler's
 * -Wunused-variable finds the same for the code that is built, so this mostly matters for
 * code that only the other platform builds. A name mentioned again in a nested block counts as
 * used, since scopes are not told apart. Returns false when the findings are full.
 */
static bool locals_check(Dead_Code *dead, Token_List const *list, char const *path) {
    for (int32_t i = 0; i < list->count; i++) {
        Token const *token = &list->items[i];
        if (token->definition != (uint8_t)DEFINITION_LOCAL) {
            continue;
        }
        Token const *function = &list->items[token->owner];
        bool used = false;
        for (int32_t k = i + 1; k <= function->span_end && !used; k++) {
            Token const *other = &list->items[k];
            used = other->kind == TOKEN_IDENTIFIER && !other->is_member &&
                   str_eq(other->text, token->text);
        }
        if (used) {
            continue;
        }
        Finding finding = {
            .name = name_pool_copy(dead, token->text),
            .scope = name_pool_copy(dead, function->text),
            .path = path,
            .line = token->line,
            .kind = (uint8_t)FINDING_LOCAL,
        };
        if (finding.name.data == NULL || finding.scope.data == NULL ||
            !finding_add(dead, finding)) {
            return false;
        }
    }
    return true;
}

static bool finding_add(Dead_Code *dead, Finding finding) {
    assert(dead->finding_count >= 0);
    assert(dead->finding_count <= FINDING_COUNT_MAX);

    if (dead->finding_count == FINDING_COUNT_MAX) {
        return false;
    }
    dead->findings[dead->finding_count] = finding;
    dead->finding_count++;
    return true;
}

/**
 * Marks the symbols that are needed: those used outside every definition, the entry points, and
 * all that those use in turn.
 */
static void symbols_reach(Dead_Code *dead) {
    static int32_t queue[SYMBOL_COUNT_MAX];
    int32_t queue_count = 0;
    for (int32_t i = 0; i < dead->symbol_count; i++) {
        Symbol *symbol = &dead->symbols[i];
        bool const is_entry = name_is_in(symbol->name, ENTRY_NAMES,
                                         sizeof(ENTRY_NAMES) / sizeof(ENTRY_NAMES[0]));
        if (symbol->used_outside || is_entry) {
            symbol->reachable = true;
            queue[queue_count] = i;
            queue_count++;
        }
    }
    // Each symbol is queued once, when it is first marked, so the loop ends after at most
    // SYMBOL_COUNT_MAX pops.
    for (int32_t head = 0; head < queue_count; head++) {
        int32_t const user = queue[head];
        for (int32_t used = 0; used < dead->symbol_count; used++) {
            bool const uses = (dead->uses[user][used / 8] & (1u << (used % 8))) != 0;
            if (uses && !dead->symbols[used].reachable) {
                dead->symbols[used].reachable = true;
                queue[queue_count] = used;
                queue_count++;
            }
        }
    }
    assert(queue_count <= dead->symbol_count);
}

/**
 * Turns every symbol that is not needed into a finding. Returns false when they do not fit.
 */
static bool symbols_collect(Dead_Code *dead) {
    for (int32_t i = 0; i < dead->symbol_count; i++) {
        Symbol const *symbol = &dead->symbols[i];
        if (symbol->reachable) {
            continue;
        }
        Finding_Kind kind = FINDING_VARIABLE;
        switch ((Definition)symbol->definition) {
        case DEFINITION_CONSTANT:
        case DEFINITION_ENUM_CONSTANT:
            kind = FINDING_CONSTANT;
            break;
        case DEFINITION_FUNCTION:
        case DEFINITION_FUNCTION_PROTOTYPE:
            kind = FINDING_FUNCTION;
            break;
        case DEFINITION_MACRO:
            kind = FINDING_MACRO;
            break;
        case DEFINITION_VARIABLE:
            kind = FINDING_VARIABLE;
            break;
        case DEFINITION_LOCAL:
        case DEFINITION_NONE:
            // Neither is ever a symbol: symbol_define leaves them out.
            assert(false);
            break;
        }
        Finding const finding = {
            .name = symbol->name,
            .scope = (Str){ NULL, 0 },
            .path = symbol->path,
            .line = symbol->line,
            .kind = (uint8_t)kind,
        };
        if (!finding_add(dead, finding)) {
            return false;
        }
    }
    return true;
}

/**
 * By kind, then file, then line, then name.
 */
static int finding_compare(void const *first, void const *second) {
    Finding const *a = first;
    Finding const *b = second;
    if (a->kind != b->kind) {
        return (int)a->kind - (int)b->kind;
    }
    int const by_path = strcmp(a->path, b->path);
    if (by_path != 0) {
        return by_path;
    }
    if (a->line != b->line) {
        return (a->line > b->line) - (a->line < b->line);
    }
    if (name_is_before(a->name, b->name)) {
        return -1;
    }
    return (int)name_is_before(b->name, a->name);
}

/**
 * The findings under a heading for each kind, as "file:line  name", and how many there are.
 */
static void dead_code_report_print(Dead_Code const *dead, Digger const *digger) {
    int32_t previous_kind = -1;
    int32_t kind_count = 0;
    for (int32_t i = 0; i < dead->finding_count; i++) {
        Finding const *finding = &dead->findings[i];
        if (finding->kind != previous_kind) {
            previous_kind = finding->kind;
            kind_count = 0;
            for (int32_t k = i; k < dead->finding_count; k++) {
                kind_count += (int32_t)(dead->findings[k].kind == finding->kind);
            }
            char const *heading = "Unused variables";
            switch ((Finding_Kind)finding->kind) {
            case FINDING_CONSTANT:
                heading = "Unused constants";
                break;
            case FINDING_FUNCTION:
                heading = "Unused functions";
                break;
            case FINDING_LOCAL:
                heading = "Unused local variables";
                break;
            case FINDING_MACRO:
                heading = "Unused macros";
                break;
            case FINDING_VARIABLE:
                heading = "Unused variables";
                break;
            }
            if (i > 0) {
                printf("\n");
            }
            printf("%s (%" PRId32 "):\n", heading, kind_count);
        }
        printf("  %s:%" PRId32 "  %.*s", finding->path, finding->line,
               STR_ARGUMENTS(finding->name));
        if (finding->scope.data != NULL) {
            printf("  in %.*s", STR_ARGUMENTS(finding->scope));
        }
        printf("\n");
    }
    printf("\n%" PRId32 " unused names in %" PRIu32 " files", dead->finding_count,
           digger->file_count);
    if (digger->file_skipped_count > 0) {
        printf(" (%" PRIu32 " files could not be read or were too large)",
               digger->file_skipped_count);
    }
    printf(".\n");
}
