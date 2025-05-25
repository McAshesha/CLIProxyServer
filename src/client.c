#include "include/client.h"
#include "include/logger.h"
#include "include/utils.h"
#include "include/socks5.h"
#include "include/relay.h"

#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define NI_FLAGS (NI_NUMERICHOST | NI_NUMERICSERV)

void *handle_client(void *arg)
{
    int client_fd = *(int *)arg;
    free(arg);

    int command = 0;
    struct sockaddr_storage dest_addr = {0};
    socklen_t dest_len = 0;

    if (socks5_handshake(client_fd, &command, &dest_addr, &dest_len) < 0) {
        log_error("SOCKS5 handshake failed: %s", strerror(errno));
        close(client_fd);
        return NULL;
    }

    char host[NI_MAXHOST], port[NI_MAXSERV];
    getnameinfo((struct sockaddr *)&dest_addr, dest_len,
                host, sizeof(host), port, sizeof(port), NI_FLAGS);
    log_info("SOCKS5 command=%s to %s:%s",
             (command == SOCKS5_CMD_CONNECT ? "CONNECT" : "UDP_ASSOCIATE"),
             host, port);

    if (command == SOCKS5_CMD_CONNECT) {
        int remote_fd = socket(dest_addr.ss_family, SOCK_STREAM, 0);
        if (remote_fd < 0) {
            log_error("socket(remote) failed: %s", strerror(errno));
            goto done;
        }
        if (connect(remote_fd, (struct sockaddr *)&dest_addr, dest_len) < 0) {
            log_error("connect(remote) failed: %s", strerror(errno));
            close(remote_fd);
            goto done;
        }
        log_info("TCP connection established");
        relay_tcp(client_fd, remote_fd);
        close(remote_fd);

    } else { // UDP_ASSOCIATE
        int udp_fd = socket(dest_addr.ss_family, SOCK_DGRAM, 0);
        if (udp_fd < 0) {
            log_error("socket(udp) failed: %s", strerror(errno));
            goto done;
        }

        int opt = 1;
        setsockopt(udp_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_storage bind_addr = {0};
        socklen_t bind_len = 0;

        if (dest_addr.ss_family == AF_INET) {
            struct sockaddr_in *b4 = (void *)&bind_addr;
            b4->sin_family      = AF_INET;
            b4->sin_addr.s_addr = INADDR_ANY;
            bind_len = sizeof(*b4);
        } else {
            struct sockaddr_in6 *b6 = (void *)&bind_addr;
            b6->sin6_family = AF_INET6;
            b6->sin6_addr   = in6addr_any;
            bind_len        = sizeof(*b6);
        }

        if (bind(udp_fd, (struct sockaddr *)&bind_addr, bind_len) < 0) {
            log_error("bind(udp) failed: %s", strerror(errno));
            close(udp_fd);
            goto done;
        }
        getsockname(udp_fd, (struct sockaddr *)&bind_addr, &bind_len);
        send_reply(client_fd, 0x00, (struct sockaddr *)&bind_addr, bind_len);
        log_info("UDP associate on port %u",
                 ntohs(((struct sockaddr_in *)&bind_addr)->sin_port));

        // register the first client packet to learn its addr
        struct sockaddr_storage client_addr = {0};
        socklen_t             client_len  = 0;
        while (true) {
            struct sockaddr_storage tmp;
            socklen_t tmp_len = sizeof(tmp);
            if (recvfrom(udp_fd, NULL, 0, MSG_PEEK,
                         (struct sockaddr *)&tmp, &tmp_len) >= 0)
            {
                client_addr = tmp;
                client_len  = tmp_len;
                char chost[NI_MAXHOST], cport[NI_MAXSERV];
                getnameinfo((struct sockaddr *)&tmp, tmp_len,
                            chost, sizeof(chost),
                            cport, sizeof(cport), NI_FLAGS);
                log_info("Registered UDP client %s:%s", chost, cport);
                break;
            }
        }

        relay_udp(udp_fd, &client_addr, client_len);
        close(udp_fd);
    }

done:
    close(client_fd);
    log_info("Client handler exiting");
    return NULL;
}
