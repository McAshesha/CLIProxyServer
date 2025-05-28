#include <unistd.h>
#include <sys/epoll.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>

#include "sock.h"
#include "logger.h"
#include "server.h"


#define INIT_BUFF_CAP 1024


typedef struct epoll_event epoll_event_t;


int epoll_add(sock_t *sock)
{
	epoll_event_t event;
	event.events = EPOLLIN;
	event.data.ptr = sock;
	return epoll_ctl(SERVER.epollfd, EPOLL_CTL_ADD, sock->fd, &event);
}

int epoll_del(const sock_t *sock)
{
	epoll_event_t event;
	return epoll_ctl(SERVER.epollfd, EPOLL_CTL_DEL, sock->fd, &event);
}

int epoll_modify(sock_t *sock, int writable, int readable)
{
	epoll_event_t event;
	event.data.ptr = sock;
	event.events = (writable ? EPOLLOUT : 0) | (readable ? EPOLLIN : 0);
	return epoll_ctl(SERVER.epollfd, EPOLL_CTL_MOD, sock->fd, &event);
}

sock_t* sock_create(int fd, sock_state_t state, int isclient, tunnel_t * tunnel)
{
	sock_t *sock = (sock_t*)malloc(sizeof(*sock));
	if (sock == NULL) return NULL;
	memset(sock, 0, sizeof(*sock));

	buff_t *read_buff = buff_create(INIT_BUFF_CAP);
	if (read_buff == NULL) return NULL;

	buff_t *write_buff = buff_create(INIT_BUFF_CAP);
	if (write_buff == NULL) {
		buff_release(read_buff);
		free(sock);
		return NULL;
	}

	sock->read_buff = read_buff;
	sock->write_buff = write_buff;
	sock->tunnel = tunnel;
	sock->fd = fd;
	sock->read_handle = tunnel_read_handle;
	sock->write_handle = tunnel_write_handle;
	sock->state = state;
	sock->isclient = isclient;
	return sock;
}

void sock_release(sock_t *sock)
{
	LOG_INFO("Closed and released sock fd=%d", sock->fd);

	tunnel_t *tunnel = sock->tunnel;

	buff_release(sock->write_buff);
	buff_release(sock->read_buff);

	if (sock->isclient) tunnel->client_sock = NULL;
	else tunnel->remote_sock = NULL;
	epoll_del(sock);
	close(sock->fd);
	free(sock);

	// when both client and remote sock release
	// release tunnel
	if (tunnel->remote_sock == NULL && tunnel->client_sock == NULL) {
		tunnel_release(tunnel);
	}
}

/*
 * Receive rst or no more data to send or invalid peer, we should release sock
 * */
void sock_force_shutdown(sock_t *sock)
{
	LOG_ERROR("Forcing shutdown of fd=%d", sock->fd);

	sock_release(sock);
}

/*
 * Receive fin, do not receive again,
 * If Append read_buff to other write_buff,
 * If write_buff not empty, we shoould still send data,
 * Otherwise force shutdown
 * */
void sock_shutdown(sock_t *sock)
{
	sock->state = sock_halfclosed;

	LOG_INFO("Half-closing fd=%d", sock->fd);

	tunnel_t *tunnel = sock->tunnel;
	// forward left data
	if (tunnel->state == connected_state) {
		if (sock->isclient && tunnel->remote_sock != NULL)
			buff_concat(tunnel->remote_sock->write_buff, sock->read_buff);
		else if(tunnel->client_sock != NULL)
			buff_concat(tunnel->client_sock->write_buff, sock->read_buff);
	}

	int writable = buff_readable(sock->write_buff) > 0;
	if (writable) epoll_modify(sock, writable, 0);
	else sock_force_shutdown(sock);
}

int sock_nonblocking(int fd)
{
	int flag;
	if ((flag = fcntl(fd, F_GETFL, 0)) < 0) return -1;
	if ((flag = fcntl(fd, F_SETFL, flag | O_NONBLOCK)) < 0) return -1;
	return flag;
}

int sock_keepalive(int fd)
{
	int keepalive = 1;
	return setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
}
