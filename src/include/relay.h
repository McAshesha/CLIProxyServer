#ifndef RELAY_H
#define RELAY_H

#include <sys/socket.h>

/**
 * Bidirectional TCP relay loop until EOF or error.
 */
void relay_tcp(int client_fd, int remote_fd);

/**
 * SOCKS5-style UDP relay:
 *  - packets from client → strip header, forward payload
 *  - packets from remote → add header, send back to client_addr
 */
void relay_udp(int udp_fd, struct sockaddr_storage *client_addr, socklen_t client_len);

#endif // RELAY_H
