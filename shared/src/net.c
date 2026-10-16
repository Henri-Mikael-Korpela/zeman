#include "net.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET PlatformSocket;
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
typedef int PlatformSocket;
#endif

#if defined(MSG_NOSIGNAL)
// A send to a closed connection must fail, not kill the process with SIGPIPE.
#define NET_SEND_FLAGS MSG_NOSIGNAL
#else
#define NET_SEND_FLAGS 0
#endif

static bool net_set_nonblocking(PlatformSocket socket);
static bool net_ipv4_is_local(uint32_t host_order);
static bool net_would_block(void);
static bool net_wait(NetSocket socket, bool for_write, int32_t timeout_ms);
static void net_error_text(const char *context, char *error, size_t error_size);

bool net_startup(void) {
#if defined(_WIN32)
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
    return true;
#endif
}

void net_cleanup(void) {
#if defined(_WIN32)
    WSACleanup();
#endif
}

bool net_connect(const char *host, uint16_t port, int32_t timeout_ms, NetSocket *socket_out,
                 char *error, size_t error_size) {
    assert(host != NULL);
    assert(timeout_ms > 0);
    *socket_out = NET_SOCKET_INVALID;

    char port_text[8];
    snprintf(port_text, sizeof(port_text), "%u", (unsigned)port);
    struct addrinfo hints = { 0 };
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *address = NULL;
    if (getaddrinfo(host, port_text, &hints, &address) != 0 || address == NULL) {
        snprintf(error, error_size, "Unknown address \"%s\".", host);
        return false;
    }

    PlatformSocket platform = socket(address->ai_family, address->ai_socktype, 0);
    NetSocket result = (NetSocket)platform;
    bool ok = platform != (PlatformSocket)-1 && net_set_nonblocking(platform);
    if (ok) {
        // Non-blocking, so that a desktop that is off cannot freeze the app beyond the timeout.
        int status = connect(platform, address->ai_addr, (int)address->ai_addrlen);
#if defined(_WIN32)
        bool pending = status != 0 && WSAGetLastError() == WSAEWOULDBLOCK;
#else
        bool pending = status != 0 && errno == EINPROGRESS;
#endif
        ok = status == 0 || (pending && net_wait(result, true, timeout_ms));
    }
    if (ok) {
        int socket_error = 0;
        socklen_t length = sizeof(socket_error);
        getsockopt(platform, SOL_SOCKET, SO_ERROR, (char *)&socket_error, &length);
        ok = socket_error == 0;
    }
    freeaddrinfo(address);
    if (!ok) {
        snprintf(error, error_size, "Cannot connect to %s:%u.", host, (unsigned)port);
        net_close(result);
        return false;
    }
    // Requests are small and answered at once, so do not wait to fill packets.
    int no_delay = 1;
    setsockopt(platform, IPPROTO_TCP, TCP_NODELAY, (const char *)&no_delay, sizeof(no_delay));
    *socket_out = result;
    return true;
}

bool net_listen(uint16_t port, NetSocket *socket_out, char *error, size_t error_size) {
    *socket_out = NET_SOCKET_INVALID;
    PlatformSocket platform = socket(AF_INET, SOCK_STREAM, 0);
    if (platform == (PlatformSocket)-1) {
        net_error_text("Cannot create socket", error, error_size);
        return false;
    }
#if !defined(_WIN32)
    // Lets the desktop app restart at once; on Windows the same option would let another
    // program take the port, so it is left off there.
    int reuse = 1;
    setsockopt(platform, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
    struct sockaddr_in address = { 0 };
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    // Every interface, so that a phone on the same network can connect.
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    bool ok = bind(platform, (struct sockaddr *)&address, sizeof(address)) == 0;
    ok = ok && listen(platform, 8) == 0 && net_set_nonblocking(platform);
    if (!ok) {
        net_error_text("Cannot listen for the phone", error, error_size);
        net_close((NetSocket)platform);
        return false;
    }
    *socket_out = (NetSocket)platform;
    return true;
}

NetSocket net_accept(NetSocket listener, bool *peer_is_local, char *peer_address,
                     size_t peer_address_size) {
    struct sockaddr_in peer = { 0 };
    socklen_t peer_length = sizeof(peer);
    *peer_is_local = false;
    snprintf(peer_address, peer_address_size, "?");
    PlatformSocket platform = accept((PlatformSocket)listener, (struct sockaddr *)&peer,
                                     &peer_length);
    if (platform == (PlatformSocket)-1) {
        return NET_SOCKET_INVALID;
    }
    *peer_is_local = peer.sin_family == AF_INET && net_ipv4_is_local(ntohl(peer.sin_addr.s_addr));
    if (peer.sin_family == AF_INET) {
        inet_ntop(AF_INET, &peer.sin_addr, peer_address, (socklen_t)peer_address_size);
    }
    if (!net_set_nonblocking(platform)) {
        net_close((NetSocket)platform);
        return NET_SOCKET_INVALID;
    }
    int no_delay = 1;
    setsockopt(platform, IPPROTO_TCP, TCP_NODELAY, (const char *)&no_delay, sizeof(no_delay));
    return (NetSocket)platform;
}

int32_t net_send(NetSocket socket, const void *data, int32_t length) {
    assert(length > 0);
    int result = send((PlatformSocket)socket, (const char *)data, length, NET_SEND_FLAGS);
    if (result > 0) {
        return result;
    }
    return result < 0 && net_would_block() ? 0 : -1;
}

int32_t net_recv(NetSocket socket, void *data, int32_t length) {
    assert(length > 0);
    int result = recv((PlatformSocket)socket, (char *)data, length, 0);
    if (result > 0) {
        return result;
    }
    // 0 is an orderly close by the other side, which ends the connection like an error.
    return result < 0 && net_would_block() ? 0 : -1;
}

bool net_send_all(NetSocket socket, const void *data, int32_t length, int32_t timeout_ms) {
    const uint8_t *bytes = data;
    int64_t deadline = net_monotonic_ms() + timeout_ms;
    int32_t sent = 0;
    // Bounded by the deadline: every pass either moves bytes or waits until it.
    while (sent < length) {
        int32_t result = net_send(socket, bytes + sent, length - sent);
        if (result < 0) {
            return false;
        }
        sent += result;
        int64_t left_ms = deadline - net_monotonic_ms();
        if (result == 0) {
            if (left_ms <= 0 || !net_wait(socket, true, (int32_t)left_ms)) {
                return false;
            }
        }
    }
    return true;
}

bool net_recv_all(NetSocket socket, void *data, int32_t length, int32_t timeout_ms) {
    uint8_t *bytes = data;
    int64_t deadline = net_monotonic_ms() + timeout_ms;
    int32_t received = 0;
    // Bounded by the deadline, as in net_send_all.
    while (received < length) {
        int32_t result = net_recv(socket, bytes + received, length - received);
        if (result < 0) {
            return false;
        }
        received += result;
        int64_t left_ms = deadline - net_monotonic_ms();
        if (result == 0) {
            if (left_ms <= 0 || !net_wait(socket, false, (int32_t)left_ms)) {
                return false;
            }
        }
    }
    return true;
}

void net_close(NetSocket socket) {
    if (socket == NET_SOCKET_INVALID) {
        return;
    }
#if defined(_WIN32)
    closesocket((PlatformSocket)socket);
#else
    close((PlatformSocket)socket);
#endif
}

bool net_udp_open(uint16_t port, NetSocket *socket_out, char *error, size_t error_size) {
    *socket_out = NET_SOCKET_INVALID;
    PlatformSocket platform = socket(AF_INET, SOCK_DGRAM, 0);
    if (platform == (PlatformSocket)-1) {
        net_error_text("Cannot create socket", error, error_size);
        return false;
    }
    int enabled = 1;
    setsockopt(platform, SOL_SOCKET, SO_BROADCAST, (const char *)&enabled, sizeof(enabled));
    struct sockaddr_in address = { 0 };
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    bool ok = bind(platform, (struct sockaddr *)&address, sizeof(address)) == 0;
    ok = ok && net_set_nonblocking(platform);
    if (!ok) {
        net_error_text("Cannot open the discovery port", error, error_size);
        net_close((NetSocket)platform);
        return false;
    }
    *socket_out = (NetSocket)platform;
    return true;
}

bool net_udp_send(NetSocket socket, const char *address, uint16_t port, const void *data,
                  int32_t length) {
    assert(length > 0);
    struct sockaddr_in target = { 0 };
    target.sin_family = AF_INET;
    target.sin_port = htons(port);
    if (inet_pton(AF_INET, address, &target.sin_addr) != 1) {
        return false;
    }
    int sent = sendto((PlatformSocket)socket, (const char *)data, length, 0,
                      (struct sockaddr *)&target, sizeof(target));
    return sent == length;
}

int32_t net_udp_recv(NetSocket socket, void *data, int32_t size, char *address,
                     size_t address_size, uint16_t *port) {
    assert(size > 0);
    struct sockaddr_in from = { 0 };
    socklen_t from_length = sizeof(from);
    int received = recvfrom((PlatformSocket)socket, (char *)data, size, 0,
                            (struct sockaddr *)&from, &from_length);
    if (received <= 0) {
        return 0;
    }
    if (inet_ntop(AF_INET, &from.sin_addr, address, (socklen_t)address_size) == NULL) {
        return 0;
    }
    *port = ntohs(from.sin_port);
    return received;
}

bool net_address_is_local(const char *address) {
    struct in_addr parsed;
    if (inet_pton(AF_INET, address, &parsed) != 1) {
        return false;
    }
    return net_ipv4_is_local(ntohl(parsed.s_addr));
}

int64_t net_monotonic_ms(void) {
#if defined(_WIN32)
    return (int64_t)GetTickCount64();
#else
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
#endif
}

void net_local_address(char *buffer, size_t buffer_size) {
    snprintf(buffer, buffer_size, "127.0.0.1");
    // Connecting a UDP socket sends nothing; it only makes the system pick the interface that
    // would reach the internet, which is the one on the local network.
    PlatformSocket platform = socket(AF_INET, SOCK_DGRAM, 0);
    if (platform == (PlatformSocket)-1) {
        return;
    }
    struct sockaddr_in target = { 0 };
    target.sin_family = AF_INET;
    target.sin_port = htons(53);
    inet_pton(AF_INET, "8.8.8.8", &target.sin_addr);
    if (connect(platform, (struct sockaddr *)&target, sizeof(target)) == 0) {
        struct sockaddr_in local = { 0 };
        socklen_t length = sizeof(local);
        if (getsockname(platform, (struct sockaddr *)&local, &length) == 0) {
            char text[NET_ADDRESS_SIZE];
            if (inet_ntop(AF_INET, &local.sin_addr, text, sizeof(text)) != NULL) {
                snprintf(buffer, buffer_size, "%s", text);
            }
        }
    }
    net_close((NetSocket)platform);
}

static bool net_set_nonblocking(PlatformSocket socket) {
#if defined(_WIN32)
    u_long enabled = 1;
    return ioctlsocket(socket, FIONBIO, &enabled) == 0;
#else
    int flags = fcntl(socket, F_GETFL, 0);
    return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

static bool net_ipv4_is_local(uint32_t host_order) {
    uint32_t first = host_order >> 24;
    uint32_t second = (host_order >> 16) & 0xFF;
    if (first == 10 || first == 127) {
        return true;
    }
    if (first == 172 && second >= 16 && second <= 31) {
        return true;
    }
    return (first == 192 && second == 168) || (first == 169 && second == 254);
}

static bool net_would_block(void) {
#if defined(_WIN32)
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK;
#endif
}

// Waits until the socket can be written or read, or the timeout passes.
static bool net_wait(NetSocket socket, bool for_write, int32_t timeout_ms) {
    assert(timeout_ms >= 0);
    fd_set set;
    FD_ZERO(&set);
    FD_SET((PlatformSocket)socket, &set);
    struct timeval timeout = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
    // The first argument is ignored by Winsock and needed by POSIX.
    int count = select((int)((PlatformSocket)socket + 1), for_write ? NULL : &set,
                       for_write ? &set : NULL, NULL, &timeout);
    return count > 0;
}

static void net_error_text(const char *context, char *error, size_t error_size) {
#if defined(_WIN32)
    snprintf(error, error_size, "%s (error %d).", context, WSAGetLastError());
#else
    snprintf(error, error_size, "%s: %s.", context, strerror(errno));
#endif
}
