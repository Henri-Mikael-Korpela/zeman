#ifndef RICH_TEXT_H
#define RICH_TEXT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zeman/str.h>

// The description of an item is rich text: UTF-8 text with line breaks, in which each image is
// one U+FFFC OBJECT REPLACEMENT CHARACTER, the character Unicode sets aside for an object that
// is embedded in text. The images themselves are stored apart from the text, in the order of
// their markers, so that the nth marker shows the nth image; the text stays small enough to
// load with every item, and an image is loaded only when its item is opened.
//
// Links are not stored as such: a web address anywhere in the text is found when the text is
// drawn (see rich_text_link_find), so that typing or pasting one is all it takes.

// U+FFFC in UTF-8.
#define RICH_TEXT_IMAGE_MARKER "\xEF\xBF\xBC"

enum {
    RICH_TEXT_IMAGE_MARKER_SIZE = 3,
    // Bytes including the terminator of the address opened for a link: the longest link a
    // description can hold, with "https://" put before a "www." one.
    RICH_TEXT_URL_SIZE = 4096 + 8,
};

// A link in a text: the bytes from start up to but not including end.
typedef struct Rich_Text_Link {
    size_t start;  // STR_NOT_FOUND when there is no link
    size_t end;
} Rich_Text_Link;

/**
 * Whether an image marker starts at index.
 */
bool rich_text_is_image_marker(Str text, size_t index);

/**
 * The number of image markers in text.
 */
int32_t rich_text_image_count(Str text);

/**
 * The number of image markers that start before index: the index of the image of a marker
 * that starts at index.
 */
int32_t rich_text_image_index_at(Str text, size_t index);

/**
 * The first link that starts at or after from: an address that begins with "http://",
 * "https://" or "www." at the start of a word, up to the first space or character that cannot
 * be in an address, without the punctuation that ends a sentence after it. start is
 * STR_NOT_FOUND when there is none.
 */
Rich_Text_Link rich_text_link_find(Str text, size_t from);

/**
 * The link at index, or one with start STR_NOT_FOUND when index is in none.
 */
Rich_Text_Link rich_text_link_at(Str text, size_t index);

/**
 * The address to open for a link found by rich_text_link_find: the link itself, with "https://"
 * before one that starts with "www.". Returns false, leaving url empty, when it does not fit.
 */
bool rich_text_link_url(Str link, char *url, size_t url_size);

/**
 * The index of the character after the one at index, which is before the end of text. A byte
 * that is not valid UTF-8 counts as one character, so that the step is always forward.
 */
size_t rich_text_character_next(Str text, size_t index);

/**
 * The index of the character before index, which is after the start of text.
 */
size_t rich_text_character_previous(Str text, size_t index);

/**
 * The Unicode code point of the character at index, which is before the end of text, and its
 * size in bytes; U+FFFD, the replacement character, for a byte that is not valid UTF-8.
 */
int32_t rich_text_codepoint(Str text, size_t index, int32_t *size);

/**
 * Writes the code point, a Unicode scalar value, as UTF-8 into bytes, which has room for four.
 * Returns how many bytes it took.
 */
int32_t rich_text_codepoint_encode(int32_t codepoint, char *bytes);

/**
 * Copies pasted text into target, a buffer of target_size bytes, as a description may hold it:
 * line breaks are kept, a Windows line break becomes one, a tab becomes a space, and other
 * control characters, broken UTF-8 and image markers are left out, so that pasted text can never
 * make an image without one. Stops at the first character that does not fit. Returns the bytes
 * written; target is not terminated.
 */
size_t rich_text_clean(Str source, char *target, size_t target_size);

/**
 * Replaces the bytes from start up to end of text, which holds *length bytes in a buffer of
 * text_size bytes, with insert, and keeps it terminated. Returns false, changing nothing, when
 * the result does not fit.
 */
bool rich_text_replace(char *text, size_t *length, size_t text_size, size_t start, size_t end,
                       Str insert);

#endif
