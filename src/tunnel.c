#include "tunnel.h"

#include <unistd.h>
#include <sys/epoll.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <signal.h>
#include <assert.h>

#include "tunnel.h"
#include "sock.h"
#include "logger.h"


#if (EAGAIN != EWOULDBLOCK)
	#define EAGAIN_EWOULDBLOCK EAGAIN : case EWOULDBLOCK
#else
	#define EAGAIN_EWOULDBLOCK EAGAIN
#endif


typedef struct sockaddr sockaddr_t;
typedef struct sockaddr_in sockaddr_in_t;
typedef struct sockaddr_in6 sockaddr_in6_t;


tunnel_t* tunnel_create(int fd)
{
	sock_nonblocking(fd);
	sock_keepalive(fd);

	tunnel_t *tunnel = (tunnel_t*)malloc(sizeof(*tunnel));
	if (tunnel == NULL) {
		close(fd);
		return NULL;
	}
	memset(tunnel, 0, sizeof(*tunnel));

	sock_t *client_sock = sock_create(fd, sock_connected, 1, tunnel);
	if (client_sock == NULL) {
		free(tunnel);
		close(fd);
		return NULL;
	}

	tunnel->state = open_state;
	tunnel->client_sock = client_sock;
	tunnel->read_count = 0;
	tunnel->closed = 0;

	epoll_add(client_sock);

	return tunnel;
}

void tunnel_release(tunnel_t *tunnel)
{
	free(tunnel);
}

void tunnel_shutdown(tunnel_t *tunnel)
{
	if (tunnel->client_sock != NULL) sock_shutdown(tunnel->client_sock);
	if (tunnel->remote_sock != NULL) sock_shutdown(tunnel->remote_sock);
}

void tunnel_read_handle(int fd, void *ud)
{
	sock_t *sock = (sock_t*)ud;
	tunnel_t *tunnel = sock->tunnel;

	int n = buff_readfd(sock->read_buff, fd);
	if (n < 0) {
		switch (errno) {
			case EINTR:
			case EAGAIN_EWOULDBLOCK:
				break;
			default:
				goto shutdown;
		}

	} else if (n == 0) goto shutdown;

	switch (tunnel->state) {
		case open_state:
			if (tunnel_open_handle(tunnel) < 0) goto force_shutdown;
			break;
		case auth_state:
			if (tunnel_auth_handle(tunnel) < 0) goto force_shutdown;
			break;
		case request_state:
			if (tunnel_request_handle(tunnel) < 0) goto force_shutdown;
			break;
		case connecting_state:
			assert(sock->isclient == 0);
			if (tunnel_connecting_handle(tunnel) < 0) goto tunnel_shutdown;
			break;
		case connected_state:
			if (tunnel_connected_handle(tunnel, sock->isclient) < 0) goto tunnel_shutdown;
			break;
		default:
			assert(0);
			break;
	}

	LOG_INFO("Read %d bytes from %s (fd=%d), state=%d",
		 n, sock->isclient ? "client" : "remote", fd, tunnel->state);

	return;

force_shutdown: // peer invalid
	LOG_WARN("Read returned %d on fd=%d – initiating shutdown", n, fd);
	sock_force_shutdown(sock);
	return;

shutdown: // half closed
	LOG_WARN("Read returned %d on fd=%d – initiating shutdown", n, fd);
	sock_shutdown(sock);
	return;

tunnel_shutdown: // half closed both client and remote
	LOG_WARN("Read returned %d on fd=%d – initiating shutdown", n, fd);
	tunnel_shutdown(tunnel);
}

void tunnel_write_handle(int fd, void *ud)
{
	sock_t *sock = (sock_t *)ud;
	tunnel_t *tunnel = sock->tunnel;

	if (buff_readable(sock->write_buff) > 0) {
		int n = buff_writefd(sock->write_buff, fd);
		if (n <= 0) {
			switch (errno) {
				case EINTR:
				case EAGAIN_EWOULDBLOCK:
					break;
				default:
					goto force_shutdown;
			}
		}
		LOG_INFO("Wrote %d bytes to %s (fd=%d)", n, sock->isclient ? "client" : "remote", fd);

	} else if (sock->state == sock_halfclosed) {
		goto force_shutdown;
	}

	if (tunnel->state == connecting_state) {
		assert(sock->isclient == 0);

		if (tunnel_connecting_handle(tunnel) < 0) goto tunnel_shutdown;
	}

	int writable = buff_readable(sock->write_buff) > 0;
	epoll_modify(sock, writable, 1);

	return;

tunnel_shutdown:
	tunnel_shutdown(tunnel);
	LOG_ERROR("Write error on fd=%d: %s", fd, strerror(errno));
	return;

force_shutdown:
	sock_force_shutdown(sock);
	LOG_ERROR("Write error on fd=%d: %s", fd, strerror(errno));
	return;
}

int tunnel_connected_handle(tunnel_t *tunnel, int client)
{
	if (client) {
		if (tunnel->remote_sock == NULL) return -1;

		LOG_INFO("Forwarded %zu bytes %s → %s", buff_readable(tunnel->client_sock->read_buff), "client", "remote");

		if (buff_concat(tunnel->remote_sock->write_buff, tunnel->client_sock->read_buff) < 0) return -1;
		buff_clear(tunnel->client_sock->read_buff);
		epoll_modify(tunnel->remote_sock, 1, 1);
	} else {
		if (tunnel->client_sock == NULL) return -1;

		LOG_INFO("Forwarded %zu bytes %s → %s", buff_readable(tunnel->remote_sock->read_buff), "remote", "client");

		if (buff_concat(tunnel->client_sock->write_buff, tunnel->remote_sock->read_buff) < 0) return -1;
		buff_clear(tunnel->remote_sock->read_buff);
		epoll_modify(tunnel->client_sock, 1, 1);
	}
	return 0;
}

int tunnel_notify_connected(tunnel_t *tunnel)
{
	sockaddr_t sa;
	socklen_t len = sizeof(sa);
	uint8_t header[4];
	header[0] = 0x05; // socks5
	header[1] = 0x00; // success
	header[2] = 0x00;

	if (getsockname(tunnel->remote_sock->fd, &sa, &len) < 0) return -1;

	if (sa.sa_family == AF_INET) {
		header[3] = 0x01; //IPV4
		if (tunnel_write_client(tunnel, header, sizeof(header)) < 0) return -1;

		sockaddr_in_t *sa_in = (sockaddr_in_t*)&sa;
		if (tunnel_write_client(tunnel, &sa_in->sin_addr, sizeof(sa_in->sin_addr)) < 0) return -1;
		if (tunnel_write_client(tunnel, &sa_in->sin_port, sizeof(sa_in->sin_port)) < 0) return -1;
	} else if (sa.sa_family == AF_INET6) {
		header[3] = 0x04; //IPV6
		tunnel_write_client(tunnel, header, sizeof(header));

		sockaddr_in6_t *sa_in6 = (sockaddr_in6_t*)&sa;
		tunnel_write_client(tunnel, &sa_in6->sin6_addr, sizeof(sa_in6->sin6_addr));
		tunnel_write_client(tunnel, &sa_in6->sin6_port, sizeof(sa_in6->sin6_port));
	} else {
		LOG_ERROR("Failed tunnel_notify_connected, unexpected family=%d", sa.sa_family);
		return -1;
	}

	LOG_INFO("Sent SOCKS5 CONNECT success to client fd=%d", tunnel->client_sock->fd);

	return 0;
}

int tunnel_write_client(tunnel_t *tunnel, void *src, size_t size)
{
	if (tunnel->client_sock == NULL) return -1;

	if (buff_write(tunnel->client_sock->write_buff, src, size) < 0) return -1;

	epoll_modify(tunnel->client_sock, 1, 1);
	return 0;
}

int tunnel_connecting_handle(tunnel_t *tunnel)
{
	int error;
	socklen_t len = sizeof(error);
	int code = getsockopt(tunnel->remote_sock->fd, SOL_SOCKET, SO_ERROR, &error, &len);
	/*
	 * If error occur, Solairs return -1 and set error to errno.
	 * Berkeley return 0 but not set errno.
	 */
	if (code < 0 || error) {
		if (error) errno = error;
		return -1;
	}

	LOG_INFO("Remote connection established on fd=%d", tunnel->remote_sock->fd);

	tunnel->state = connected_state;
	tunnel->remote_sock->state = sock_connected;
	return tunnel_notify_connected(tunnel);
}
