// Tests of the rich text module. Built and run on request, see desktop/CMakeLists.txt:
//   cmake --build <build directory> --target rich_text_test, then run rich_text_test from it.
#include <zeman/rich_text.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

static Str link_text(Str text, size_t from) {
    Rich_Text_Link const link = rich_text_link_find(text, from);
    if (link.start == STR_NOT_FOUND) {
        return STR("");
    }
    return str_slice(text, link.start, link.end);
}

static void test_links(void) {
    assert(str_eq(link_text(STR("See https://example.com/a?b=1."), 0),
                  STR("https://example.com/a?b=1")));
    assert(str_eq(link_text(STR("www.example.com, then"), 0), STR("www.example.com")));
    assert(str_eq(link_text(STR("HTTP://EXAMPLE.COM"), 0), STR("HTTP://EXAMPLE.COM")));
    assert(str_eq(link_text(STR("(see http://a.fi/x)"), 0), STR("http://a.fi/x")));
    assert(str_eq(link_text(STR("https://en.wikipedia.org/wiki/C_(language)"), 0),
                  STR("https://en.wikipedia.org/wiki/C_(language)")));
    assert(str_eq(link_text(STR("\"https://a.fi\" and"), 0), STR("https://a.fi")));
    assert(str_eq(link_text(STR("https://a.fi'; rm"), 0), STR("https://a.fi")));
    // Not at the start of a word, or nothing after the prefix.
    assert(str_eq(link_text(STR("xwww.a.fi a.http://b"), 0), STR("")));
    assert(str_eq(link_text(STR("www. and https://"), 0), STR("")));
    assert(str_eq(link_text(STR("Ends at a line\nwww.a.fi\nnext"), 0), STR("www.a.fi")));
    // An image marker ends a link.
    assert(str_eq(link_text(STR("www.a.fi" RICH_TEXT_IMAGE_MARKER "b"), 0), STR("www.a.fi")));
    // Non-ASCII letters belong to it.
    assert(str_eq(link_text(STR("https://fi.wikipedia.org/wiki/P\xC3\xA4iv\xC3\xA4"), 0),
                  STR("https://fi.wikipedia.org/wiki/P\xC3\xA4iv\xC3\xA4")));
    // From a later position, the next link.
    Str const two = STR("www.a.fi and www.b.fi");
    assert(str_eq(link_text(two, 1), STR("www.b.fi")));
    assert(rich_text_link_at(two, 3).start == 0);
    assert(rich_text_link_at(two, 9).start == STR_NOT_FOUND);
    assert(rich_text_link_at(two, 14).start == 13);

    char url[RICH_TEXT_URL_SIZE] = "";
    assert(rich_text_link_url(STR("www.a.fi"), url, sizeof(url)));
    assert(strcmp(url, "https://www.a.fi") == 0);
    assert(rich_text_link_url(STR("http://a.fi"), url, sizeof(url)));
    assert(strcmp(url, "http://a.fi") == 0);
    char small[8] = "";
    assert(!rich_text_link_url(STR("http://a.fi"), small, sizeof(small)));
    assert(small[0] == '\0');
}

static void test_images(void) {
    Str const text = STR("a" RICH_TEXT_IMAGE_MARKER "b" RICH_TEXT_IMAGE_MARKER);
    assert(rich_text_image_count(text) == 2);
    assert(rich_text_image_index_at(text, 0) == 0);
    assert(rich_text_image_index_at(text, 1) == 0);
    assert(rich_text_image_index_at(text, 2) == 1);
    assert(rich_text_image_index_at(text, 5) == 1);
    assert(rich_text_image_index_at(text, 6) == 2);
    assert(rich_text_is_image_marker(text, 1));
    assert(!rich_text_is_image_marker(text, 2));
    assert(!rich_text_is_image_marker(STR("\xEF\xBF"), 0)); // Cut short.
}

static void test_characters(void) {
    Str const text = STR("a\xC3\xA4" RICH_TEXT_IMAGE_MARKER "\x80z");
    assert(rich_text_character_next(text, 0) == 1);
    assert(rich_text_character_next(text, 1) == 3);
    assert(rich_text_character_next(text, 3) == 6);
    assert(rich_text_character_next(text, 6) == 7); // A stray continuation byte is one.
    assert(rich_text_character_previous(text, 8) == 7);
    assert(rich_text_character_previous(text, 7) == 6);
    assert(rich_text_character_previous(text, 6) == 3);
    assert(rich_text_character_previous(text, 3) == 1);
    assert(rich_text_character_previous(text, 1) == 0);

    int32_t size = 0;
    assert(rich_text_codepoint(text, 1, &size) == 0xE4);
    assert(size == 2);
    assert(rich_text_codepoint(text, 3, &size) == 0xFFFC);
    assert(size == 3);
    assert(rich_text_codepoint(text, 6, &size) == 0xFFFD);
    assert(size == 1);

    // Each size of encoding decodes back to the same code point.
    int32_t const codepoints[] = { 'z', 0xE4, 0x20AC, 0xFFFC, 0x1F600 };
    int32_t const sizes[] = { 1, 2, 3, 3, 4 };
    for (size_t i = 0; i < sizeof(codepoints) / sizeof(codepoints[0]); i++) {
        char bytes[4] = { 0 };
        int32_t const length = rich_text_codepoint_encode(codepoints[i], bytes);
        assert(length == sizes[i]);
        assert(rich_text_codepoint(str_make(bytes, (size_t)length), 0, &size) == codepoints[i]);
        assert(size == length);
    }
}

static void test_clean(void) {
    char target[32] = "";
    Str const pasted = STR("a\r\nb\tc" RICH_TEXT_IMAGE_MARKER "\x01\xFF\xC3\xA4");
    size_t length = rich_text_clean(pasted, target, sizeof(target));
    assert(str_eq(str_make(target, length), STR("a\nb c\xC3\xA4")));
    // A character that does not fit whole is left out.
    length = rich_text_clean(STR("ab\xC3\xA4"), target, 3);
    assert(str_eq(str_make(target, length), STR("ab")));
}

static void test_replace(void) {
    char text[8] = "abcd";
    size_t length = 4;
    assert(rich_text_replace(text, &length, sizeof(text), 1, 3, STR("XYZ")));
    assert(length == 5);
    assert(strcmp(text, "aXYZd") == 0);
    assert(rich_text_replace(text, &length, sizeof(text), 5, 5, STR("!!")));
    assert(strcmp(text, "aXYZd!!") == 0);
    // One more byte would leave no room for the terminator.
    assert(!rich_text_replace(text, &length, sizeof(text), 0, 0, STR("?")));
    assert(strcmp(text, "aXYZd!!") == 0);
    assert(rich_text_replace(text, &length, sizeof(text), 0, 7, STR("")));
    assert(length == 0);
    assert(text[0] == '\0');
}

int main(void) {
    test_links();
    test_images();
    test_characters();
    test_clean();
    test_replace();
    printf("rich_text_test: all tests passed.\n");
    return 0;
}
