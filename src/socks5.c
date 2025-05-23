#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "include/socks5.h"

// Send simple reply for CONNECT only
static int send_reply(int fd, unsigned char rep) {
    unsigned char resp[10] = {SOCKS5_VERSION, rep, 0x00, SOCKS5_ATYP_IPV4, 0,0,0,0, 0,0};
    return write(fd, resp, sizeof(resp));
}

int socks5_handshake(int client_fd, int *cmd, struct sockaddr_in *addr) {
    unsigned char buf[262];
    // 1) Greeting
    if (read(client_fd, buf, 2) != 2 || buf[0] != SOCKS5_VERSION) return -1;
    int nmethods = buf[1];
    if (read(client_fd, buf, nmethods) != nmethods) return -1;
    // Select NO AUTH
    unsigned char method_sel[2] = {SOCKS5_VERSION, 0x00};
    if (write(client_fd, method_sel, 2) != 2) return -1;
    // 2) Request
    if (read(client_fd, buf, 4) != 4 || buf[0] != SOCKS5_VERSION) return -1;
    *cmd = buf[1];
    unsigned char atyp = buf[3];
    int offset = 4;
    if (atyp == SOCKS5_ATYP_IPV4) {
        if (read(client_fd, buf+offset, 4+2) != 6) return -1;
        memcpy(&addr->sin_addr.s_addr, buf+offset, 4);
        offset += 4;
    } else {
        return -1; // only IPv4 supported
    }
    memcpy(&addr->sin_port, buf+offset, 2);
    addr->sin_family = AF_INET;
    // Reply for CONNECT only; UDP_ASSOCIATE reply later
    if (*cmd == SOCKS5_CMD_CONNECT) {
        if (send_reply(client_fd, 0x00) < 0) return -1;
    }
    return 0;
}
