#ifndef DISCOVERY_H
#define DISCOVERY_H

#include <stdbool.h>
#include <stdint.h>

#include "net.h"

enum {
    DISCOVERY_ERROR_SIZE = 160,
};

// Finds a desktop app on the local network, so that nobody has to type its address on a phone.
// Broadcasts a probe every second while polled; the first desktop app to answer is found.
typedef struct Discovery {
    int64_t probe_sent_ms;  // When the last probe went out, or 0
    NetSocket socket;
    uint16_t found_port;
    bool open;
    bool found;
    bool net_started;
    char found_address[NET_ADDRESS_SIZE];
    char error[DISCOVERY_ERROR_SIZE];
} Discovery;

bool discovery_start(Discovery *discovery);
void discovery_stop(Discovery *discovery);
// Sends a probe when one is due and reads answers. Returns true once a desktop app was found;
// found_address and found_port then say where.
bool discovery_poll(Discovery *discovery);

#endif
