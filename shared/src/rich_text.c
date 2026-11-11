#include <zeman/byte.h>
#include <zeman/rich_text.h>

#include <assert.h>
#include <string.h>

// The beginnings of an address that make a link, compared without regard to case. "www." is
// there because people write addresses that way, without the scheme.
static Str const LINK_PREFIXES[] = {
    STR_INIT("https://"),
    STR_INIT("http://"),
    STR_INIT("www."),
};
enum { LINK_PREFIX_COUNT = sizeof(LINK_PREFIXES) / sizeof(LINK_PREFIXES[0]) };

static size_t rich_text_link_prefix_at(Str text, size_t index);
static bool rich_text_link_starts_word(Str text, size_t index);
static bool rich_text_link_byte_allowed(Str text, size_t index);
static size_t rich_text_link_trim(Str text, size_t start, size_t end);
static byte ascii_lower(byte character);
static bool ascii_is_alphanumeric(byte character);

bool rich_text_is_image_marker(Str text, size_t index) {
    assert(index <= text.length);

    if (text.length - index < RICH_TEXT_IMAGE_MARKER_SIZE) {
        return false;
    }
    return memcmp(text.data + index, RICH_TEXT_IMAGE_MARKER, RICH_TEXT_IMAGE_MARKER_SIZE) == 0;
}

int32_t rich_text_image_count(Str text) {
    return rich_text_image_index_at(text, text.length);
}

int32_t rich_text_image_index_at(Str text, size_t index) {
    assert(index <= text.length);

    int32_t count = 0;
    // A marker's first byte, 0xEF, starts no other part of one, so a byte-wise scan finds every
    // marker exactly once.
    for (size_t i = 0; i < index; i++) {
        if (rich_text_is_image_marker(text, i)) {
            count++;
        }
    }
    assert(count >= 0);
    return count;
}

Rich_Text_Link rich_text_link_find(Str text, size_t from) {
    // Bounded by the length: each pass moves i on, by at least one byte.
    size_t i = from;
    while (i < text.length) {
        size_t const prefix_length = rich_text_link_prefix_at(text, i);
        if (prefix_length == 0) {
            i++;
            continue;
        }
        size_t end = i + prefix_length;
        while (end < text.length && rich_text_link_byte_allowed(text, end)) {
            end++;
        }
        end = rich_text_link_trim(text, i + prefix_length, end);
        // "www." or "https://" alone is no address.
        if (end > i + prefix_length) {
            assert(end <= text.length);
            return (Rich_Text_Link){ .start = i, .end = end };
        }
        i += prefix_length;
    }
    return (Rich_Text_Link){ .start = STR_NOT_FOUND, .end = STR_NOT_FOUND };
}

Rich_Text_Link rich_text_link_at(Str text, size_t index) {
    size_t from = 0;
    // Bounded by the length: each link found ends after from, and the next search starts there.
    while (from < text.length) {
        Rich_Text_Link const link = rich_text_link_find(text, from);
        if (link.start == STR_NOT_FOUND || link.start > index) {
            break;
        }
        if (index < link.end) {
            return link;
        }
        assert(link.end > from);
        from = link.end;
    }
    return (Rich_Text_Link){ .start = STR_NOT_FOUND, .end = STR_NOT_FOUND };
}

bool rich_text_link_url(Str link, char *url, size_t url_size) {
    assert(url_size > 0);
    assert(link.length > 0);

    Str_Builder builder;
    str_builder_init(&builder, url, url_size - 1);
    // Compared as rich_text_link_find found it, without regard to case.
    bool const scheme_missing = link.length >= 4 && ascii_lower((byte)link.data[0]) == 'w' &&
                                ascii_lower((byte)link.data[1]) == 'w' &&
                                ascii_lower((byte)link.data[2]) == 'w' && link.data[3] == '.';
    if (scheme_missing) {
        (void)str_builder_append(&builder, STR("https://"));
    }
    (void)str_builder_append(&builder, link);
    if (builder.overflow) {
        url[0] = '\0';
        return false;
    }
    url[builder.length] = '\0';
    return true;
}

size_t rich_text_character_next(Str text, size_t index) {
    assert(index < text.length);

    int32_t size = str_utf8_sequence_size(str_slice(text, index, text.length));
    if (size == 0) {
        size = 1;
    }
    assert(index + (size_t)size <= text.length);
    return index + (size_t)size;
}

size_t rich_text_character_previous(Str text, size_t index) {
    assert(index > 0);
    assert(index <= text.length);

    // A UTF-8 character has at most three continuation bytes, 10xxxxxx, after its first byte.
    size_t start = index - 1;
    for (int32_t i = 0; i < 3 && start > 0 && ((byte)text.data[start] & 0xC0) == 0x80; i++) {
        start--;
    }
    // Continuation bytes that follow no valid first byte are characters of their own, as
    // rich_text_character_next steps over them.
    if (rich_text_character_next(text, start) != index) {
        return index - 1;
    }
    return start;
}

int32_t rich_text_codepoint(Str text, size_t index, int32_t *size) {
    assert(index < text.length);
    assert(size != NULL);

    Str const rest = str_slice(text, index, text.length);
    int32_t const sequence_size = str_utf8_sequence_size(rest);
    if (sequence_size == 0) {
        *size = 1;
        return 0xFFFD;
    }
    // The payload bits of the first byte: 7, 5, 4 or 3 of them for sizes 1 to 4.
    static byte const FIRST_MASKS[5] = { 0, 0x7F, 0x1F, 0x0F, 0x07 };
    int32_t codepoint = (byte)rest.data[0] & FIRST_MASKS[sequence_size];
    for (int32_t i = 1; i < sequence_size; i++) {
        codepoint = (codepoint << 6) | ((byte)rest.data[i] & 0x3F);
    }
    *size = sequence_size;
    assert(codepoint >= 0);
    return codepoint;
}

int32_t rich_text_codepoint_encode(int32_t codepoint, char *bytes) {
    assert(codepoint >= 0);
    assert(codepoint <= 0x10FFFF);
    assert(codepoint < 0xD800 || codepoint > 0xDFFF); // A surrogate is no character.

    if (codepoint < 0x80) {
        bytes[0] = (char)codepoint;
        return 1;
    }
    if (codepoint < 0x800) {
        bytes[0] = (char)(0xC0 | (codepoint >> 6));
        bytes[1] = (char)(0x80 | (codepoint & 0x3F));
        return 2;
    }
    if (codepoint < 0x10000) {
        bytes[0] = (char)(0xE0 | (codepoint >> 12));
        bytes[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        bytes[2] = (char)(0x80 | (codepoint & 0x3F));
        return 3;
    }
    bytes[0] = (char)(0xF0 | (codepoint >> 18));
    bytes[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
    bytes[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
    bytes[3] = (char)(0x80 | (codepoint & 0x3F));
    return 4;
}

size_t rich_text_clean(Str source, char *target, size_t target_size) {
    assert(target != NULL || target_size == 0);

    size_t length = 0;
    size_t i = 0;
    // Bounded by the source's length: each pass moves i on by at least one byte.
    while (i < source.length) {
        byte const character = (byte)source.data[i];
        if (rich_text_is_image_marker(source, i)) {
            i += RICH_TEXT_IMAGE_MARKER_SIZE;
            continue;
        }
        if (character == '\n' || character == '\t') {
            if (length + 1 > target_size) {
                break;
            }
            // A tab would need tab stops to line up, which a description has no use for.
            target[length] = (char)character;
            if (character == '\t') {
                target[length] = ' ';
            }
            length++;
            i++;
            continue;
        }
        // The carriage return of a Windows line break, and other control characters.
        if (character < 0x20 || character == 0x7F) {
            i++;
            continue;
        }
        int32_t const size = str_utf8_sequence_size(str_slice(source, i, source.length));
        if (size == 0) {
            i++;
            continue;
        }
        if (length + (size_t)size > target_size) {
            break; // A character that does not fit whole is left out, and the rest with it.
        }
        memcpy(target + length, source.data + i, (size_t)size);
        length += (size_t)size;
        i += (size_t)size;
    }
    assert(length <= target_size);
    return length;
}

bool rich_text_replace(char *text, size_t *length, size_t text_size, size_t start, size_t end,
                       Str insert) {
    assert(text != NULL);
    assert(*length < text_size);
    assert(start <= end);
    assert(end <= *length);

    size_t const removed = end - start;
    // Kept apart from text: inserting a slice of text itself would read what is being moved.
    assert(insert.length == 0 || insert.data + insert.length <= text ||
           insert.data >= text + text_size);
    if (*length - removed + insert.length >= text_size) {
        return false;
    }
    // The tail moves to just after the inserted text, terminator included.
    memmove(text + start + insert.length, text + end, *length - end + 1);
    if (insert.length > 0) {
        memcpy(text + start, insert.data, insert.length);
    }
    *length = *length - removed + insert.length;
    assert(text[*length] == '\0');
    return true;
}

/**
 * The length of the link prefix at index, or 0 when none starts a word there.
 */
static size_t rich_text_link_prefix_at(Str text, size_t index) {
    assert(index < text.length);

    if (!rich_text_link_starts_word(text, index)) {
        return 0;
    }
    for (int32_t i = 0; i < LINK_PREFIX_COUNT; i++) {
        Str const prefix = LINK_PREFIXES[i];
        if (text.length - index < prefix.length) {
            continue;
        }
        bool matches = true;
        for (size_t j = 0; j < prefix.length && matches; j++) {
            matches = ascii_lower((byte)text.data[index + j]) == (byte)prefix.data[j];
        }
        if (matches) {
            return prefix.length;
        }
    }
    return 0;
}

/**
 * A link starts a word, so that "xwww.a" or "a.http://b" is not taken for one; it may follow
 * an opening bracket or a quote.
 */
static bool rich_text_link_starts_word(Str text, size_t index) {
    if (index == 0) {
        return true;
    }
    byte const before = (byte)text.data[index - 1];
    if (ascii_is_alphanumeric(before)) {
        return false;
    }
    return before != '.' && before != '-' && before != '_' && before != '@' && before != '/' &&
           before != ':';
}

/**
 * Whether the byte at index can be part of an address. Spaces and control characters end one,
 * and so do the quotes, angle brackets and other characters that surround addresses in text
 * rather than appear in them. Quotes matter for safety too: the address is handed to the
 * operating system to open (see raylib's OpenURL), quoted, and must not be able to end the
 * quoting. Characters outside ASCII are allowed, for addresses in other languages, except an
 * image marker.
 */
static bool rich_text_link_byte_allowed(Str text, size_t index) {
    assert(index < text.length);

    byte const character = (byte)text.data[index];
    if (character >= 0x80) {
        return !rich_text_is_image_marker(text, index);
    }
    if (character <= 0x20 || character == 0x7F) {
        return false;
    }
    return str_find_char(STR("\"'<>`{}|\\^"), (char)character) == STR_NOT_FOUND;
}

/**
 * The end of a link without the punctuation that ends the sentence around it, such as the full
 * stop of "See www.example.com." A closing bracket stays when the link opened it, as in
 * Wikipedia's addresses. body_start is where the link's prefix ends; nothing before it is cut.
 */
static size_t rich_text_link_trim(Str text, size_t body_start, size_t end) {
    assert(body_start <= end);
    assert(end <= text.length);

    // Bounded by the link's length: each pass cuts one byte or ends the loop.
    while (end > body_start) {
        char const last = text.data[end - 1];
        if (last == ')' || last == ']') {
            char opening = '(';
            if (last == ']') {
                opening = '[';
            }
            int32_t balance = 0;
            for (size_t i = body_start; i < end; i++) {
                balance += (int32_t)(text.data[i] == opening) - (int32_t)(text.data[i] == last);
            }
            if (balance >= 0) {
                break; // Every closing bracket has its opening one in the link.
            }
        } else if (str_find_char(STR(".,;:!?"), last) == STR_NOT_FOUND) {
            break;
        }
        end--;
    }
    return end;
}

static byte ascii_lower(byte character) {
    if (character >= 'A' && character <= 'Z') {
        return (byte)(character - 'A' + 'a');
    }
    return character;
}

static bool ascii_is_alphanumeric(byte character) {
    bool const letter = ascii_lower(character) >= 'a' && ascii_lower(character) <= 'z';
    return letter || (character >= '0' && character <= '9');
}
