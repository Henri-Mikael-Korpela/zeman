#include "server.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

enum {
    // Ranges asked for by phones are kept to plausible calendar views, which also keeps every
    // date the store derives from them, such as a year before first, within valid dates.
    SERVER_YEAR_MIN = 1900,
    SERVER_YEAR_MAX = 9000,
    SERVER_RANGE_DAYS_MAX = 400,
};

static void server_discovery_poll(Server *server);
static void server_client_poll(Server *server, ServerClient *client);
static bool server_client_flush(ServerClient *client);
static void server_client_drop(ServerClient *client);
static void server_handle(Server *server, Reader *request, Writer *response);
static void server_handle_load_range(Server *server, Reader *request, Writer *response);
static void server_handle_load_completions(Server *server, Reader *request, Writer *response);
static void server_handle_insert(Server *server, Reader *request, Writer *response);
static void server_handle_update(Server *server, Reader *request, Writer *response);
static void server_handle_set_task_done(Server *server, Reader *request, Writer *response);
static void server_handle_location(Server *server, Reader *request, Writer *response);
static bool server_range_is_valid(Date first, Date last);
static void server_fail(Writer *response, const char *message);

bool server_start(Server *server, Backend *backend, uint16_t port) {
    assert(backend != NULL);
    server->backend = backend;
    server->listener = NET_SOCKET_INVALID;
    server->discovery = NET_SOCKET_INVALID;
    server->port = port;
    server->listening = false;
    server->error[0] = '\0';
    for (int32_t i = 0; i < SERVER_CLIENT_COUNT_MAX; i++) {
        server->clients[i].active = false;
        server->clients[i].socket = NET_SOCKET_INVALID;
    }
    server->net_started = net_startup();
    if (!server->net_started) {
        snprintf(server->error, sizeof(server->error), "Networking is not available.");
        return false;
    }
    if (!net_listen(port, &server->listener, server->error, sizeof(server->error))) {
        return false;
    }
    net_local_address(server->address, sizeof(server->address));
    server->listening = true;
    // Without discovery, phones can still connect by typing the address.
    char discovery_error[SERVER_ERROR_SIZE];
    if (!net_udp_open(PROTOCOL_DISCOVERY_PORT, &server->discovery, discovery_error,
                      sizeof(discovery_error))) {
        server->discovery = NET_SOCKET_INVALID;
    }
    return true;
}

void server_stop(Server *server) {
    for (int32_t i = 0; i < SERVER_CLIENT_COUNT_MAX; i++) {
        if (server->clients[i].active) {
            server_client_drop(&server->clients[i]);
        }
    }
    net_close(server->listener);
    server->listener = NET_SOCKET_INVALID;
    net_close(server->discovery);
    server->discovery = NET_SOCKET_INVALID;
    server->listening = false;
    if (server->net_started) {
        net_cleanup();
        server->net_started = false;
    }
}

void server_poll(Server *server) {
    if (!server->listening) {
        return;
    }
    // Bounded by the client slots: at most one new connection per free slot per frame.
    for (int32_t i = 0; i < SERVER_CLIENT_COUNT_MAX; i++) {
        ServerClient *client = &server->clients[i];
        if (client->active) {
            continue;
        }
        bool peer_is_local = false;
        NetSocket socket = net_accept(server->listener, &peer_is_local);
        if (socket == NET_SOCKET_INVALID) {
            break;
        }
        if (!peer_is_local) {
            net_close(socket);
            continue;
        }
        client->socket = socket;
        client->request_length = 0;
        client->response_length = 0;
        client->response_sent = 0;
        client->active = true;
    }
    for (int32_t i = 0; i < SERVER_CLIENT_COUNT_MAX; i++) {
        server_client_poll(server, &server->clients[i]);
    }
    server_discovery_poll(server);
}

// Answers each probe from the local network with this server's port, at the sender's address.
static void server_discovery_poll(Server *server) {
    if (server->discovery == NET_SOCKET_INVALID) {
        return;
    }
    for (int32_t i = 0; i < SERVER_DISCOVERY_PACKETS_PER_POLL; i++) {
        char packet[PROTOCOL_DISCOVERY_SIZE_MAX];
        char address[NET_ADDRESS_SIZE];
        uint16_t port = 0;
        int32_t length = net_udp_recv(server->discovery, packet, sizeof(packet) - 1, address,
                                      sizeof(address), &port);
        if (length == 0) {
            return;
        }
        packet[length] = '\0';
        if (!net_address_is_local(address) || strcmp(packet, PROTOCOL_DISCOVERY_PROBE) != 0) {
            continue;
        }
        char reply[PROTOCOL_DISCOVERY_SIZE_MAX];
        int reply_length = snprintf(reply, sizeof(reply), "%s%u", PROTOCOL_DISCOVERY_REPLY,
                                    (unsigned)server->port);
        assert(reply_length > 0 && reply_length < (int)sizeof(reply));
        net_udp_send(server->discovery, address, port, reply, reply_length);
    }
}

int32_t server_client_count(const Server *server) {
    int32_t count = 0;
    for (int32_t i = 0; i < SERVER_CLIENT_COUNT_MAX; i++) {
        count += server->clients[i].active ? 1 : 0;
    }
    return count;
}

// Finishes sending the previous response, then reads at most one request and answers it.
static void server_client_poll(Server *server, ServerClient *client) {
    if (!client->active || !server_client_flush(client)) {
        return;
    }
    if (client->response_length > 0) {
        return; // Still sending; read the next request once the phone has taken this one.
    }

    if (client->request_length < PROTOCOL_LENGTH_SIZE) {
        int32_t received = net_recv(client->socket, client->request + client->request_length,
                                    PROTOCOL_LENGTH_SIZE - client->request_length);
        if (received < 0) {
            server_client_drop(client);
            return;
        }
        client->request_length += received;
        if (client->request_length < PROTOCOL_LENGTH_SIZE) {
            return;
        }
    }
    uint32_t payload_length = protocol_length_read(client->request);
    if (payload_length < 1 || payload_length > PROTOCOL_REQUEST_SIZE_MAX) {
        server_client_drop(client); // Not our protocol, or a broken phone app.
        return;
    }
    int32_t total = PROTOCOL_LENGTH_SIZE + (int32_t)payload_length;
    if (client->request_length < total) {
        int32_t received = net_recv(client->socket, client->request + client->request_length,
                                    total - client->request_length);
        if (received < 0) {
            server_client_drop(client);
            return;
        }
        client->request_length += received;
        if (client->request_length < total) {
            return;
        }
    }

    Reader request = reader_make(client->request + PROTOCOL_LENGTH_SIZE, payload_length);
    Writer response = writer_make(client->response + PROTOCOL_LENGTH_SIZE,
                                  PROTOCOL_RESPONSE_SIZE_MAX);
    server_handle(server, &request, &response);
    // The largest response, a full range of events, fits with room to spare.
    assert(!response.overflow);
    protocol_length_write(client->response, (uint32_t)response.length);
    client->response_length = PROTOCOL_LENGTH_SIZE + (int32_t)response.length;
    client->response_sent = 0;
    client->request_length = 0;
    server_client_flush(client);
}

// Sends what the socket takes now. Returns false if the connection was dropped.
static bool server_client_flush(ServerClient *client) {
    if (client->response_length == 0) {
        return true;
    }
    int32_t sent = net_send(client->socket, client->response + client->response_sent,
                            client->response_length - client->response_sent);
    if (sent < 0) {
        server_client_drop(client);
        return false;
    }
    client->response_sent += sent;
    if (client->response_sent == client->response_length) {
        client->response_length = 0;
        client->response_sent = 0;
    }
    return true;
}

static void server_client_drop(ServerClient *client) {
    net_close(client->socket);
    client->socket = NET_SOCKET_INVALID;
    client->active = false;
}

static void server_handle(Server *server, Reader *request, Writer *response) {
    uint8_t operation = reader_u8(request);
    switch (operation) {
    case PROTOCOL_HELLO: {
        uint32_t version = reader_u32(request);
        (void)version; // The client compares versions, and says so to its user.
        writer_u8(response, PROTOCOL_OK);
        writer_u32(response, PROTOCOL_VERSION);
        return;
    }
    case PROTOCOL_REVISION:
        if (!reader_done(request)) {
            server_fail(response, "Invalid request.");
            return;
        }
        writer_u8(response, PROTOCOL_OK);
        writer_u64(response, backend_revision(server->backend));
        return;
    case PROTOCOL_LOAD_RANGE:
        server_handle_load_range(server, request, response);
        return;
    case PROTOCOL_LOAD_COMPLETIONS:
        server_handle_load_completions(server, request, response);
        return;
    case PROTOCOL_INSERT:
        server_handle_insert(server, request, response);
        return;
    case PROTOCOL_UPDATE:
        server_handle_update(server, request, response);
        return;
    case PROTOCOL_DELETE: {
        int64_t id = reader_i64(request);
        if (!reader_done(request) || id <= 0) {
            server_fail(response, "Invalid request.");
            return;
        }
        if (!backend_remove(server->backend, id)) {
            server_fail(response, backend_error(server->backend));
            return;
        }
        writer_u8(response, PROTOCOL_OK);
        return;
    }
    case PROTOCOL_SET_TASK_DONE:
        server_handle_set_task_done(server, request, response);
        return;
    case PROTOCOL_LOCATION:
        server_handle_location(server, request, response);
        return;
    default:
        // The byte comes from the network, so values that are no operation must be answered.
        server_fail(response, "Unknown request: update the desktop app.");
        return;
    }
}

static void server_handle_load_range(Server *server, Reader *request, Writer *response) {
    Date first = reader_date(request);
    Date last = reader_date(request);
    int32_t count_max = reader_i32(request);
    if (!reader_done(request) || !server_range_is_valid(first, last) || count_max < 1) {
        server_fail(response, "Invalid request.");
        return;
    }
    if (count_max > PROTOCOL_EVENT_COUNT_MAX) {
        count_max = PROTOCOL_EVENT_COUNT_MAX;
    }
    bool truncated = false;
    int32_t count = backend_load_range(server->backend, first, last, server->events, count_max,
                                       &truncated);
    if (count < 0) {
        server_fail(response, backend_error(server->backend));
        return;
    }
    writer_u8(response, PROTOCOL_OK);
    writer_u8(response, truncated ? 1 : 0);
    writer_i32(response, count);
    for (int32_t i = 0; i < count; i++) {
        writer_event(response, &server->events[i]);
    }
}

static void server_handle_load_completions(Server *server, Reader *request, Writer *response) {
    Date first = reader_date(request);
    Date last = reader_date(request);
    int32_t count_max = reader_i32(request);
    if (!reader_done(request) || !server_range_is_valid(first, last) || count_max < 1) {
        server_fail(response, "Invalid request.");
        return;
    }
    if (count_max > PROTOCOL_COMPLETION_COUNT_MAX) {
        count_max = PROTOCOL_COMPLETION_COUNT_MAX;
    }
    bool truncated = false;
    int32_t count = backend_load_completions(server->backend, first, last, server->completions,
                                             count_max, &truncated);
    if (count < 0) {
        server_fail(response, backend_error(server->backend));
        return;
    }
    writer_u8(response, PROTOCOL_OK);
    writer_u8(response, truncated ? 1 : 0);
    writer_i32(response, count);
    for (int32_t i = 0; i < count; i++) {
        writer_completion(response, &server->completions[i]);
    }
}

// reader_event has checked the event against event_is_valid already.
static void server_handle_insert(Server *server, Reader *request, Writer *response) {
    Event event = reader_event(request);
    if (!reader_done(request) || event.id != 0) {
        server_fail(response, "Invalid event.");
        return;
    }
    // Problems fall on the day they are created, which is today on this computer.
    if (event.kind == EVENT_KIND_PROBLEM && !date_equal(event.start_date, date_today())) {
        server_fail(response, "A problem can only be added for today.");
        return;
    }
    int64_t id = backend_insert(server->backend, &event);
    if (id == 0) {
        server_fail(response, backend_error(server->backend));
        return;
    }
    writer_u8(response, PROTOCOL_OK);
    writer_i64(response, id);
}

static void server_handle_update(Server *server, Reader *request, Writer *response) {
    Event event = reader_event(request);
    if (!reader_done(request) || event.id <= 0) {
        server_fail(response, "Invalid event.");
        return;
    }
    if (!backend_update(server->backend, &event)) {
        server_fail(response, backend_error(server->backend));
        return;
    }
    writer_u8(response, PROTOCOL_OK);
}

static void server_handle_set_task_done(Server *server, Reader *request, Writer *response) {
    int64_t id = reader_i64(request);
    Date occurrence_start = reader_date(request);
    uint8_t done = reader_u8(request);
    if (!reader_done(request) || id <= 0 || done > 1) {
        server_fail(response, "Invalid request.");
        return;
    }
    if (!backend_set_task_done(server->backend, id, occurrence_start, done == 1)) {
        server_fail(response, backend_error(server->backend));
        return;
    }
    writer_u8(response, PROTOCOL_OK);
}

static void server_handle_location(Server *server, Reader *request, Writer *response) {
    char address[LOCATION_ADDRESS_SIZE];
    char city[LOCATION_CITY_SIZE];
    reader_text(request, address, sizeof(address));
    reader_text(request, city, sizeof(city));
    if (!reader_done(request) || (address[0] == '\0' && city[0] == '\0')) {
        server_fail(response, "Invalid location.");
        return;
    }
    int64_t id = backend_location_find_or_create(server->backend, address, city);
    if (id == 0) {
        server_fail(response, backend_error(server->backend));
        return;
    }
    writer_u8(response, PROTOCOL_OK);
    writer_i64(response, id);
}

static bool server_range_is_valid(Date first, Date last) {
    if (first.year < SERVER_YEAR_MIN || last.year > SERVER_YEAR_MAX) {
        return false;
    }
    int32_t days = date_to_day_number(last) - date_to_day_number(first);
    return days >= 0 && days <= SERVER_RANGE_DAYS_MAX;
}

// Replaces anything written so far with a failure and its message.
static void server_fail(Writer *response, const char *message) {
    response->length = 0;
    response->overflow = false;
    writer_u8(response, PROTOCOL_FAILED);
    writer_text(response, message);
}
