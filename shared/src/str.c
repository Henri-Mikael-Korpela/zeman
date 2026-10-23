#include <zeman/str.h>

#include <assert.h>
#include <string.h>

static size_t utf8_length_at_most(Str s, size_t length_max);

Str str_make(const char *data, size_t length) {
    assert(data != NULL || length == 0);
    return (Str){
        .data = data,
        .length = length,
    };
}

Str str_from_cstr(const char *cstr) {
    if (cstr == NULL) {
        return str_make("", 0);
    }
    return str_make(cstr, strlen(cstr));
}

Str str_slice(Str s, size_t start, size_t end) {
    if (end > s.length) {
        end = s.length;
    }
    if (start > end) {
        start = end;
    }
    assert(end <= s.length);
    return str_make(s.data + start, end - start);
}

Str str_trim_any(Str s, Str chars) {
    size_t start = 0;
    size_t end = s.length;
    // Bounded by the length: each pass moves start or end one byte inwards.
    while (start < end && str_find_char(chars, s.data[start]) != STR_NOT_FOUND) {
        start++;
    }
    while (end > start && str_find_char(chars, s.data[end - 1]) != STR_NOT_FOUND) {
        end--;
    }
    return str_slice(s, start, end);
}

bool str_eq(Str a, Str b) {
    if (a.length != b.length) {
        return false;
    }
    if (a.length == 0) {
        return true;
    }
    return memcmp(a.data, b.data, a.length) == 0;
}

bool str_starts_with(Str s, Str prefix) {
    if (s.length < prefix.length) {
        return false;
    }
    return str_eq(str_slice(s, 0, prefix.length), prefix);
}

size_t str_find_char(Str s, char c) {
    if (s.length == 0) {
        return STR_NOT_FOUND;
    }
    const char *hit = memchr(s.data, c, s.length);
    if (hit == NULL) {
        return STR_NOT_FOUND;
    }
    return (size_t)(hit - s.data);
}

size_t str_find_not_any(Str s, Str chars) {
    for (size_t i = 0; i < s.length; i++) {
        if (str_find_char(chars, s.data[i]) == STR_NOT_FOUND) {
            return i;
        }
    }
    return STR_NOT_FOUND;
}

size_t str_find(Str haystack, Str needle) {
    if (needle.length == 0) {
        return 0;
    }
    size_t position = 0;
    // Bounded by the length: each pass moves position on by at least one byte.
    while (haystack.length - position >= needle.length) {
        // Jump to the next byte that could start the needle, then compare the rest.
        Str rest = str_slice(haystack, position, haystack.length - needle.length + 1);
        size_t hit = str_find_char(rest, needle.data[0]);
        if (hit == STR_NOT_FOUND) {
            return STR_NOT_FOUND;
        }
        position += hit;
        if (memcmp(haystack.data + position, needle.data, needle.length) == 0) {
            return position;
        }
        position++;
    }
    return STR_NOT_FOUND;
}

bool str_split_once(Str s, Str sep, Str *before, Str *after) {
    assert(before != NULL);
    assert(after != NULL);
    size_t position = str_find(s, sep);
    if (position == STR_NOT_FOUND) {
        return false;
    }
    *before = str_slice(s, 0, position);
    *after = str_slice(s, position + sep.length, s.length);
    return true;
}

bool str_to_u64(Str s, uint64_t *out) {
    assert(out != NULL);
    if (s.length == 0) {
        return false;
    }
    uint64_t number = 0;
    for (size_t i = 0; i < s.length; i++) {
        const char c = s.data[i];
        if (c < '0' || c > '9') {
            return false;
        }
        const uint64_t digit = (uint64_t)(c - '0');
        // Checked before the multiplication, which would wrap around.
        if (number > (UINT64_MAX - digit) / 10) {
            return false;
        }
        number = number * 10 + digit;
    }
    *out = number;
    return true;
}

bool str_to_cstr(Str s, char *buffer, size_t capacity) {
    assert(buffer != NULL);
    if (s.length >= capacity) {
        return false;
    }
    if (str_find_char(s, '\0') != STR_NOT_FOUND) {
        return false;
    }
    if (s.length > 0) {
        memcpy(buffer, s.data, s.length);
    }
    buffer[s.length] = '\0';
    return true;
}

bool str_copy_truncated(Str s, char *buffer, size_t capacity) {
    assert(buffer != NULL);
    assert(capacity >= 1);
    // A NUL byte ends the text where it is, as it would for any C string.
    const size_t nul = str_find_char(s, '\0');
    bool complete = nul == STR_NOT_FOUND;
    if (!complete) {
        s = str_slice(s, 0, nul);
    }
    const size_t length = utf8_length_at_most(s, capacity - 1);
    if (length < s.length) {
        complete = false;
    }
    if (length > 0) {
        memcpy(buffer, s.data, length);
    }
    buffer[length] = '\0';
    return complete;
}

void str_builder_init(Str_Builder *builder, char *buffer, size_t capacity) {
    assert(builder != NULL);
    assert(buffer != NULL || capacity == 0);
    builder->data = buffer;
    builder->length = 0;
    builder->capacity = capacity;
    builder->overflow = false;
}

bool str_builder_append(Str_Builder *builder, Str s) {
    assert(builder != NULL);
    assert(builder->length <= builder->capacity);
    if (s.length > builder->capacity - builder->length) {
        builder->overflow = true;
        return false;
    }
    if (s.length > 0) {
        memcpy(builder->data + builder->length, s.data, s.length);
    }
    builder->length += s.length;
    return true;
}

Str str_builder_str(const Str_Builder *builder) {
    return str_make(builder->data, builder->length);
}

/**
 * How many of the first bytes of s, at most length_max, make up whole UTF-8 characters: a cut
 * through the middle of one is moved back to where it starts.
 */
static size_t utf8_length_at_most(Str s, size_t length_max) {
    if (s.length <= length_max) {
        return s.length;
    }
    size_t length = length_max;
    // A continuation byte, 10xxxxxx, belongs to the character before it. At most three of them
    // follow a start byte, so this moves back a few bytes at most.
    while (length > 0 && ((unsigned char)s.data[length] & 0xC0) == 0x80) {
        length--;
    }
    return length;
}
