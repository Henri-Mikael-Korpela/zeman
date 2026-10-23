// Tests of the texts that cross the network and the text boxes: the length-prefixed text of the
// protocol, the link to the desktop app, and pasting into a text box.
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <zeman/protocol.h>
#include <zeman/str.h>
#include <zeman/text_box.h>

static void test_text_round_trip(void) {
    uint8_t bytes[64] = { 0 };
    Writer writer = writer_make(array_slice_make(bytes, (int32_t)sizeof(bytes)));
    writer_text(&writer, STR("Dentist"));
    writer_text(&writer, STR(""));
    assert(!writer.overflow);
    assert(writer.length == 2 + 7 + 2);

    Reader reader = reader_make(array_slice_make(bytes, (int32_t)writer.length));
    const Str first = reader_str(&reader);
    assert(str_eq(first, STR("Dentist")));
    // The view points into the message itself: no copy was made.
    assert((const void *)first.data == (const void *)(bytes + 2));
    const Str second = reader_str(&reader);
    assert(second.length == 0);
    assert(reader_done(&reader));

    // The copying reader reads the same bytes.
    Reader again = reader_make(array_slice_make(bytes, (int32_t)writer.length));
    char copy[16] = "";
    reader_text(&again, copy, sizeof(copy));
    assert(strcmp(copy, "Dentist") == 0);
    assert(!again.failed);
}

static void test_text_rejects_bad_messages(void) {
    // A length that goes past the end of the message.
    uint8_t cut[4] = { 9, 0, 'a', 'b' };
    Reader reader = reader_make(array_slice_make(cut, (int32_t)sizeof(cut)));
    assert(reader_str(&reader).length == 0);
    assert(reader.failed);

    // A terminator inside the text would make it shorter than it claims to be.
    uint8_t embedded[5] = { 3, 0, 'a', 0, 'b' };
    reader = reader_make(array_slice_make(embedded, (int32_t)sizeof(embedded)));
    assert(reader_str(&reader).length == 0);
    assert(reader.failed);

    // Not even the two length bytes.
    uint8_t one[1] = { 1 };
    reader = reader_make(array_slice_make(one, (int32_t)sizeof(one)));
    assert(reader_str(&reader).length == 0);
    assert(reader.failed);

    // A text too long for the field fails the copying reader, as before.
    uint8_t long_text[6] = { 4, 0, 'a', 'b', 'c', 'd' };
    reader = reader_make(array_slice_make(long_text, (int32_t)sizeof(long_text)));
    char small[4] = "";
    reader_text(&reader, small, sizeof(small));
    assert(reader.failed);
}

static void test_link_parse(void) {
    Str address = str_make("", 0);
    uint16_t port = 0;
    assert(protocol_link_parse(STR("192.168.1.20"), &address, &port));
    assert(str_eq(address, STR("192.168.1.20")));
    assert(port == PROTOCOL_PORT_DEFAULT);

    assert(protocol_link_parse(STR("192.168.1.20:5000"), &address, &port));
    assert(str_eq(address, STR("192.168.1.20")));
    assert(port == 5000);

    // The full link as the QR code holds it.
    char link[PROTOCOL_LINK_SIZE] = "";
    protocol_link_format(link, sizeof(link), STR("10.0.0.7"), 47801);
    assert(protocol_link_parse(str_from_cstr(link), &address, &port));
    assert(str_eq(address, STR("10.0.0.7")));
    assert(port == 47801);

    assert(!protocol_link_parse(STR(""), &address, &port));
    assert(!protocol_link_parse(STR("example.com"), &address, &port));
    assert(!protocol_link_parse(STR("1.2.3.4:"), &address, &port));
    assert(!protocol_link_parse(STR("1.2.3.4:0"), &address, &port));
    assert(!protocol_link_parse(STR("1.2.3.4:65536"), &address, &port));
    assert(!protocol_link_parse(STR("1.2.3.4:5:6"), &address, &port));
    assert(!protocol_link_parse(STR(":5000"), &address, &port));
}

static void test_paste(void) {
    char text[16] = "ab";
    // Line breaks and tabs become spaces, other control characters are left out.
    int32_t cursor = text_box_paste(text, (int32_t)sizeof(text), 1, STR("x\ny\t\r\x01z"));
    assert(strcmp(text, "ax y zb") == 0);
    assert(cursor == 6);

    // What does not fit is cut, and a character is never cut in half.
    char tight[6] = "";
    cursor = text_box_paste(tight, (int32_t)sizeof(tight), 0, STR("abc\xC3\xA4\xC3\xA4"));
    assert(strcmp(tight, "abc\xC3\xA4") == 0);
    assert(cursor == 5);

    // Broken UTF-8 is dropped, and a sequence cut short by the end of the text is too.
    char broken[16] = "";
    cursor = text_box_paste(broken, (int32_t)sizeof(broken), 0, STR("a\x80" "b\xE2\x82"));
    assert(strcmp(broken, "ab") == 0);
    assert(cursor == 2);

    // An embedded terminator is a control character like the others.
    char embedded[16] = "";
    cursor = text_box_paste(embedded, (int32_t)sizeof(embedded), 0, str_make("a\0b", 3));
    assert(strcmp(embedded, "ab") == 0);
    assert(cursor == 2);
}

int main(void) {
    test_text_round_trip();
    test_text_rejects_bad_messages();
    test_link_parse();
    test_paste();
    printf("wire_test: all passed\n");
    return 0;
}
