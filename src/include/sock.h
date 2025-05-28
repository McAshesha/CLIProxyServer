#ifndef SOCK_H
#define SOCK_H


#include "buff.h"
#include "tunnel.h"


typedef void read_cb(int fd, void *ud);
typedef void write_cb(int fd, void *ud);


typedef enum sock_state {
    sock_connecting,
    sock_connected,
    sock_halfclosed,
    sock_closed,
} sock_state_t;

struct sock {
    int fd;
    read_cb *read_handle;
    write_cb *write_handle;
    buff_t *read_buff;
    buff_t *write_buff;
    tunnel_t *tunnel;
    sock_state_t state;
    int isclient;
};


int epoll_add(sock_t *sock);

int epoll_del(const sock_t *sock);

int epoll_modify(sock_t *sock, int writable, int readable);


sock_t* sock_create(int fd, sock_state_t state, int isclient, tunnel_t * tunnel);

void sock_release(sock_t *sock);

void sock_shutdown(sock_t *sock);

void sock_force_shutdown(sock_t *sock);

int sock_nonblocking(int fd);

int sock_keepalive(int fd);


#endif //SOCK_H
