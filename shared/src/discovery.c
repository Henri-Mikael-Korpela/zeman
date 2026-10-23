#include <zeman/discovery.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zeman/protocol.h>

enum {
    DISCOVERY_PROBE_INTERVAL_MS = 1000,
    // Answers read per poll, so that a busy network cannot stall the app.
    DISCOVERY_ANSWERS_PER_POLL = 8,
};

static bool discovery_parse_answer(Str answer, uint16_t *port);

bool discovery_start(Discovery *discovery) {
    *discovery = (Discovery){ .socket = NET_SOCKET_INVALID };
    discovery->net_started = net_startup();
    if (!discovery->net_started) {
        snprintf(discovery->error, sizeof(discovery->error), "Networking is not available.");
        return false;
    }
    // Any free port: the desktop app answers whichever port the probe came from.
    if (!net_udp_open(0, &discovery->socket, discovery->error, sizeof(discovery->error))) {
        return false;
    }
    discovery->open = true;
    return true;
}

void discovery_stop(Discovery *discovery) {
    net_close(discovery->socket);
    discovery->socket = NET_SOCKET_INVALID;
    discovery->open = false;
    if (discovery->net_started) {
        net_cleanup();
        discovery->net_started = false;
    }
}

bool discovery_poll(Discovery *discovery) {
    if (!discovery->open || discovery->found) {
        return discovery->found;
    }
    int64_t now = net_monotonic_ms();
    if (discovery->probe_sent_ms == 0 ||
        now - discovery->probe_sent_ms >= DISCOVERY_PROBE_INTERVAL_MS) {
        discovery->probe_sent_ms = now;
        char probe[] = PROTOCOL_DISCOVERY_PROBE;
        // The limited broadcast address reaches every device on this network and no further.
        net_udp_send(discovery->socket, "255.255.255.255", PROTOCOL_DISCOVERY_PORT,
                     array_slice_make(probe, (int32_t)strlen(probe)));
    }

    for (int32_t i = 0; i < DISCOVERY_ANSWERS_PER_POLL; i++) {
        char answer[PROTOCOL_DISCOVERY_SIZE_MAX] = "";
        char address[NET_ADDRESS_SIZE] = "";
        uint16_t from_port = 0;
        // One byte is kept for the terminator that makes the answer a string.
        int32_t length = net_udp_recv(discovery->socket,
                                      array_slice_make(answer, (int32_t)sizeof(answer) - 1),
                                      address, sizeof(address), &from_port);
        if (length == 0) {
            break;
        }
        uint16_t port = 0;
        // The datagram has a length of its own, so it needs no terminator to be read.
        const Str answer_text = str_make(answer, (size_t)length);
        if (net_address_is_local(address) && discovery_parse_answer(answer_text, &port)) {
            snprintf(discovery->found_address, sizeof(discovery->found_address), "%s", address);
            discovery->found_port = port;
            discovery->found = true;
            break;
        }
    }
    return discovery->found;
}

/**
 * "ZEMAN-SERVER 1 47800": the reply prefix, then a port of digits only.
 */
static bool discovery_parse_answer(Str answer, uint16_t *port) {
    const Str prefix = str_from_cstr(PROTOCOL_DISCOVERY_REPLY);
    if (!str_starts_with(answer, prefix)) {
        return false;
    }
    // str_to_u64 takes digits only, so a sign, a space or an empty port are all refused.
    uint64_t value = 0;
    if (!str_to_u64(str_slice(answer, prefix.length, answer.length), &value)) {
        return false;
    }
    if (value < 1 || value > UINT16_MAX) {
        return false;
    }
    *port = (uint16_t)value;
    return true;
}
