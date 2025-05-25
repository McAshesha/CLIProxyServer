#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <poll.h>
#include "include/logger.h"
#include "include/utils.h"

#include "include/socks5.h"

#define LISTEN_PORT     1080
#define BACKLOG         128
#define TCP_BUF_SIZE    4096
#define UDP_BUF_SIZE    65536

static void relay_tcp(int client_fd, int remote_fd)
{
    struct pollfd fds[2] =
    {
        { .fd = client_fd, .events = POLLIN },
        { .fd = remote_fd, .events = POLLIN }
    };
    unsigned char buffer[TCP_BUF_SIZE];

    log_info("Starting TCP relay");

    while (true)
    {
        if (poll(fds, 2, -1) < 0)
        {
            log_error("poll() failed: %s", strerror(errno));
            break;
        }

        if (fds[0].revents & POLLIN)
        {
            ssize_t n = safe_read(client_fd, buffer, sizeof(buffer));
            if (n <= 0)
            {
                log_info("Client closed TCP");
                break;
            }
            hexdump("TCP ← client", buffer, n);
            safe_write(remote_fd, buffer, n);
        }

        if (fds[1].revents & POLLIN)
        {
            ssize_t n = safe_read(remote_fd, buffer, sizeof(buffer));
            if (n <= 0)
            {
                log_info("Remote closed TCP");
                break;
            }
            hexdump("TCP ← remote", buffer, n);
            safe_write(client_fd, buffer, n);
        }
    }

    log_info("TCP relay ended");
}

static void relay_udp(int udp_fd, struct sockaddr_storage *client_addr, socklen_t client_len)
{
    log_info("Starting UDP relay");

    unsigned char recv_buf[UDP_BUF_SIZE];
    unsigned char send_buf[UDP_BUF_SIZE];
    struct sockaddr_storage peer_addr;
    socklen_t peer_len;

    bool client_registered = (client_addr->ss_family != 0);

    while (true)
    {
        peer_len = sizeof(peer_addr);
        ssize_t n = recvfrom(udp_fd, recv_buf, sizeof(recv_buf), 0,
                             (struct sockaddr *)&peer_addr, &peer_len);

        if (n < 0)
        {
            log_error("recvfrom() failed: %s", strerror(errno));
            break;
        }

        // Client → Remote
        if (client_registered
            && peer_len == client_len
            && memcmp(&peer_addr, client_addr, peer_len) == 0)
        {
            size_t pos = 0;
            if ((size_t)n < 4)
            {
                continue;
            }

            pos += 2; // RSV
            uint8_t frag = recv_buf[pos++];
            if (frag != 0)
            {
                continue;
            }

            uint8_t atyp = recv_buf[pos++];
            struct sockaddr_storage remote_dest = {0};
            socklen_t remote_len = 0;

            if (atyp == SOCKS5_ATYP_IPV4 && (size_t)n >= pos + 6)
            {
                struct sockaddr_in *dst4 = (void *)&remote_dest;
                dst4->sin_family = AF_INET;
                memcpy(&dst4->sin_addr.s_addr, recv_buf + pos, 4);
                pos += 4;
                memcpy(&dst4->sin_port, recv_buf + pos, 2);
                pos += 2;
                remote_len = sizeof(*dst4);
            }
            else if (atyp == SOCKS5_ATYP_DOMAIN && (size_t)n > pos + 1)
            {
                uint8_t dom_len = recv_buf[pos++];
                if ((size_t)n < pos + dom_len + 2)
                {
                    continue;
                }

                char domain[256] = {0};
                memcpy(domain, recv_buf + pos, dom_len);
                pos += dom_len;

                uint16_t port_net;
                memcpy(&port_net, recv_buf + pos, 2);
                pos += 2;
                port_net = ntohs(port_net);

                struct addrinfo hints = {0}, *res = NULL;
                hints.ai_family   = AF_UNSPEC;
                hints.ai_socktype = SOCK_DGRAM;

                char port_str[6];
                snprintf(port_str, sizeof(port_str), "%u", port_net);

                if (getaddrinfo(domain, port_str, &hints, &res) == 0 && res)
                {
                    memcpy(&remote_dest, res->ai_addr, res->ai_addrlen);
                    remote_len = res->ai_addrlen;
                    freeaddrinfo(res);
                }
                else
                {
                    continue;
                }
            }
            else if (atyp == SOCKS5_ATYP_IPV6 && (size_t)n >= pos + 18)
            {
                struct sockaddr_in6 *dst6 = (void *)&remote_dest;
                dst6->sin6_family = AF_INET6;
                memcpy(&dst6->sin6_addr, recv_buf + pos, 16);
                pos += 16;
                memcpy(&dst6->sin6_port, recv_buf + pos, 2);
                pos += 2;
                remote_len = sizeof(*dst6);
            }
            else
            {
                // unsupported header
                continue;
            }

            size_t payload_len = (size_t)n - pos;
            unsigned char *payload = recv_buf + pos;
            hexdump("UDP → remote", payload, payload_len);

            if (sendto(udp_fd, payload, payload_len, 0,
                       (struct sockaddr *)&remote_dest, remote_len) < 0)
            {
                log_error("sendto(remote) failed: %s", strerror(errno));
            }
        }
        // Remote → Client
        else
        {
            hexdump("UDP ← remote", recv_buf, n);

            size_t off = 0;
            send_buf[off++] = 0; // RSV
            send_buf[off++] = 0; // RSV
            send_buf[off++] = 0; // FRAG

            if (peer_addr.ss_family == AF_INET)
            {
                send_buf[off++] = SOCKS5_ATYP_IPV4;
                struct sockaddr_in *p4 = (void *)&peer_addr;
                memcpy(send_buf + off, &p4->sin_addr.s_addr, 4);
                off += 4;
                memcpy(send_buf + off, &p4->sin_port, 2);
                off += 2;
            }
            else // AF_INET6
            {
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

static void *handle_client(void *arg)
{
    int client_fd = *(int *)arg;
    free(arg);

    struct sockaddr_storage dest_addr = {0};
    socklen_t dest_len = 0;
    int command = 0;

    if (socks5_handshake(client_fd, &command, &dest_addr, &dest_len) < 0)
    {
        log_error("SOCKS5 handshake failed: %s", strerror(errno));
        close(client_fd);
        return NULL;
    }

    char host[NI_MAXHOST] = {0};
    char port[NI_MAXSERV] = {0};

    getnameinfo((struct sockaddr *)&dest_addr, dest_len,
                host, sizeof(host), port, sizeof(port),
                NI_NUMERICHOST | NI_NUMERICSERV);

    log_info("SOCKS5 command=%s to %s:%s",
             (command == SOCKS5_CMD_CONNECT ? "CONNECT" : "UDP_ASSOCIATE"),
             host, port);

    if (command == SOCKS5_CMD_CONNECT)
    {
        int remote_fd = socket(dest_addr.ss_family, SOCK_STREAM, 0);
        if (remote_fd < 0)
        {
            log_error("socket(remote) failed: %s", strerror(errno));
            goto cleanup;
        }

        if (connect(remote_fd, (struct sockaddr *)&dest_addr, dest_len) < 0)
        {
            log_error("connect(remote) failed: %s", strerror(errno));
            close(remote_fd);
            goto cleanup;
        }

        log_info("TCP connection established");
        relay_tcp(client_fd, remote_fd);
        close(remote_fd);
    }
    else // UDP_ASSOCIATE
    {
        int udp_fd = socket(dest_addr.ss_family, SOCK_DGRAM, 0);
        if (udp_fd < 0)
        {
            log_error("socket(udp) failed: %s", strerror(errno));
            goto cleanup;
        }

        int opt = 1;
        if (setsockopt(udp_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
        {
            log_error("setsockopt() failed: %s", strerror(errno));
            close(udp_fd);
            goto cleanup;
        }

        struct sockaddr_storage udp_bind = {0};
        socklen_t bind_len = 0;

        if (dest_addr.ss_family == AF_INET)
        {
            struct sockaddr_in *b4 = (void *)&udp_bind;
            b4->sin_family = AF_INET;
            b4->sin_addr.s_addr = INADDR_ANY;
            bind_len = sizeof(*b4);
        }
        else
        {
            struct sockaddr_in6 *b6 = (void *)&udp_bind;
            b6->sin6_family = AF_INET6;
            b6->sin6_addr = in6addr_any;
            bind_len = sizeof(*b6);
        }

        if (bind(udp_fd, (struct sockaddr *)&udp_bind, bind_len) < 0)
        {
            log_error("bind(udp) failed: %s", strerror(errno));
            close(udp_fd);
            goto cleanup;
        }

        if (getsockname(udp_fd, (struct sockaddr *)&udp_bind, &bind_len) < 0)
        {
            log_error("getsockname() failed: %s", strerror(errno));
            close(udp_fd);
            goto cleanup;
        }

        if (send_reply(client_fd, 0x00, (struct sockaddr *)&udp_bind, bind_len) < 0)
        {
            log_error("send_reply() failed: %s", strerror(errno));
            close(udp_fd);
            goto cleanup;
        }

        log_info("UDP associate on port %u",
                 ntohs(((struct sockaddr_in *)&udp_bind)->sin_port));

        struct sockaddr_storage client_addr = {0};
        socklen_t client_len = 0;
        bool registered = false;

        // First packet registers client
        while (!registered)
        {
            struct sockaddr_storage tmp;
            socklen_t tmp_len = sizeof(tmp);
            ssize_t peeked = recvfrom(udp_fd, NULL, 0, MSG_PEEK,
                                      (struct sockaddr *)&tmp, &tmp_len);
            if (peeked >= 0)
            {
                memcpy(&client_addr, &tmp, tmp_len);
                client_len = tmp_len;
                registered = true;
                char chost[NI_MAXHOST], cport[NI_MAXSERV];
                getnameinfo((struct sockaddr *)&tmp, tmp_len,
                            chost, sizeof(chost),
                            cport, sizeof(cport),
                            NI_NUMERICHOST | NI_NUMERICSERV);
                log_info("Registered UDP client %s:%s", chost, cport);
            }
        }

        relay_udp(udp_fd, &client_addr, client_len);
        close(udp_fd);
    }

cleanup:
    close(client_fd);
    log_info("Client handler exiting");
    return NULL;
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0)
    {
        log_error("socket() failed: %s", strerror(errno));
        return EXIT_FAILURE;
    }

    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        log_error("setsockopt() failed: %s", strerror(errno));
        close(listen_fd);
        return EXIT_FAILURE;
    }

    struct sockaddr_in srv = {0};
    srv.sin_family = AF_INET;
    srv.sin_addr.s_addr = INADDR_ANY;
    srv.sin_port = htons(LISTEN_PORT);

    if (bind(listen_fd, (struct sockaddr *)&srv, sizeof(srv)) < 0)
    {
        log_error("bind() failed: %s", strerror(errno));
        close(listen_fd);
        return EXIT_FAILURE;
    }

    if (listen(listen_fd, BACKLOG) < 0)
    {
        log_error("listen() failed: %s", strerror(errno));
        close(listen_fd);
        return EXIT_FAILURE;
    }

    log_info("Listening on port %d", LISTEN_PORT);

    while (true)
    {
        struct sockaddr_storage cli = {0};
        socklen_t cli_len = sizeof(cli);
        int *client_fd_ptr = malloc(sizeof(int));

        if (!client_fd_ptr)
        {
            log_error("malloc() failed");
            continue;
        }

        *client_fd_ptr = accept(listen_fd,
                                (struct sockaddr *)&cli,
                                &cli_len);

        if (*client_fd_ptr < 0)
        {
            log_error("accept() failed: %s", strerror(errno));
            free(client_fd_ptr);
            continue;
        }

        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, client_fd_ptr) != 0)
        {
            log_error("pthread_create() failed");
            close(*client_fd_ptr);
            free(client_fd_ptr);
            continue;
        }

        pthread_detach(tid);
    }

    close(listen_fd);
    return EXIT_SUCCESS;
}
