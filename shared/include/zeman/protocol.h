#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zeman/event.h>

// The conversation between the Android app (client) and the desktop app (server).
//
// Every message is a 4-byte little-endian payload length followed by the payload. A request's
// payload is an operation byte and its arguments; the response's payload is a status byte,
// then the result on success or an error text on failure. The client sends one request and
// waits for its response before sending the next.
//
// Integers are little-endian; a text is a 16-bit length followed by its bytes, without a
// terminator; a date is its year, month and day as 32-bit integers.

enum {
    // Raised whenever a message changes shape, so that old apps say so instead of misreading.
    // 2 added the links between problems and the tasks that solve them, 3 whether a problem is
    // done to each event, and 4 opportunities as link targets beside problems.
    PROTOCOL_VERSION = 4,
    PROTOCOL_PORT_DEFAULT = 47800,
    // UDP port where the desktop app answers phones looking for it on the local network.
    PROTOCOL_DISCOVERY_PORT = 47801,
    PROTOCOL_DISCOVERY_SIZE_MAX = 64,
    PROTOCOL_LENGTH_SIZE = 4,
    // The largest request is an event, well under this.
    PROTOCOL_REQUEST_SIZE_MAX = 4096,
    // Bounds what one response carries, so that both sides can use fixed buffers.
    PROTOCOL_EVENT_COUNT_MAX = 512,
    PROTOCOL_COMPLETION_COUNT_MAX = 1024,
    // The largest response, a full range of events, is about 250 KiB.
    PROTOCOL_RESPONSE_SIZE_MAX = 512 * 1024,
};

// Discovery: a phone broadcasts PROTOCOL_DISCOVERY_PROBE to PROTOCOL_DISCOVERY_PORT; each desktop
// app on the network answers the sender with PROTOCOL_DISCOVERY_REPLY followed by its TCP port
// in decimal, for example "ZEMAN-SERVER 1 47800". The "1" is PROTOCOL_VERSION.
#define PROTOCOL_DISCOVERY_PROBE "ZEMAN-DISCOVER 1"
#define PROTOCOL_DISCOVERY_REPLY "ZEMAN-SERVER 1 "

// What the desktop's QR code holds: where to connect, as "zeman://192.168.1.20:47800".
#define PROTOCOL_LINK_PREFIX "zeman://"
enum {
    // The prefix, "255.255.255.255:65535" and the terminator, rounded up.
    PROTOCOL_LINK_SIZE = 48,
};

typedef enum Protocol_Operation {
    PROTOCOL_HELLO = 1,          // version -> version
    PROTOCOL_REVISION,           // -> revision
    PROTOCOL_LOAD_RANGE,         // first, last, count_max -> truncated, count, events
    PROTOCOL_LOAD_COMPLETIONS,   // first, last, count_max -> truncated, count, completions
    PROTOCOL_INSERT,             // event -> id
    PROTOCOL_UPDATE,             // event ->
    PROTOCOL_DELETE,             // id ->
    PROTOCOL_SET_TASK_DONE,      // id, occurrence start, done ->
    PROTOCOL_LOCATION,           // address, city -> id
    PROTOCOL_LOAD_TARGET_TASKS,  // target id, count_max -> truncated, count, tasks
    // task id, problems only, count_max -> truncated, count, targets
    PROTOCOL_LOAD_TASK_TARGETS,
    PROTOCOL_SET_TASK_TARGETS,   // task id, count, target ids ->
    PROTOCOL_OPERATION_END,
} Protocol_Operation;

typedef enum Protocol_Status {
    PROTOCOL_OK = 0,
    PROTOCOL_FAILED = 1,
} Protocol_Status;

// Writes into a fixed buffer. Writing past the end sets overflow rather than writing, so that
// a message is built without checking every call, and checked once at the end.
typedef struct Writer {
    uint8_t *buffer;
    size_t size;
    size_t length;
    bool overflow;
} Writer;

// Reads from a fixed buffer. Reading past the end, or reading a value that breaks its rules,
// sets failed and yields zeroes, so that a message is read without checking every call.
typedef struct Reader {
    const uint8_t *buffer;
    size_t length;
    size_t position;
    bool failed;
} Reader;

Writer writer_make(uint8_t *buffer, size_t size);
void writer_u8(Writer *writer, uint8_t value);
void writer_u32(Writer *writer, uint32_t value);
void writer_i32(Writer *writer, int32_t value);
void writer_u64(Writer *writer, uint64_t value);
void writer_i64(Writer *writer, int64_t value);
void writer_text(Writer *writer, const char *text);
void writer_date(Writer *writer, Date date);
void writer_event(Writer *writer, const Event *event);
void writer_completion(Writer *writer, const Task_Completion *completion);
void writer_linked_task(Writer *writer, const Linked_Task *task);
void writer_link_target(Writer *writer, const Link_Target *target);

Reader reader_make(const uint8_t *buffer, size_t length);
uint8_t reader_u8(Reader *reader);
uint32_t reader_u32(Reader *reader);
int32_t reader_i32(Reader *reader);
uint64_t reader_u64(Reader *reader);
int64_t reader_i64(Reader *reader);
// Copies the text with a terminator; a text that does not fit fails the reader.
void reader_text(Reader *reader, char *text, size_t text_size);
// A date that is not a real date fails the reader.
Date reader_date(Reader *reader);
// An event that breaks event_is_valid fails the reader.
Event reader_event(Reader *reader);
Task_Completion reader_completion(Reader *reader);
// Links with no id or title, counts that do not add up, or a target of another kind than a
// problem or an opportunity, fail the reader.
Linked_Task reader_linked_task(Reader *reader);
Link_Target reader_link_target(Reader *reader);
// True when the whole message was read and nothing failed.
bool reader_done(const Reader *reader);

// Writes the link to the desktop app at address and port, as the QR code holds it.
void protocol_link_format(char *link, size_t link_size, const char *address, uint16_t port);
// Reads address and port from a link, or from "address" or "address:port" typed without the
// prefix, the port then being PROTOCOL_PORT_DEFAULT. Returns false for anything else; whether
// the address is local is left to the caller.
bool protocol_link_parse(const char *link, char *address, size_t address_size, uint16_t *port);

// Little-endian length prefix of a message.
void protocol_length_write(uint8_t *bytes, uint32_t length);
uint32_t protocol_length_read(const uint8_t *bytes);

#endif
