#ifndef SERVER_H
#define SERVER_H

#include <stdbool.h>
#include <stdint.h>

#include "backend.h"
#include "net.h"
#include "protocol.h"

enum {
    // Phones served at once. More are accepted and closed at once.
    SERVER_CLIENT_COUNT_MAX = 4,
    SERVER_ERROR_SIZE = 160,
    // Discovery packets answered per frame, so that a flood of them cannot stall the app.
    SERVER_DISCOVERY_PACKETS_PER_POLL = 8,
};

typedef struct ServerClient {
    uint8_t request[PROTOCOL_LENGTH_SIZE + PROTOCOL_REQUEST_SIZE_MAX];
    uint8_t response[PROTOCOL_LENGTH_SIZE + PROTOCOL_RESPONSE_SIZE_MAX];
    int32_t request_length;   // Bytes of the current request received so far
    int32_t response_length;  // Bytes of the response to send, 0 when none
    int32_t response_sent;
    int64_t connected_at;  // Seconds since 1970, when the phone connected
    NetSocket socket;
    bool active;
    char address[NET_ADDRESS_SIZE];  // The phone's address on the local network
} ServerClient;

// Serves the calendar to phones on the local network, from the desktop app's own backend, and
// answers phones looking for it there. Connections and probes from outside the local network
// are ignored.
// Runs on the user interface thread: server_poll does a bounded amount of non-blocking work
// each frame, so a slow phone never freezes the desktop app.
typedef struct Server {
    ServerClient clients[SERVER_CLIENT_COUNT_MAX];
    Event events[PROTOCOL_EVENT_COUNT_MAX];
    TaskCompletion completions[PROTOCOL_COMPLETION_COUNT_MAX];
    ProblemTask problem_tasks[PROBLEM_LINK_COUNT_MAX];
    TaskProblem task_problems[PROBLEM_LINK_COUNT_MAX];
    Backend *backend;
    NetSocket listener;
    NetSocket discovery;  // UDP, or NET_SOCKET_INVALID when the port was taken
    uint16_t port;
    bool listening;
    bool net_started;
    char address[NET_ADDRESS_SIZE];  // Where phones connect, for showing to the user
    char error[SERVER_ERROR_SIZE];
} Server;

// Returns false, with error set, when the port cannot be used; the desktop app works without.
bool server_start(Server *server, Backend *backend, uint16_t port);
void server_stop(Server *server);
void server_poll(Server *server);
int32_t server_client_count(const Server *server);

#endif
