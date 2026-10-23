// Tests of the Str module. Built and run on request, see desktop/CMakeLists.txt:
//   cmake --build <build directory> --target str_test, then run str_test from it.
#include <zeman/str.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_make_and_slice(void) {
    assert(str_from_cstr(NULL).length == 0);
    assert(str_from_cstr("").length == 0);
    assert(str_from_cstr("abc").length == 3);
    assert(STR("abc").length == 3);

    const Str s = STR("hello");
    assert(str_eq(str_slice(s, 1, 3), STR("el")));
    assert(str_eq(str_slice(s, 0, 99), STR("hello"))); // Past the end is the end.
    assert(str_slice(s, 4, 2).length == 0);            // Start after end is empty.
    assert(str_slice(s, 5, 5).length == 0);
}

static void test_trim(void) {
    assert(str_eq(str_trim_any(STR("\r\n x \r\n"), STR(" \r\n")), STR("x")));
    assert(str_eq(str_trim_any(STR("\"path\""), STR("\"")), STR("path")));
}

static void test_compare(void) {
    assert(str_eq(STR(""), STR("")));
    assert(!str_eq(STR("a"), STR("b")));
    assert(!str_eq(STR("a"), STR("ab")));
    assert(str_starts_with(STR("zeman://1.2.3.4"), STR("zeman://")));
    assert(!str_starts_with(STR("zem"), STR("zeman")));
    assert(str_starts_with(STR("a"), STR("")));
}

static void test_find(void) {
    assert(str_find_char(STR("a:b"), ':') == 1);
    assert(str_find_char(STR("ab"), ':') == STR_NOT_FOUND);
    assert(str_find_char(STR(""), 'a') == STR_NOT_FOUND);
    assert(str_find_not_any(STR("12.5"), STR("0123456789")) == 2);
    assert(str_find_not_any(STR("123"), STR("0123456789")) == STR_NOT_FOUND);
    assert(str_find(STR("abcabd"), STR("abd")) == 3);
    assert(str_find(STR("abc"), STR("")) == 0);
    assert(str_find(STR("ab"), STR("abc")) == STR_NOT_FOUND);
    assert(str_find(STR("aaab"), STR("aab")) == 1);
}

static void test_split(void) {
    Str before = STR("unchanged");
    Str after = STR("unchanged");
    assert(str_split_once(STR("key=value=x"), STR("="), &before, &after));
    assert(str_eq(before, STR("key")));
    assert(str_eq(after, STR("value=x")));
    assert(str_split_once(STR("=v"), STR("="), &before, &after));
    assert(before.length == 0);
    assert(str_eq(after, STR("v")));
    before = STR("keep");
    after = STR("keep");
    assert(!str_split_once(STR("novalue"), STR("="), &before, &after));
    assert(str_eq(before, STR("keep")));
    assert(str_eq(after, STR("keep")));
}

static void test_numbers(void) {
    uint64_t number = 7;
    assert(str_to_u64(STR("0"), &number) && number == 0);
    assert(str_to_u64(STR("47800"), &number) && number == 47800);
    assert(str_to_u64(STR("18446744073709551615"), &number) && number == UINT64_MAX);
    number = 7;
    assert(!str_to_u64(STR("18446744073709551616"), &number)); // One too many.
    assert(!str_to_u64(STR("99999999999999999999999"), &number));
    assert(!str_to_u64(STR(""), &number));
    assert(!str_to_u64(STR("12a"), &number));
    assert(!str_to_u64(STR("-1"), &number));
    assert(!str_to_u64(STR(" 1"), &number));
    assert(number == 7); // Not changed by any failure.
}

static void test_cstr_boundaries(void) {
    char buffer[8] = "xxxxxxx";
    assert(str_to_cstr(STR("abc"), buffer, sizeof(buffer)));
    assert(strcmp(buffer, "abc") == 0);
    assert(str_to_cstr(STR("1234567"), buffer, sizeof(buffer)));      // Exactly fits.
    assert(!str_to_cstr(STR("12345678"), buffer, sizeof(buffer)));    // No room for the NUL.
    assert(strcmp(buffer, "1234567") == 0);                           // Unchanged.
    assert(!str_to_cstr(str_make("a\0b", 3), buffer, sizeof(buffer))); // Embedded NUL.

    // Truncating copy: text that fits, text that does not, and a cut inside a UTF-8 character.
    assert(str_copy_truncated(STR("abc"), buffer, sizeof(buffer)));
    assert(strcmp(buffer, "abc") == 0);
    assert(!str_copy_truncated(STR("abcdefghij"), buffer, sizeof(buffer)));
    assert(strcmp(buffer, "abcdefg") == 0);
    // Six letters and a two-byte character need eight bytes, but seven fit in this buffer: the
    // character is left out whole, not cut in two.
    assert(!str_copy_truncated(STR("aaaaaa\xC3\xA4z"), buffer, sizeof(buffer)));
    assert(strcmp(buffer, "aaaaaa") == 0);
    // With room for exactly those eight bytes it is kept, and the "z" after it is the cut.
    char wider[9] = "";
    assert(!str_copy_truncated(STR("aaaaaa\xC3\xA4z"), wider, sizeof(wider)));
    assert(strcmp(wider, "aaaaaa\xC3\xA4") == 0);
    assert(!str_copy_truncated(STR("aaaaa\xC3\xA4z"), buffer, 7)); // Six bytes: five letters.
    assert(strcmp(buffer, "aaaaa") == 0);
    assert(!str_copy_truncated(str_make("ab\0cd", 5), buffer, sizeof(buffer)));
    assert(strcmp(buffer, "ab") == 0);                               // Ends at the NUL.
    assert(str_copy_truncated(STR(""), buffer, 1));
    assert(buffer[0] == '\0');
}

static void test_builder(void) {
    char buffer[10] = "";
    Str_Builder builder = { 0 };
    str_builder_init(&builder, buffer, sizeof(buffer));
    assert(str_builder_append(&builder, STR("ab")));
    assert(str_builder_append(&builder, STR("cdef")));
    assert(str_eq(str_builder_str(&builder), STR("abcdef")));
    assert(builder.length == 6);
    assert(!str_builder_append(&builder, STR("wxyz1"))); // Would reach 11 of 10.
    assert(builder.overflow);
    assert(str_eq(str_builder_str(&builder), STR("abcdef"))); // Nothing was written.

    str_builder_init(&builder, buffer, sizeof(buffer));
    assert(str_builder_append(&builder, STR("1234567890"))); // Fills the buffer exactly.
    assert(!builder.overflow);
    assert(builder.length == 10);
}

int main(void) {
    test_make_and_slice();
    test_trim();
    test_compare();
    test_find();
    test_split();
    test_numbers();
    test_cstr_boundaries();
    test_builder();
    printf("str: all tests passed\n");
    return 0;
}
