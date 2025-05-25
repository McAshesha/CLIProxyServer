#include "include/relay.h"
#include "include/logger.h"
#include "include/utils.h"
#include "include/socks5.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>

#define TCP_BUF_SIZE 4096
#define UDP_BUF_SIZE 65536

void relay_tcp(int client_fd, int remote_fd)
{
    struct pollfd fds[2] = {
        { .fd = client_fd, .events = POLLIN },
        { .fd = remote_fd, .events = POLLIN }
    };
    unsigned char buf[TCP_BUF_SIZE];

    log_info("Starting TCP relay");
    while (true) {
        if (poll(fds, 2, -1) < 0) {
            log_error("poll() failed: %s", strerror(errno));
            break;
        }

        if (fds[0].revents & POLLIN) {
            ssize_t n = safe_read(client_fd, buf, sizeof(buf));
            if (n <= 0) { log_info("Client closed TCP"); break; }
            hexdump("TCP ← client", buf, n);
            safe_write(remote_fd, buf, n);
        }
        if (fds[1].revents & POLLIN) {
            ssize_t n = safe_read(remote_fd, buf, sizeof(buf));
            if (n <= 0) { log_info("Remote closed TCP"); break; }
            hexdump("TCP ← remote", buf, n);
            safe_write(client_fd, buf, n);
        }
    }
    log_info("TCP relay ended");
}

void relay_udp(int udp_fd, struct sockaddr_storage *client_addr, socklen_t client_len)
{
    log_info("Starting UDP relay");

    unsigned char recv_buf[UDP_BUF_SIZE];
    unsigned char send_buf[UDP_BUF_SIZE];
    struct sockaddr_storage peer_addr;
    socklen_t peer_len;
    bool client_registered = (client_addr->ss_family != 0);

    while (true) {
        peer_len = sizeof(peer_addr);
        ssize_t n = recvfrom(udp_fd, recv_buf, sizeof(recv_buf), 0,
                             (struct sockaddr *)&peer_addr, &peer_len);
        if (n < 0) {
            log_error("recvfrom() failed: %s", strerror(errno));
            break;
        }

        // client → remote
        if (client_registered
            && peer_len == client_len
            && memcmp(&peer_addr, client_addr, peer_len) == 0)
        {
            size_t pos = 0;
            if ((size_t)n < 4) continue;
            pos += 2;              // RSV
            if (recv_buf[pos++] != 0) continue; // FRAG
            uint8_t atyp = recv_buf[pos++];
            struct sockaddr_storage dest = {0};
            socklen_t dest_len = 0;

            if (atyp == SOCKS5_ATYP_IPV4 && (size_t)n >= pos + 6) {
                struct sockaddr_in *d4 = (void *)&dest;
                d4->sin_family = AF_INET;
                memcpy(&d4->sin_addr.s_addr, recv_buf + pos, 4);
                pos += 4;
                memcpy(&d4->sin_port,      recv_buf + pos, 2);
                pos += 2;
                dest_len = sizeof(*d4);
            }
            else if (atyp == SOCKS5_ATYP_DOMAIN && (size_t)n > pos + 1) {
                uint8_t domlen = recv_buf[pos++];
                if ((size_t)n < pos + domlen + 2) continue;
                char domain[256] = {0};
                memcpy(domain, recv_buf + pos, domlen);
                pos += domlen;
                uint16_t port_net;
                memcpy(&port_net, recv_buf + pos, 2);
                pos += 2; port_net = ntohs(port_net);

                struct addrinfo hints = { .ai_family=AF_UNSPEC, .ai_socktype=SOCK_DGRAM }, *res;
                char port_str[6];
                snprintf(port_str, sizeof(port_str), "%u", port_net);
                if (getaddrinfo(domain, port_str, &hints, &res) == 0) {
                    memcpy(&dest, res->ai_addr, res->ai_addrlen);
                    dest_len = res->ai_addrlen;
                    freeaddrinfo(res);
                } else continue;
            }
            else if (atyp == SOCKS5_ATYP_IPV6 && (size_t)n >= pos + 18) {
                struct sockaddr_in6 *d6 = (void *)&dest;
                d6->sin6_family = AF_INET6;
                memcpy(&d6->sin6_addr, recv_buf + pos, 16);
                pos += 16;
                memcpy(&d6->sin6_port, recv_buf + pos, 2);
                pos += 2;
                dest_len = sizeof(*d6);
            } else {
                continue;
            }

            size_t payload_len = (size_t)n - pos;
            unsigned char *payload = recv_buf + pos;
            hexdump("UDP → remote", payload, payload_len);
            if (sendto(udp_fd, payload, payload_len, 0,
                       (struct sockaddr *)&dest, dest_len) < 0)
            {
                log_error("sendto(remote) failed: %s", strerror(errno));
            }
        }
        // remote → client
        else {
            hexdump("UDP ← remote", recv_buf, n);
            size_t off = 0;
            send_buf[off++] = 0; // RSV
            send_buf[off++] = 0; // RSV
            send_buf[off++] = 0; // FRAG

            if (peer_addr.ss_family == AF_INET) {
                send_buf[off++] = SOCKS5_ATYP_IPV4;
                struct sockaddr_in *p4 = (void *)&peer_addr;
                memcpy(send_buf + off, &p4->sin_addr.s_addr, 4);
                off += 4;
                memcpy(send_buf + off, &p4->sin_port, 2);
                off += 2;
            } else {
                send_buf[off++] = SOCKS5_ATYP_IPV6;
                struct sockaddr_in6 *p6 = (void *)&peer_addr;
                memcpy(send_buf + off, &p6->sin6_addr, 16);
                off += 16;
                memcpy(send_buf + off, &p6->sin6_port, 2);
                off += 2;
            }

            memcpy(send_buf + off, recv_buf, n);
            off += n;

            if (sendto(udp_fd, send_buf, off, 0,
                       (struct sockaddr *)client_addr, client_len) < 0)
            {
                log_error("sendto(client) failed: %s", strerror(errno));
            }
        }
    }

    log_info("UDP relay ended");
}
