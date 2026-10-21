#include <zeman/protocol.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void writer_bytes(Writer *writer, const void *bytes, size_t count);
static bool reader_bytes(Reader *reader, void *bytes, size_t count);

Writer writer_make(uint8_t *buffer, size_t size) {
    assert(buffer != NULL);
    assert(size > 0);
    return (Writer){ .buffer = buffer, .size = size, .length = 0, .overflow = false };
}

void writer_u8(Writer *writer, uint8_t value) {
    writer_bytes(writer, &value, 1);
}

void writer_u32(Writer *writer, uint32_t value) {
    uint8_t bytes[4] = { 0 };
    protocol_length_write(bytes, value);
    writer_bytes(writer, bytes, sizeof(bytes));
}

void writer_i32(Writer *writer, int32_t value) {
    writer_u32(writer, (uint32_t)value);
}

void writer_u64(Writer *writer, uint64_t value) {
    writer_u32(writer, (uint32_t)(value & 0xFFFFFFFFu));
    writer_u32(writer, (uint32_t)(value >> 32));
}

void writer_i64(Writer *writer, int64_t value) {
    writer_u64(writer, (uint64_t)value);
}

void writer_text(Writer *writer, const char *text) {
    size_t length = strlen(text);
    assert(length <= UINT16_MAX);
    uint8_t prefix[2] = { (uint8_t)(length & 0xFF), (uint8_t)(length >> 8) };
    writer_bytes(writer, prefix, sizeof(prefix));
    writer_bytes(writer, text, length);
}

void writer_date(Writer *writer, Date date) {
    writer_i32(writer, date.year);
    writer_i32(writer, date.month);
    writer_i32(writer, date.day);
}

/**
 * Every field, in declaration order; reader_event reads them back in the same order.
 */
void writer_event(Writer *writer, const Event *event) {
    writer_i64(writer, event->id);
    writer_i64(writer, event->location.id);
    writer_text(writer, event->location.address);
    writer_text(writer, event->location.city);
    writer_date(writer, event->start_date);
    writer_date(writer, event->end_date);
    // A new event has no creation time yet; zeroes stand for it.
    Date created_date = { 0, 0, 0 };
    if (date_is_valid(event->created_date)) {
        created_date = event->created_date;
    }
    writer_date(writer, created_date);
    // Zeroes also stand for a repeat that never ends.
    Date repeat_end_date = { 0, 0, 0 };
    if (event->repeat_has_end) {
        repeat_end_date = event->repeat_end_date;
    }
    writer_date(writer, repeat_end_date);
    writer_i32(writer, event->start_minute);
    writer_i32(writer, event->end_minute);
    writer_i32(writer, event->created_minute);
    writer_i32(writer, event->repeat_unit);
    writer_i32(writer, event->repeat_interval);
    writer_i32(writer, event->kind);
    writer_u8(writer, (uint8_t)event->repeat_has_end);
    writer_u8(writer, (uint8_t)event->tasks_done);
    writer_text(writer, event->title);
    writer_text(writer, event->description);
}

void writer_completion(Writer *writer, const Task_Completion *completion) {
    writer_i64(writer, completion->event_id);
    writer_i32(writer, completion->occurrence_start_day);
}

void writer_linked_task(Writer *writer, const Linked_Task *task) {
    writer_i64(writer, task->task_id);
    writer_u8(writer, (uint8_t)task->done);
    writer_text(writer, task->title);
}

/**
 * Every field, in declaration order; reader_link_target reads them back in the same order.
 */
void writer_link_target(Writer *writer, const Link_Target *target) {
    writer_i64(writer, target->target_id);
    writer_date(writer, target->end_date);
    writer_i32(writer, target->end_minute);
    writer_i32(writer, target->task_count);
    writer_i32(writer, target->task_done_count);
    writer_i32(writer, target->kind);
    writer_u8(writer, (uint8_t)target->linked);
    writer_text(writer, target->title);
}

Reader reader_make(const uint8_t *buffer, size_t length) {
    assert(buffer != NULL);
    return (Reader){ .buffer = buffer, .length = length, .position = 0, .failed = false };
}

uint8_t reader_u8(Reader *reader) {
    uint8_t value = 0;
    reader_bytes(reader, &value, 1);
    return value;
}

uint32_t reader_u32(Reader *reader) {
    uint8_t bytes[4] = { 0, 0, 0, 0 };
    reader_bytes(reader, bytes, sizeof(bytes));
    return protocol_length_read(bytes);
}

int32_t reader_i32(Reader *reader) {
    return (int32_t)reader_u32(reader);
}

uint64_t reader_u64(Reader *reader) {
    uint64_t low = reader_u32(reader);
    uint64_t high = reader_u32(reader);
    return low | (high << 32);
}

int64_t reader_i64(Reader *reader) {
    return (int64_t)reader_u64(reader);
}

void reader_text(Reader *reader, char *text, size_t text_size) {
    assert(text_size > 0);
    text[0] = '\0';
    uint8_t prefix[2] = { 0, 0 };
    if (!reader_bytes(reader, prefix, sizeof(prefix))) {
        return;
    }
    size_t length = (size_t)prefix[0] | ((size_t)prefix[1] << 8);
    if (length >= text_size) {
        reader->failed = true;
        return;
    }
    if (reader_bytes(reader, text, length)) {
        text[length] = '\0';
        // An embedded terminator would make the text shorter than it claims to be.
        if (strlen(text) != length) {
            reader->failed = true;
        }
    }
}

Date reader_date(Reader *reader) {
    Date date = { 0, 0, 0 };
    date.year = reader_i32(reader);
    date.month = reader_i32(reader);
    date.day = reader_i32(reader);
    if (!date_is_valid(date)) {
        reader->failed = true;
        return (Date){ DATE_YEAR_MIN, 1, 1 };
    }
    return date;
}

Event reader_event(Reader *reader) {
    Event event = { 0 };
    event.id = reader_i64(reader);
    event.location.id = reader_i64(reader);
    reader_text(reader, event.location.address, sizeof(event.location.address));
    reader_text(reader, event.location.city, sizeof(event.location.city));
    event.start_date = reader_date(reader);
    event.end_date = reader_date(reader);
    // Zeroes mean not yet created, and no repeat end; anything else must be a real date.
    Date created = { reader_i32(reader), reader_i32(reader), reader_i32(reader) };
    Date repeat_end = { reader_i32(reader), reader_i32(reader), reader_i32(reader) };
    event.start_minute = reader_i32(reader);
    event.end_minute = reader_i32(reader);
    event.created_minute = reader_i32(reader);
    event.repeat_unit = reader_i32(reader);
    event.repeat_interval = reader_i32(reader);
    event.kind = reader_i32(reader);
    event.repeat_has_end = reader_u8(reader) != 0;
    // event_is_valid below refuses it on anything but a link target.
    event.tasks_done = reader_u8(reader) != 0;
    reader_text(reader, event.title, sizeof(event.title));
    reader_text(reader, event.description, sizeof(event.description));

    if (date_is_valid(created)) {
        event.created_date = created;
    } else if (created.year != 0 || created.month != 0 || created.day != 0) {
        reader->failed = true;
    }
    if (event.repeat_has_end) {
        event.repeat_end_date = repeat_end;
    }
    if (!reader->failed && !event_is_valid(&event)) {
        reader->failed = true;
    }
    if (reader->failed) {
        return (Event){ 0 };
    }
    return event;
}

Task_Completion reader_completion(Reader *reader) {
    Task_Completion completion = { 0 };
    completion.event_id = reader_i64(reader);
    completion.occurrence_start_day = reader_i32(reader);
    return completion;
}

Linked_Task reader_linked_task(Reader *reader) {
    Linked_Task task = { 0 };
    task.task_id = reader_i64(reader);
    uint8_t done = reader_u8(reader);
    reader_text(reader, task.title, sizeof(task.title));
    task.done = done == 1;
    if (task.task_id <= 0 || done > 1 || task.title[0] == '\0') {
        reader->failed = true;
    }
    if (reader->failed) {
        return (Linked_Task){ 0 };
    }
    return task;
}

/**
 * The counts, kind and deadline are checked as target_status, event_kind_is_target and
 * opportunity_is_missed assert them, because they come from the network.
 */
Link_Target reader_link_target(Reader *reader) {
    Link_Target target = { 0 };
    target.target_id = reader_i64(reader);
    target.end_date = reader_date(reader);
    target.end_minute = reader_i32(reader);
    target.task_count = reader_i32(reader);
    target.task_done_count = reader_i32(reader);
    target.kind = reader_i32(reader);
    uint8_t linked = reader_u8(reader);
    reader_text(reader, target.title, sizeof(target.title));
    target.linked = linked == 1;
    if (target.target_id <= 0 || linked > 1 || target.title[0] == '\0') {
        reader->failed = true;
    }
    if (target.task_done_count < 0 || target.task_done_count > target.task_count) {
        reader->failed = true;
    }
    if (target.kind != EVENT_KIND_PROBLEM && target.kind != EVENT_KIND_OPPORTUNITY) {
        reader->failed = true;
    }
    bool minute_valid = target.end_minute >= 0 && target.end_minute < MINUTES_PER_DAY;
    if (target.end_minute != TIME_NONE && !minute_valid) {
        reader->failed = true;
    }
    if (reader->failed) {
        return (Link_Target){ 0 };
    }
    return target;
}

bool reader_done(const Reader *reader) {
    return !reader->failed && reader->position == reader->length;
}

void protocol_length_write(uint8_t *bytes, uint32_t length) {
    bytes[0] = (uint8_t)(length & 0xFF);
    bytes[1] = (uint8_t)((length >> 8) & 0xFF);
    bytes[2] = (uint8_t)((length >> 16) & 0xFF);
    bytes[3] = (uint8_t)((length >> 24) & 0xFF);
}

uint32_t protocol_length_read(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static void writer_bytes(Writer *writer, const void *bytes, size_t count) {
    if (writer->overflow || count > writer->size - writer->length) {
        writer->overflow = true;
        return;
    }
    memcpy(writer->buffer + writer->length, bytes, count);
    writer->length += count;
}

static bool reader_bytes(Reader *reader, void *bytes, size_t count) {
    if (reader->failed || count > reader->length - reader->position) {
        reader->failed = true;
        memset(bytes, 0, count);
        return false;
    }
    memcpy(bytes, reader->buffer + reader->position, count);
    reader->position += count;
    return true;
}

void protocol_link_format(char *link, size_t link_size, const char *address, uint16_t port) {
    int written = snprintf(link, link_size, "%s%s:%u", PROTOCOL_LINK_PREFIX, address,
                           (unsigned)port);
    assert(written > 0);
    assert((size_t)written < link_size);
}

bool protocol_link_parse(const char *link, char *address, size_t address_size, uint16_t *port) {
    assert(address_size > 0);
    const size_t prefix_length = strlen(PROTOCOL_LINK_PREFIX);
    if (strncmp(link, PROTOCOL_LINK_PREFIX, prefix_length) == 0) {
        link += prefix_length;
    }
    // Only the characters of a numeric address and port, so that anything else in a scanned
    // code, such as a web address, is refused rather than half read.
    if (link[0] == '\0' || strspn(link, "0123456789.:") != strlen(link)) {
        return false;
    }
    const char *colon = strchr(link, ':');
    size_t address_length = strlen(link);
    if (colon != NULL) {
        address_length = (size_t)(colon - link);
    }
    if (address_length == 0 || address_length >= address_size) {
        return false;
    }
    *port = PROTOCOL_PORT_DEFAULT;
    if (colon != NULL) {
        char *end = NULL;
        long value = strtol(colon + 1, &end, 10);
        if (end == colon + 1 || *end != '\0' || value < 1 || value > UINT16_MAX) {
            return false;
        }
        *port = (uint16_t)value;
    }
    memcpy(address, link, address_length);
    address[address_length] = '\0';
    return true;
}
