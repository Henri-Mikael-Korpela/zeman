#ifndef STR_H
#define STR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Length-based strings, which do not rely on a NUL terminator.
 *
 * A Str never owns its bytes. It points into memory that someone else owns, such as a string
 * literal, a text box's buffer or the bytes of a network message, and carries its own length, so
 * that slicing, trimming and splitting never copy and cannot run past the end. Text that goes to
 * raylib, raygui, SQLite or the C library, which want a NUL-terminated string, is converted at
 * that boundary with str_to_cstr or str_copy_truncated, and nowhere else.
 *
 * A Str_Builder appends into a fixed buffer that the caller provides, and never grows.
 */
typedef struct Str {
    const char *data;
    size_t length;
} Str;

typedef struct Str_Builder {
    char *data;
    size_t length;
    size_t capacity;
    bool overflow;  // Set once an append did not fit
} Str_Builder;

/**
 * Returned by the search functions when nothing was found: an index that no string has.
 */
static const size_t STR_NOT_FOUND = SIZE_MAX;

/**
 * The initializer of a Str for a string literal, which a static or constant table needs, where
 * the compound literal of STR is not allowed.
 */
#define STR_INIT(literal) { .data = (literal), .length = sizeof(literal) - 1 }

/**
 * The Str of a string literal, usable in expressions. The literal is named once and is not
 * evaluated, only measured, so that this is safe in any context.
 */
#define STR(literal) ((Str){ .data = (literal), .length = sizeof(literal) - 1 })

/**
 * The arguments that printf's "%.*s" takes for a Str: its length, then its bytes. The Str is
 * named twice, so it must be a plain variable and not an expression with side effects.
 */
#define STR_ARGUMENTS(text) (int)(text).length, (text).data

/**
 * Makes a Str from the pointer to its first byte and its length in bytes.
 */
Str str_make(const char *data, size_t length);

/**
 * Makes a Str from a NUL-terminated C string, for text that comes from the operating system or a
 * library. The terminator is not part of it. A NULL pointer is the empty string.
 */
Str str_from_cstr(const char *cstr);

/**
 * The bytes from start up to but not including end, both kept within the string, so that the
 * result is always a part of s.
 */
Str str_slice(Str s, size_t start, size_t end);

/**
 * s without any of the bytes in chars at its start and end.
 */
Str str_trim_any(Str s, Str chars);

/**
 * Whether both strings have the same length and the same bytes.
 */
bool str_eq(Str a, Str b);

/**
 * Whether the first bytes of s are prefix.
 */
bool str_starts_with(Str s, Str prefix);

/**
 * The index of the first byte c in s, or STR_NOT_FOUND.
 */
size_t str_find_char(Str s, char c);

/**
 * The index of the first byte of s that is none of the bytes in chars, or STR_NOT_FOUND when
 * every byte is one of them.
 */
size_t str_find_not_any(Str s, Str chars);

/**
 * The index of the first occurrence of needle in haystack, or STR_NOT_FOUND. An empty needle
 * is found at index 0.
 */
size_t str_find(Str haystack, Str needle);

/**
 * Splits s around the first occurrence of sep. Returns false when sep is not in s, and then
 * leaves before and after as they were; otherwise they are the bytes before it and after it.
 */
bool str_split_once(Str s, Str sep, Str *before, Str *after);

/**
 * Parses a decimal number made only of digits, with no sign and no spaces. Returns false when s
 * is empty, has another byte, or the number does not fit; out is then not changed.
 */
bool str_to_u64(Str s, uint64_t *out);

/**
 * Copies s into buffer, a char array of capacity bytes, and terminates it, for the code that
 * wants a C string. Returns false, leaving buffer unchanged, when s and its terminator do not
 * fit or s has a NUL byte, which would end the C string early.
 */
bool str_to_cstr(Str s, char *buffer, size_t capacity);

/**
 * Copies as much of s as fits into buffer, a char array of capacity bytes, and terminates it,
 * for a field of fixed size that holds whatever text it can. The text is cut at a whole
 * character when it is UTF-8. Returns false when something was cut off, or s has a NUL byte,
 * which also ends the text; capacity is at least 1.
 */
bool str_copy_truncated(Str s, char *buffer, size_t capacity);

/**
 * Starts an empty builder that writes into buffer, which has room for capacity bytes.
 */
void str_builder_init(Str_Builder *builder, char *buffer, size_t capacity);

/**
 * Appends a string. Returns false, writing nothing and setting overflow, when it does not fit.
 */
bool str_builder_append(Str_Builder *builder, Str s);

/**
 * What has been written so far, as a view of the builder's buffer.
 */
Str str_builder_str(const Str_Builder *builder);

#endif
