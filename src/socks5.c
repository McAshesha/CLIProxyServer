#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "include/logger.h"
#include "include/utils.h"
#include <netdb.h>
#include "include/socks5.h"


int send_reply(int client_fd, uint8_t reply_code,
               const struct sockaddr *bind_address, socklen_t bind_address_len)
{
    uint8_t response[4 + 16 + 2] = {0};
    size_t offset = 0;

    response[offset++] = SOCKS5_VERSION;
    response[offset++] = reply_code;
    response[offset++] = 0x00;  // RSV

    if (bind_address->sa_family == AF_INET)
    {
        const struct sockaddr_in *addr4 = (const struct sockaddr_in *)bind_address;
        response[offset++] = SOCKS5_ATYP_IPV4;
        memcpy(&response[offset], &addr4->sin_addr.s_addr, 4);
        offset += 4;
        memcpy(&response[offset], &addr4->sin_port, 2);
        offset += 2;
    }
    else if (bind_address->sa_family == AF_INET6)
    {
        const struct sockaddr_in6 *addr6 = (const struct sockaddr_in6 *)bind_address;
        response[offset++] = SOCKS5_ATYP_IPV6;
        memcpy(&response[offset], &addr6->sin6_addr, 16);
        offset += 16;
        memcpy(&response[offset], &addr6->sin6_port, 2);
        offset += 2;
    }
    else
    {
        response[offset++] = SOCKS5_ATYP_IPV4;
        // zeroed by initializer
        offset += 6;
    }

    if (safe_write_n(client_fd, response, offset) != (ssize_t)offset)
    {
        log_error("send_reply() write error");
        return -1;
    }

    log_info("Sent SOCKS5 reply: code=0x%02x, family=%d",
             reply_code, bind_address->sa_family);
    return 0;
}

int socks5_handshake(int client_fd, int *out_command,
                     struct sockaddr_storage *out_destination, socklen_t *out_dest_len)
{
    uint8_t header[4] = {0};
    uint8_t method_resp[2] = {0};

    // 1) Greeting
    if (safe_read_n(client_fd, header, 2) != 2 || header[0] != SOCKS5_VERSION)
    {
        log_error("Invalid SOCKS version in greeting");
        return -1;
    }

    uint8_t n_methods = header[1];
    if (n_methods == 0)
    {
        log_error("No authentication methods offered");
        return -1;
    }

    if (safe_read_n(client_fd, header, n_methods) != n_methods)
    {
        log_error("Failed to read auth methods");
        return -1;
    }

    // Select NO AUTH
    method_resp[0] = SOCKS5_VERSION;
    method_resp[1] = 0x00;
    if (safe_write_n(client_fd, method_resp, sizeof(method_resp)) != sizeof(method_resp))
    {
        log_error("Failed to send method selection");
        return -1;
    }

    // 2) Client Request
    if (safe_read_n(client_fd, header, 4) != 4 || header[0] != SOCKS5_VERSION)
    {
        log_error("Invalid SOCKS version in request");
        return -1;
    }

    uint8_t command     = header[1];
    uint8_t address_type = header[3];
    *out_command        = command;

    // 3) Read Destination
    size_t addr_len = 0;
    struct sockaddr_storage dest = {0};

    switch (address_type)
    {
        case SOCKS5_ATYP_IPV4:
        {
            uint8_t buf[6] = {0};
            if (safe_read_n(client_fd, buf, sizeof(buf)) != sizeof(buf))
            {
                log_error("Failed to read IPv4 dest");
                return -1;
            }
            struct sockaddr_in *ip4 = (struct sockaddr_in *)&dest;
            ip4->sin_family = AF_INET;
            memcpy(&ip4->sin_addr.s_addr, buf, 4);
            memcpy(&ip4->sin_port, buf + 4, 2);
            addr_len = sizeof(*ip4);
            break;
        }
        case SOCKS5_ATYP_IPV6:
        {
            uint8_t buf[18] = {0};
            if (safe_read_n(client_fd, buf, sizeof(buf)) != sizeof(buf))
            {
                log_error("Failed to read IPv6 dest");
                return -1;
            }
            struct sockaddr_in6 *ip6 = (struct sockaddr_in6 *)&dest;
            ip6->sin6_family = AF_INET6;
            memcpy(&ip6->sin6_addr, buf, 16);
            memcpy(&ip6->sin6_port, buf + 16, 2);
            addr_len = sizeof(*ip6);
            break;
        }
        case SOCKS5_ATYP_DOMAIN:
        {
            uint8_t dom_len;
            if (safe_read_n(client_fd, &dom_len, 1) != 1 || dom_len == 0)
            {
                log_error("Invalid domain length");
                return -1;
            }

            char domain[256] = {0};
            if (safe_read_n(client_fd, domain, dom_len) != dom_len)
            {
                log_error("Failed to read domain");
                return -1;
            }
            domain[dom_len] = '\0';

            uint8_t port_be[2] = {0};
            if (safe_read_n(client_fd, port_be, 2) != 2)
            {
                log_error("Failed to read port");
                return -1;
            }
            uint16_t port = ntohs(*(uint16_t *)port_be);

            struct addrinfo hints = {0}, *res = NULL;
            hints.ai_family   = AF_UNSPEC;
            hints.ai_socktype = (command == SOCKS5_CMD_CONNECT ? SOCK_STREAM : SOCK_DGRAM);

            char port_str[6];
            snprintf(port_str, sizeof(port_str), "%u", port);

            if (getaddrinfo(domain, port_str, &hints, &res) != 0 || !res)
            {
                log_error("Domain resolve failed for %s:%u", domain, port);
                return -1;
            }

            memcpy(&dest, res->ai_addr, res->ai_addrlen);
            addr_len = res->ai_addrlen;
            freeaddrinfo(res);
            break;
        }
        default:
            log_error("Unsupported address type: %u", address_type);
            return -1;
    }

    // 4) Send CONNECT reply
    if (command == SOCKS5_CMD_CONNECT)
    {
        struct sockaddr_storage bind_addr = {0};
        socklen_t bind_len = 0;

        if (dest.ss_family == AF_INET)
        {
            struct sockaddr_in *b4 = (struct sockaddr_in *)&bind_addr;
            b4->sin_family = AF_INET;
            b4->sin_addr.s_addr = INADDR_ANY;
            b4->sin_port = 0;
            bind_len = sizeof(*b4);
        }
        else
        {
            struct sockaddr_in6 *b6 = (struct sockaddr_in6 *)&bind_addr;
            b6->sin6_family = AF_INET6;
            b6->sin6_addr = in6addr_any;
            b6->sin6_port = 0;
            bind_len = sizeof(*b6);
        }

        if (send_reply(client_fd, 0x00,
                       (const struct sockaddr *)&bind_addr, bind_len) < 0)
        {
            log_error("Failed to send CONNECT reply");
            return -1;
        }

        log_info("CONNECT handshake done");
    }

    *out_dest_len = addr_len;
    memcpy(out_destination, &dest, addr_len);

    return 0;
}
