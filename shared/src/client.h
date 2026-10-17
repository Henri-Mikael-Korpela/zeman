#ifndef CLIENT_H
#define CLIENT_H

#include <stdbool.h>
#include <stdint.h>

#include "backend.h"
#include "net.h"
#include "protocol.h"

enum {
    CLIENT_HOST_SIZE = 128,
    CLIENT_ERROR_SIZE = 160,
};

// A Backend that asks a desktop app over the network. Connects on first use and again after
// the connection drops, but at most every few seconds, so that a desktop that is off costs the
// user interface one short wait now and then rather than on every frame.
typedef struct Client {
    uint8_t request[PROTOCOL_LENGTH_SIZE + PROTOCOL_REQUEST_SIZE_MAX];
    uint8_t response[PROTOCOL_RESPONSE_SIZE_MAX];
    int64_t connect_attempt_ms;   // When the last connection attempt started, or 0
    int64_t revision_checked_ms;  // When the revision was last asked for
    uint64_t revision;            // The desktop's last reported revision, or 0 when unknown
    Net_Socket socket;
    uint16_t port;
    bool connected;
    bool net_started;
    char host[CLIENT_HOST_SIZE];
    char error[CLIENT_ERROR_SIZE];
} Client;

void client_init(Client *client);
void client_deinit(Client *client);
// Sets where the desktop app is and drops any connection to the previous one.
void client_set_server(Client *client, const char *host, uint16_t port);
bool client_is_connected(const Client *client);
Backend client_backend(Client *client);

#endif
