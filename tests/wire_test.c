// Tests of the texts that cross the network and the text boxes: the length-prefixed text of the
// protocol, the link to the desktop app, and pasting into a text box. Also which occurrence of a
// task is due, and the task as it crosses the network for the Tasks view.
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
    Str const first = reader_str(&reader);
    assert(str_eq(first, STR("Dentist")));
    // The view points into the message itself: no copy was made.
    assert((void const *)first.data == (void const *)(bytes + 2));
    Str const second = reader_str(&reader);
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

static Event test_task_make(Date start, int32_t repeat_unit, int32_t repeat_interval) {
    Event task = {
        .id = 7,
        .start_date = start,
        .end_date = start,
        .created_date = start,
        .start_minute = TIME_NONE,
        .end_minute = TIME_NONE,
        .repeat_unit = repeat_unit,
        .repeat_interval = repeat_interval,
        .kind = EVENT_KIND_TASK,
        .title = "Water the plants",
    };
    assert(event_is_valid(&task));
    return task;
}

static int32_t test_day(int32_t year, int32_t month, int32_t day) {
    return date_to_day_number((Date){ year, month, day });
}

static void test_occurrence_due(void) {
    // A one-time task is due on its day, before and after it.
    Event once = test_task_make((Date){ 2026, 10, 20 }, REPEAT_NONE, 1);
    assert(event_occurrence_due(&once, test_day(2026, 10, 1)) == test_day(2026, 10, 20));
    assert(event_occurrence_due(&once, test_day(2026, 10, 28)) == test_day(2026, 10, 20));

    // Every three days from 1 October: 28 October is one, and from 29 October the next is the
    // 31st, the missed one passed over.
    Event every_three = test_task_make((Date){ 2026, 10, 1 }, REPEAT_DAY, 3);
    assert(event_occurrence_due(&every_three, test_day(2026, 10, 28)) == test_day(2026, 10, 28));
    assert(event_occurrence_due(&every_three, test_day(2026, 10, 29)) == test_day(2026, 10, 31));

    // With the repeat ended on 20 October, the last occurrence, the 19th, stays due.
    every_three.repeat_has_end = true;
    every_three.repeat_end_date = (Date){ 2026, 10, 20 };
    assert(event_occurrence_due(&every_three, test_day(2026, 10, 29)) == test_day(2026, 10, 19));

    // Monthly on the 31st skips months without one: after 31 October comes 31 December.
    Event monthly = test_task_make((Date){ 2026, 1, 31 }, REPEAT_MONTH, 1);
    assert(event_occurrence_due(&monthly, test_day(2026, 11, 5)) == test_day(2026, 12, 31));

    // Yearly on 29 February waits for the next leap year.
    Event leap = test_task_make((Date){ 2024, 2, 29 }, REPEAT_YEAR, 1);
    assert(event_occurrence_due(&leap, test_day(2026, 10, 28)) == test_day(2028, 2, 29));
}

static void test_task_occurrence_round_trip(void) {
    static uint8_t bytes[1024];
    Task_Occurrence occurrence = {
        .task = test_task_make((Date){ 2026, 10, 1 }, REPEAT_WEEK, 1),
        .start_day = test_day(2026, 10, 29),
        .done = true,
    };
    Writer writer = writer_make(array_slice_make(bytes, (int32_t)sizeof(bytes)));
    writer_task_occurrence(&writer, &occurrence);
    assert(!writer.overflow);

    Reader reader = reader_make(array_slice_make(bytes, (int32_t)writer.length));
    Task_Occurrence read = reader_task_occurrence(&reader);
    assert(reader_done(&reader));
    assert(read.task.id == occurrence.task.id);
    assert(read.start_day == occurrence.start_day);
    assert(read.done);

    // A day that is not one of its weekly occurrences is refused.
    occurrence.start_day++;
    writer = writer_make(array_slice_make(bytes, (int32_t)sizeof(bytes)));
    writer_task_occurrence(&writer, &occurrence);
    reader = reader_make(array_slice_make(bytes, (int32_t)writer.length));
    read = reader_task_occurrence(&reader);
    assert(reader.failed);
    assert(read.task.id == 0);
}

static Event test_event_round_trip(Event const *event, bool *accepted) {
    static uint8_t bytes[1024];
    Writer writer = writer_make(array_slice_make(bytes, (int32_t)sizeof(bytes)));
    writer_event(&writer, event);
    assert(!writer.overflow);

    Reader reader = reader_make(array_slice_make(bytes, (int32_t)writer.length));
    Event read = reader_event(&reader);
    *accepted = !reader.failed && reader_done(&reader);
    return read;
}

static void test_deadline(void) {
    bool accepted = false;
    Event task = test_task_make((Date){ 2026, 10, 20 }, REPEAT_NONE, 1);
    task.has_deadline = true;
    task.deadline_date = (Date){ 2026, 10, 25 };
    task.deadline_minute = 17 * 60;
    assert(event_is_valid(&task));

    // A deadline crosses the network with its time, or without one.
    Event read = test_event_round_trip(&task, &accepted);
    assert(accepted);
    assert(read.has_deadline);
    assert(date_equal(read.deadline_date, task.deadline_date));
    assert(read.deadline_minute == 17 * 60);
    task.deadline_minute = TIME_NONE;
    read = test_event_round_trip(&task, &accepted);
    assert(accepted);
    assert(read.deadline_minute == TIME_NONE);

    // A task without one stays without, whatever its deadline fields hold.
    Event none = test_task_make((Date){ 2026, 10, 20 }, REPEAT_NONE, 1);
    none.deadline_date = (Date){ 2026, 10, 25 };
    read = test_event_round_trip(&none, &accepted);
    assert(accepted);
    assert(!read.has_deadline);

    // The deadline can be the start day, but not before it.
    task.deadline_date = task.start_date;
    assert(event_is_valid(&task));
    task.deadline_date = (Date){ 2026, 10, 19 };
    assert(!event_is_valid(&task));
    test_event_round_trip(&task, &accepted);
    assert(!accepted);

    // Only a task that does not repeat has one.
    task.deadline_date = (Date){ 2026, 10, 25 };
    task.repeat_unit = REPEAT_WEEK;
    assert(!event_is_valid(&task));
    task.repeat_unit = REPEAT_NONE;
    task.kind = EVENT_KIND_EVENT;
    assert(!event_is_valid(&task));
    task.kind = EVENT_KIND_TASK;
    task.deadline_minute = MINUTES_PER_DAY;
    assert(!event_is_valid(&task));
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
    test_occurrence_due();
    test_task_occurrence_round_trip();
    test_deadline();
    test_paste();
    printf("wire_test: all passed\n");
    return 0;
}
