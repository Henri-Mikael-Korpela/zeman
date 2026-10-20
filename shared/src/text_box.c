#include <zeman/text_box.h>

#include <assert.h>
#include <string.h>

enum {
    // Pasting reads at most this many bytes of the clipboard: far more than the largest text
    // box, so that a huge clipboard of mostly line breaks still fills it, while the read stays
    // bounded.
    CLIPBOARD_READ_SIZE_MAX = 4096,
};
// Cast because the constants come from different enums.
static_assert((int32_t)CLIPBOARD_READ_SIZE_MAX >= (int32_t)TEXT_BOX_SIZE_MAX,
              "A paste can fill any text box.");

static int32_t utf8_sequence_size(const char *text);

int32_t text_box_paste(char *text, int32_t text_size, int32_t cursor_index,
                       const char *clipboard) {
    const int32_t length = (int32_t)strlen(text);
    assert(length < text_size);
    assert(text_size <= TEXT_BOX_SIZE_MAX);
    assert(cursor_index >= 0);
    assert(cursor_index <= length);
    char pasted[TEXT_BOX_SIZE_MAX];
    int32_t pasted_length = 0;
    const int32_t room = text_size - 1 - length;
    int32_t i = 0;
    while (i < CLIPBOARD_READ_SIZE_MAX && clipboard[i] != '\0' && pasted_length < room) {
        const unsigned char byte = (unsigned char)clipboard[i];
        const int32_t size = utf8_sequence_size(clipboard + i);
        if (byte == '\n' || byte == '\t') {
            pasted[pasted_length] = ' ';
            pasted_length++;
        } else if (byte >= 0x20 && byte != 0x7F && size > 0) {
            if (pasted_length + size > room) {
                break; // A character that does not fit whole is left out, and the rest with it.
            }
            memcpy(pasted + pasted_length, clipboard + i, (size_t)size);
            pasted_length += size;
        }
        // Anything else, such as the carriage return of a Windows line break, is skipped.
        i += size > 0 ? size : 1;
    }
    assert(pasted_length <= room);
    // The tail moves right, terminator included, to make room at the cursor.
    memmove(text + cursor_index + pasted_length, text + cursor_index,
            (size_t)(length - cursor_index + 1));
    memcpy(text + cursor_index, pasted, (size_t)pasted_length);
    assert((int32_t)strlen(text) == length + pasted_length);
    return cursor_index + pasted_length;
}

// Bytes in the UTF-8 character that text starts with, or 0 when it is not valid UTF-8 there,
// such as a stray continuation byte or a sequence cut short by the end of the text.
static int32_t utf8_sequence_size(const char *text) {
    const unsigned char first = (unsigned char)text[0];
    assert(first != 0);
    int32_t size = 0;
    if (first < 0x80) {
        size = 1;
    } else if (first >= 0xC2 && first <= 0xDF) {
        size = 2;
    } else if (first >= 0xE0 && first <= 0xEF) {
        size = 3;
    } else if (first >= 0xF0 && first <= 0xF4) {
        size = 4;
    } else {
        return 0;
    }
    // Stops at the terminator too, which is no continuation byte.
    for (int32_t i = 1; i < size; i++) {
        if (((unsigned char)text[i] & 0xC0) != 0x80) {
            return 0;
        }
    }
    return size;
}
