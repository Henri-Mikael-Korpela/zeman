#ifndef NET_H
#define NET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// TCP sockets over Winsock on Windows and BSD sockets elsewhere (Android), behind one small
// interface. Only net.c includes the platform headers, because windows.h clashes with raylib.h.
// Every socket is non-blocking; the *_all functions wait with a timeout instead of blocking.

// Wide enough for a Winsock SOCKET and a POSIX descriptor alike.
typedef uintptr_t Net_Socket;
#define NET_SOCKET_INVALID ((Net_Socket)UINTPTR_MAX)

enum {
    // "255.255.255.255" plus the terminator, rounded up.
    NET_ADDRESS_SIZE = 64,
};

// Pair each successful net_startup with net_cleanup. Needed by Winsock; nothing elsewhere.
bool net_startup(void);
void net_cleanup(void);

bool net_connect(const char *host, uint16_t port, int32_t timeout_ms, Net_Socket *socket_out,
                 char *error, size_t error_size);
bool net_listen(uint16_t port, Net_Socket *socket_out, char *error, size_t error_size);
// Returns a new connection, or NET_SOCKET_INVALID when none is waiting. Sets *peer_is_local to
// whether it comes from the local network (see net_address_is_local), and peer_address to where
// it comes from.
Net_Socket net_accept(Net_Socket listener, bool *peer_is_local, char *peer_address,
                      size_t peer_address_size);
// Return the bytes moved, 0 when the call would block, or -1 when the connection failed or
// closed.
int32_t net_send(Net_Socket socket, const void *data, int32_t length);
int32_t net_recv(Net_Socket socket, void *data, int32_t length);
bool net_send_all(Net_Socket socket, const void *data, int32_t length, int32_t timeout_ms);
bool net_recv_all(Net_Socket socket, void *data, int32_t length, int32_t timeout_ms);
void net_close(Net_Socket socket);

// UDP for finding the desktop app on the local network. The socket is bound to port, or to any
// port when it is 0, and may send broadcasts.
bool net_udp_open(uint16_t port, Net_Socket *socket_out, char *error, size_t error_size);
// address is a dotted IPv4 address, such as "255.255.255.255" for everyone on the network.
bool net_udp_send(Net_Socket socket, const char *address, uint16_t port, const void *data,
                  int32_t length);
// Returns the bytes received, or 0 when nothing is waiting; fills in where they came from.
int32_t net_udp_recv(Net_Socket socket, void *data, int32_t size, char *address,
                     size_t address_size, uint16_t *port);

// True for addresses on a local network: private ranges (10/8, 172.16/12, 192.168/16),
// link-local (169.254/16) and this computer (127/8). The desktop app talks only to these,
// so that the calendar is never reachable from the internet, even through a forwarded port.
bool net_address_is_local(const char *address);

// Milliseconds from an arbitrary start, never going backwards.
int64_t net_monotonic_ms(void);
// This computer's address on the local network, as people type it into the phone, or
// "127.0.0.1" when there is no network.
void net_local_address(char *buffer, size_t buffer_size);

#endif
