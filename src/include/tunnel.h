#ifndef TUNNEL_H
#define TUNNEL_H

#include <stddef.h>
#include "protocol.h"

typedef struct sock sock_t;

typedef enum tunnel_state
{
    open_state,
    auth_state,
    request_state,
    connecting_state, // connecting to remote
    connected_state   // connected to remote
}
tunnel_state_t;

struct tunnel
{
    sock_t *client_sock;
    sock_t *remote_sock;

    tunnel_state_t state;
    open_protocol_t op;
    auth_protocol_t ap;
    request_protocol_t rp;
    size_t read_count;
    int closed;
};

tunnel_t*   tunnel_create(int fd);

void        tunnel_release(tunnel_t *tunnel);

void        tunnel_read_handle(int fd, void *ud);

void        tunnel_write_handle(int fd, void *ud);

int         tunnel_write_client(tunnel_t *tunnel, void *src, size_t size);

int         tunnel_connect_to_remote(tunnel_t *tunnel);

#endif // TUNNEL_H
