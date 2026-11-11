#include <zeman/byte.h>
#include <zeman/client.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

enum {
    CLIENT_CONNECT_TIMEOUT_MS = 1500,
    // Long enough for a full month of events, or an image, over a slow Wi-Fi network.
    CLIENT_IO_TIMEOUT_MS = 5000,
    CLIENT_RECONNECT_INTERVAL_MS = 3000,
    // How stale the desktop's revision may get before asking again, which is how quickly
    // changes made on the desktop appear on the phone.
    CLIENT_REVISION_INTERVAL_MS = 1000,
};

static bool client_connect(Client *client);
static void client_disconnect(Client *client, char const *reason);
static Writer client_request_begin(Client *client, Protocol_Operation operation);
static bool client_exchange(Client *client, Writer const *request, Reader *response);
static bool client_call(Client *client, Writer const *request, Reader *response);
static bool client_response_done(Client *client, Reader const *response);
static int32_t client_response_count(Client *client, Reader const *response, int32_t count);
static int64_t client_response_id(Client *client, Reader const *response, int64_t id);

static int32_t client_load_range(void *context, Date first, Date last, Array_Slice events,
                                 bool *truncated);
static int32_t client_load_completions(void *context, Date first, Date last,
                                       Array_Slice completions, bool *truncated);
static int64_t client_insert(void *context, Event const *event);
static bool client_update(void *context, Event const *event);
static bool client_remove(void *context, int64_t id);
static bool client_set_task_done(void *context, int64_t id, Date occurrence_start, bool done);
static int64_t client_location(void *context, Str address, Str city);
static int32_t client_load_target_tasks(void *context, int64_t target_id, Array_Slice tasks,
                                        bool *truncated);
static int32_t client_load_task_targets(void *context, int64_t task_id, bool problems_only,
                                        Array_Slice targets, bool *truncated);
static bool client_set_task_targets(void *context, int64_t task_id, Array_Slice target_ids);
static int32_t client_load_tasks(void *context, Date today, Array_Slice tasks, bool *truncated);
static int64_t client_image_insert(void *context, Array_Slice png_bytes);
static int32_t client_image_load(void *context, int64_t id, Array_Slice png_bytes);
static int32_t client_load_event_images(void *context, int64_t event_id, Array_Slice image_ids);
static bool client_set_event_images(void *context, int64_t event_id, Array_Slice image_ids);
static uint64_t client_revision(void *context);
static Str client_error(void const *context);

static Backend_Ops const CLIENT_OPS = {
    .load_range = client_load_range,
    .load_completions = client_load_completions,
    .insert = client_insert,
    .update = client_update,
    .remove = client_remove,
    .set_task_done = client_set_task_done,
    .location_find_or_create = client_location,
    .load_target_tasks = client_load_target_tasks,
    .load_task_targets = client_load_task_targets,
    .set_task_targets = client_set_task_targets,
    .load_tasks = client_load_tasks,
    .image_insert = client_image_insert,
    .image_load = client_image_load,
    .load_event_images = client_load_event_images,
    .set_event_images = client_set_event_images,
    .revision = client_revision,
    .error = client_error,
};

void client_init(Client *client) {
    *client = (Client){ .socket = NET_SOCKET_INVALID, .port = PROTOCOL_PORT_DEFAULT };
    client->net_started = net_startup();
    if (!client->net_started) {
        snprintf(client->error, sizeof(client->error), "Networking is not available.");
    }
}

void client_deinit(Client *client) {
    client_disconnect(client, "");
    if (client->net_started) {
        net_cleanup();
        client->net_started = false;
    }
}

void client_set_server(Client *client, Str host, uint16_t port) {
    assert(host.length > 0);

    client_disconnect(client, "");
    // A host that does not fit is cut, as the connect then fails with a message of its own.
    (void)str_copy_truncated(host, client->host, sizeof(client->host));
    client->port = port;
    // Connect at the next request rather than after the reconnect interval.
    client->connect_attempt_ms = 0;
    client->revision_checked_ms = 0;
}

bool client_is_connected(Client const *client) {
    return client->connected;
}

Backend client_backend(Client *client) {
    return (Backend){ .ops = &CLIENT_OPS, .context = client };
}

static int32_t client_load_range(void *context, Date first, Date last, Array_Slice events,
                                 bool *truncated) {
    Client *client = context;
    assert(events.count > 0);
    Event *event_items = events.items;
    Writer request = client_request_begin(client, PROTOCOL_LOAD_RANGE);
    writer_date(&request, first);
    writer_date(&request, last);
    writer_i32(&request, events.count);
    Reader response = { 0 };
    if (!client_call(client, &request, &response)) {
        return -1;
    }
    *truncated = reader_u8(&response) != 0;
    int32_t count = reader_i32(&response);
    if (count < 0 || count > events.count) {
        client_disconnect(client, "The desktop app sent an invalid answer.");
        return -1;
    }
    for (int32_t i = 0; i < count; i++) {
        event_items[i] = reader_event(&response);
    }
    return client_response_count(client, &response, count);
}

static int32_t client_load_completions(void *context, Date first, Date last,
                                       Array_Slice completions, bool *truncated) {
    Client *client = context;
    assert(completions.count > 0);
    Task_Completion *completion_items = completions.items;
    Writer request = client_request_begin(client, PROTOCOL_LOAD_COMPLETIONS);
    writer_date(&request, first);
    writer_date(&request, last);
    writer_i32(&request, completions.count);
    Reader response = { 0 };
    if (!client_call(client, &request, &response)) {
        return -1;
    }
    *truncated = reader_u8(&response) != 0;
    int32_t count = reader_i32(&response);
    if (count < 0 || count > completions.count) {
        client_disconnect(client, "The desktop app sent an invalid answer.");
        return -1;
    }
    for (int32_t i = 0; i < count; i++) {
        completion_items[i] = reader_completion(&response);
    }
    return client_response_count(client, &response, count);
}

static int64_t client_insert(void *context, Event const *event) {
    Client *client = context;
    Writer request = client_request_begin(client, PROTOCOL_INSERT);
    writer_event(&request, event);
    Reader response = { 0 };
    if (!client_call(client, &request, &response)) {
        return 0;
    }
    int64_t id = reader_i64(&response);
    return client_response_id(client, &response, id);
}

static bool client_update(void *context, Event const *event) {
    Client *client = context;
    Writer request = client_request_begin(client, PROTOCOL_UPDATE);
    writer_event(&request, event);
    Reader response = { 0 };
    return client_call(client, &request, &response) && client_response_done(client, &response);
}

static bool client_remove(void *context, int64_t id) {
    Client *client = context;
    Writer request = client_request_begin(client, PROTOCOL_DELETE);
    writer_i64(&request, id);
    Reader response = { 0 };
    return client_call(client, &request, &response) && client_response_done(client, &response);
}

static bool client_set_task_done(void *context, int64_t id, Date occurrence_start, bool done) {
    Client *client = context;
    Writer request = client_request_begin(client, PROTOCOL_SET_TASK_DONE);
    writer_i64(&request, id);
    writer_date(&request, occurrence_start);
    writer_u8(&request, (byte)done);
    Reader response = { 0 };
    return client_call(client, &request, &response) && client_response_done(client, &response);
}

static int64_t client_location(void *context, Str address, Str city) {
    Client *client = context;
    Writer request = client_request_begin(client, PROTOCOL_LOCATION);
    writer_text(&request, address);
    writer_text(&request, city);
    Reader response = { 0 };
    if (!client_call(client, &request, &response)) {
        return 0;
    }
    int64_t id = reader_i64(&response);
    return client_response_id(client, &response, id);
}

static int32_t client_load_target_tasks(void *context, int64_t target_id, Array_Slice tasks,
                                        bool *truncated) {
    Client *client = context;
    assert(target_id > 0);
    assert(tasks.count > 0);
    Linked_Task *task_items = tasks.items;
    Writer request = client_request_begin(client, PROTOCOL_LOAD_TARGET_TASKS);
    writer_i64(&request, target_id);
    writer_i32(&request, tasks.count);
    Reader response = { 0 };
    if (!client_call(client, &request, &response)) {
        return -1;
    }
    *truncated = reader_u8(&response) != 0;
    int32_t count = reader_i32(&response);
    if (count < 0 || count > tasks.count) {
        client_disconnect(client, "The desktop app sent an invalid answer.");
        return -1;
    }
    for (int32_t i = 0; i < count; i++) {
        task_items[i] = reader_linked_task(&response);
    }
    return client_response_count(client, &response, count);
}

static int32_t client_load_task_targets(void *context, int64_t task_id, bool problems_only,
                                        Array_Slice targets, bool *truncated) {
    Client *client = context;
    assert(task_id >= 0);
    assert(targets.count > 0);
    Link_Target *target_items = targets.items;
    Writer request = client_request_begin(client, PROTOCOL_LOAD_TASK_TARGETS);
    writer_i64(&request, task_id);
    writer_u8(&request, (byte)problems_only);
    writer_i32(&request, targets.count);
    Reader response = { 0 };
    if (!client_call(client, &request, &response)) {
        return -1;
    }
    *truncated = reader_u8(&response) != 0;
    int32_t count = reader_i32(&response);
    if (count < 0 || count > targets.count) {
        client_disconnect(client, "The desktop app sent an invalid answer.");
        return -1;
    }
    for (int32_t i = 0; i < count; i++) {
        target_items[i] = reader_link_target(&response);
        // Asked for problems alone, an opportunity is an answer to another question.
        if (problems_only && target_items[i].kind != EVENT_KIND_PROBLEM) {
            response.failed = true;
        }
    }
    return client_response_count(client, &response, count);
}

static bool client_set_task_targets(void *context, int64_t task_id, Array_Slice target_ids) {
    Client *client = context;
    assert(task_id > 0);
    assert(target_ids.count >= 0);
    assert(target_ids.count <= LINK_COUNT_MAX);
    int64_t const *ids = target_ids.items;
    Writer request = client_request_begin(client, PROTOCOL_SET_TASK_TARGETS);
    writer_i64(&request, task_id);
    writer_i32(&request, target_ids.count);
    for (int32_t i = 0; i < target_ids.count; i++) {
        writer_i64(&request, ids[i]);
    }
    Reader response = { 0 };
    return client_call(client, &request, &response) && client_response_done(client, &response);
}

static int32_t client_load_tasks(void *context, Date today, Array_Slice tasks, bool *truncated) {
    Client *client = context;
    assert(date_is_valid(today));
    assert(tasks.count > 0);
    Task_Occurrence *task_items = tasks.items;
    Writer request = client_request_begin(client, PROTOCOL_LOAD_TASKS);
    writer_date(&request, today);
    writer_i32(&request, tasks.count);
    Reader response = { 0 };
    if (!client_call(client, &request, &response)) {
        return -1;
    }
    *truncated = reader_u8(&response) != 0;
    int32_t count = reader_i32(&response);
    if (count < 0 || count > tasks.count) {
        client_disconnect(client, "The desktop app sent an invalid answer.");
        return -1;
    }
    for (int32_t i = 0; i < count; i++) {
        task_items[i] = reader_task_occurrence(&response);
    }
    return client_response_count(client, &response, count);
}

static int64_t client_image_insert(void *context, Array_Slice png_bytes) {
    Client *client = context;
    assert(description_image_is_valid(png_bytes));
    Writer request = client_request_begin(client, PROTOCOL_IMAGE_INSERT);
    writer_blob(&request, png_bytes);
    Reader response = { 0 };
    if (!client_call(client, &request, &response)) {
        return 0;
    }
    int64_t const id = reader_i64(&response);
    return client_response_id(client, &response, id);
}

/**
 * The bytes are copied out of the response, which the next request overwrites.
 */
static int32_t client_image_load(void *context, int64_t id, Array_Slice png_bytes) {
    Client *client = context;
    assert(id > 0);
    assert(png_bytes.count > 0);
    Writer request = client_request_begin(client, PROTOCOL_IMAGE_LOAD);
    writer_i64(&request, id);
    Reader response = { 0 };
    if (!client_call(client, &request, &response)) {
        return -1;
    }
    Array_Slice const blob = reader_blob(&response, png_bytes.count);
    if (!client_response_done(client, &response)) {
        return -1;
    }
    if (blob.count > 0) {
        memcpy(png_bytes.items, blob.items, (size_t)blob.count);
    }
    return blob.count;
}

static int32_t client_load_event_images(void *context, int64_t event_id, Array_Slice image_ids) {
    Client *client = context;
    assert(event_id > 0);
    assert(image_ids.count > 0);
    int64_t *ids = image_ids.items;
    Writer request = client_request_begin(client, PROTOCOL_LOAD_EVENT_IMAGES);
    writer_i64(&request, event_id);
    writer_i32(&request, image_ids.count);
    Reader response = { 0 };
    if (!client_call(client, &request, &response)) {
        return -1;
    }
    int32_t const count = reader_i32(&response);
    if (count < 0 || count > image_ids.count) {
        client_disconnect(client, "The desktop app sent an invalid answer.");
        return -1;
    }
    for (int32_t i = 0; i < count; i++) {
        ids[i] = reader_i64(&response);
        if (ids[i] <= 0) {
            response.failed = true;
        }
    }
    return client_response_count(client, &response, count);
}

static bool client_set_event_images(void *context, int64_t event_id, Array_Slice image_ids) {
    Client *client = context;
    assert(event_id > 0);
    assert(image_ids.count >= 0);
    assert(image_ids.count <= DESCRIPTION_IMAGE_COUNT_MAX);
    int64_t const *ids = image_ids.items;
    Writer request = client_request_begin(client, PROTOCOL_SET_EVENT_IMAGES);
    writer_i64(&request, event_id);
    writer_i32(&request, image_ids.count);
    for (int32_t i = 0; i < image_ids.count; i++) {
        writer_i64(&request, ids[i]);
    }
    Reader response = { 0 };
    return client_call(client, &request, &response) && client_response_done(client, &response);
}

/**
 * Asks the desktop at most every CLIENT_REVISION_INTERVAL_MS; between, the last answer stands.
 */
static uint64_t client_revision(void *context) {
    Client *client = context;
    int64_t now = net_monotonic_ms();
    if (client->revision_checked_ms != 0) {
        if (now - client->revision_checked_ms < CLIENT_REVISION_INTERVAL_MS) {
            return client->revision;
        }
    }
    client->revision_checked_ms = now;
    Writer request = client_request_begin(client, PROTOCOL_REVISION);
    Reader response = { 0 };
    if (!client_call(client, &request, &response)) {
        client->revision = 0;
        return 0;
    }
    uint64_t revision = reader_u64(&response);
    if (!client_response_done(client, &response)) {
        return 0;
    }
    client->revision = revision;
    return revision;
}

static Str client_error(void const *context) {
    Client const *client = context;
    return str_from_cstr(client->error);
}

static bool client_connect(Client *client) {
    assert(!client->connected);

    if (!client->net_started) {
        return false;
    }
    if (client->host[0] == '\0') {
        snprintf(client->error, sizeof(client->error), "No desktop address set.");
        return false;
    }
    // The phone and the desktop talk only within the local network.
    if (!net_address_is_local(client->host)) {
        snprintf(client->error, sizeof(client->error),
                 "Use the desktop's local network address, such as 192.168.1.20.");
        return false;
    }
    // Between attempts the last failure's message stands.
    int64_t now = net_monotonic_ms();
    if (client->connect_attempt_ms != 0) {
        if (now - client->connect_attempt_ms < CLIENT_RECONNECT_INTERVAL_MS) {
            return false;
        }
    }
    client->connect_attempt_ms = now;
    if (!net_connect(client->host, client->port, CLIENT_CONNECT_TIMEOUT_MS, &client->socket,
                     client->error, sizeof(client->error))) {
        return false;
    }
    client->connected = true;

    // In its own buffer: connecting happens inside a call whose request is already waiting in
    // client->request, and must not overwrite it.
    byte hello[PROTOCOL_LENGTH_SIZE + 8] = { 0 };
    Writer request = writer_make(
        array_slice_make(hello + PROTOCOL_LENGTH_SIZE,
                         (int32_t)sizeof(hello) - PROTOCOL_LENGTH_SIZE));
    writer_u8(&request, PROTOCOL_HELLO);
    writer_u32(&request, PROTOCOL_VERSION);
    Reader response = { 0 };
    if (!client_exchange(client, &request, &response)) {
        client_disconnect(client, "The desktop app did not answer.");
        return false;
    }
    uint32_t version = reader_u32(&response);
    if (!reader_done(&response) || version != PROTOCOL_VERSION) {
        client_disconnect(client, "The desktop app is a different version: update both.");
        return false;
    }
    client->error[0] = '\0';
    return true;
}

static void client_disconnect(Client *client, char const *reason) {
    net_close(client->socket);
    client->socket = NET_SOCKET_INVALID;
    client->connected = false;
    // Unknown until connected again; a new connection may reach a restarted desktop app.
    client->revision = 0;
    snprintf(client->error, sizeof(client->error), "%s", reason);
}

/**
 * The payload goes after room for the length prefix, which client_exchange fills in.
 */
static Writer client_request_begin(Client *client, Protocol_Operation operation) {
    assert(operation > 0);
    assert(operation < PROTOCOL_OPERATION_END);

    Writer request = writer_make(
        array_slice_make(client->request + PROTOCOL_LENGTH_SIZE, PROTOCOL_REQUEST_SIZE_MAX));
    writer_u8(&request, (byte)operation);
    return request;
}

/**
 * Sends the request and reads the response up to its result. Returns false, with the error set,
 * if the connection fails (and is closed) or the desktop app reports a failure (and stays
 * connected). Every request is written after PROTOCOL_LENGTH_SIZE free bytes, where the length
 * prefix goes.
 */
static bool client_exchange(Client *client, Writer const *request, Reader *response) {
    assert(client->connected);
    // Every request is far smaller than its buffer; overflowing it is a programmer error.
    assert(!request->overflow);

    byte *payload = request->buffer.items;
    byte *message = payload - PROTOCOL_LENGTH_SIZE;
    protocol_length_write(message, (uint32_t)request->length);
    int32_t message_size = (int32_t)(PROTOCOL_LENGTH_SIZE + request->length);
    if (!net_send_all(client->socket, array_slice_make(message, message_size),
                      CLIENT_IO_TIMEOUT_MS)) {
        client_disconnect(client, "The connection to the desktop app was lost.");
        return false;
    }
    byte prefix[PROTOCOL_LENGTH_SIZE] = { 0 };
    if (!net_recv_all(client->socket, array_slice_make(prefix, (int32_t)sizeof(prefix)),
                      CLIENT_IO_TIMEOUT_MS)) {
        client_disconnect(client, "The connection to the desktop app was lost.");
        return false;
    }
    uint32_t length = protocol_length_read(prefix);
    if (length < 1 || length > PROTOCOL_RESPONSE_SIZE_MAX) {
        client_disconnect(client, "The desktop app sent an invalid answer.");
        return false;
    }
    Array_Slice answer = array_slice_make(client->response, (int32_t)length);
    if (!net_recv_all(client->socket, answer, CLIENT_IO_TIMEOUT_MS)) {
        client_disconnect(client, "The connection to the desktop app was lost.");
        return false;
    }

    *response = reader_make(answer);
    if (reader_u8(response) != PROTOCOL_OK) {
        reader_text(response, client->error, sizeof(client->error));
        if (response->failed) {
            snprintf(client->error, sizeof(client->error), "The desktop app reported a failure.");
        }
        return false;
    }
    return true;
}

static bool client_call(Client *client, Writer const *request, Reader *response) {
    if (!client->connected && !client_connect(client)) {
        return false;
    }
    return client_exchange(client, request, response);
}

/**
 * A response with fields missing, left over or breaking their rules means the two apps do not
 * understand each other, so the connection is dropped rather than trusted.
 */
static bool client_response_done(Client *client, Reader const *response) {
    if (!reader_done(response)) {
        client_disconnect(client, "The desktop app sent an invalid answer.");
        return false;
    }
    return true;
}

/**
 * count, once the whole answer has been read as it should be, or -1 for a load that failed.
 */
static int32_t client_response_count(Client *client, Reader const *response, int32_t count) {
    assert(count >= 0);

    if (client_response_done(client, response)) {
        return count;
    }
    return -1;
}

/**
 * The id a write answered with, once the whole answer has been read as it should be, or 0 for
 * one that failed or came back without a stored id.
 */
static int64_t client_response_id(Client *client, Reader const *response, int64_t id) {
    if (!client_response_done(client, response)) {
        return 0;
    }
    if (id > 0) {
        return id;
    }
    return 0;
}
