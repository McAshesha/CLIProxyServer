#ifndef SOCKS5_H
#define SOCKS5_H

#include <netinet/in.h>

#define SOCKS5_VERSION          0x05
#define SOCKS5_CMD_CONNECT      0x01
#define SOCKS5_CMD_UDP_ASSOCIATE 0x03
#define SOCKS5_ATYP_IPV4        0x01
#define SOCKS5_ATYP_DOMAIN      0x03

/**
 * Perform SOCKS5 handshake: greeting + request + reply.
 * @param client_fd - accepted client socket
 * @param cmd - output: requested command (CONNECT or UDP ASSOCIATE)
 * @param addr - output: target address and port (network byte order)
 * @return 0 on success, -1 on error
 */
int socks5_handshake(int client_fd, int *cmd, struct sockaddr_in *addr);

#endif // SOCKS5_H
