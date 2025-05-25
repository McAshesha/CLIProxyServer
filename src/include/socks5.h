#ifndef SOCKS5_H
#define SOCKS5_H

#include <sys/socket.h>
#include <netinet/in.h>

#define SOCKS5_VERSION           0x05
#define SOCKS5_CMD_CONNECT       0x01
#define SOCKS5_CMD_UDP_ASSOCIATE 0x03

#define SOCKS5_ATYP_IPV4         0x01
#define SOCKS5_ATYP_DOMAIN       0x03
#define SOCKS5_ATYP_IPV6         0x04

/**
 * Perform SOCKS5 handshake: greeting + request + reply.
 * @param client_fd  – клиентский TCP-сокет
 * @param cmd        – OUT: команда (CONNECT или UDP_ASSOCIATE)
 * @param addr       – OUT: целевая sockaddr (IPv4, IPv6 или DOMAIN→resolved)
 * @param addr_len   – OUT: sizeof(*addr)
 * @return 0 on success, -1 on error
 */
int socks5_handshake(int client_fd,
                     int *cmd,
                     struct sockaddr_storage *addr,
                     socklen_t *addr_len);

/**
 * Reply to SOCKS5 client with BND.ADDR = bind_addr.
 * Корректно работает для IPv4 и IPv6.
 */
int send_reply(int fd, unsigned char rep,
               const struct sockaddr *bind_addr, socklen_t bind_len);

#endif // SOCKS5_H
