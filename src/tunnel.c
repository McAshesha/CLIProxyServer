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
#include <arpa/inet.h>

#include "tunnel.h"
#include "sock.h"
#include "logger.h"
#include "protocol_parser.h"
#include "terminal.h"



#if (EAGAIN != EWOULDBLOCK)
	#define EAGAIN_EWOULDBLOCK EAGAIN : case EWOULDBLOCK
#else
	#define EAGAIN_EWOULDBLOCK EAGAIN
#endif


typedef struct sockaddr sockaddr_t;

typedef struct sockaddr_in sockaddr_in_t;

typedef struct sockaddr_in6 sockaddr_in6_t;

typedef struct addrinfo addrinfo_t;


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

static void tunnel_shutdown(tunnel_t *tunnel)
{
	if (tunnel->client_sock != NULL) sock_shutdown(tunnel->client_sock);
	if (tunnel->remote_sock != NULL) sock_shutdown(tunnel->remote_sock);
}

static int tunnel_connected_handle(tunnel_t *tunnel, int is_client);

static int tunnel_connecting_handle(tunnel_t *tunnel);


void tunnel_read_handle(int fd, void *ud)
{
	sock_t *sock = (sock_t*)ud;
	tunnel_t *tunnel = sock->tunnel;

	int n = buffer_readfd(sock->read_buffer, fd);
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
			assert(sock->is_client == 0);
			if (tunnel_connecting_handle(tunnel) < 0) goto tunnel_shutdown;
			break;
		case connected_state:
			if (tunnel_connected_handle(tunnel, sock->is_client) < 0) goto tunnel_shutdown;
			break;
		default:
			assert(0);
			break;
	}

	LOG_INFO("Read %d bytes from %s (fd=%d), state=%d",
		 n, sock->is_client ? "client" : "remote", fd, tunnel->state);

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

	if (buffer_readable(sock->write_buffer) > 0) {
		int n = buffer_writefd(sock->write_buffer, fd);
		if (n <= 0) {
			switch (errno) {
				case EINTR:
				case EAGAIN_EWOULDBLOCK:
					break;
				default:
					goto force_shutdown;
			}
		}
		LOG_INFO("Wrote %d bytes to %s (fd=%d)", n, sock->is_client ? "client" : "remote", fd);

	} else if (sock->state == sock_halfclosed) {
		goto force_shutdown;
	}

	if (tunnel->state == connecting_state) {
		assert(sock->is_client == 0);

		if (tunnel_connecting_handle(tunnel) < 0) goto tunnel_shutdown;
	}

	int writable = buffer_readable(sock->write_buffer) > 0;
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

// Dump up to first 128 bytes (to avoid huge logs); adjust as needed
static void dump_hex(const char *label, const uint8_t *buf, size_t len) {
	size_t max = len < 128 ? len : 128;
	char hexstr[3 * 128 + 1] = {0};
	char *p = hexstr;
	for (size_t i = 0; i < max; ++i) {
		p += sprintf(p, "%02x ", buf[i]);
	}
	LOG_INFO("%s hex (%zu bytes): %s% s", label, len, hexstr,
			 (len > max ? "...(truncated)" : ""));
}

static int tunnel_connected_handle(tunnel_t *tunnel, int is_client)
{
	sock_t *sock_front = (is_client != 0) ? tunnel->remote_sock : tunnel->client_sock;
	sock_t *sock_rear = (is_client != 0) ? tunnel->client_sock : tunnel->remote_sock;

	char *label = (is_client != 0) ? "Forwarded client → remote" : "Forwarded remote → client";

	if (sock_front == NULL)
	{
		return -1;
	}

	buffer_t *rear_buffer = sock_rear->read_buffer;
	uint8_t *buffer_data = (uint8_t *) rear_buffer->data + rear_buffer->read_index;
	size_t length = buffer_readable(rear_buffer);

	if (!parse_and_log_http(buffer_data, length, is_client) &&
		!parse_and_log_websocket(buffer_data, length, is_client))
	{
		dump_hex(label, buffer_data, length);
	}

	// Если команда freeze активна — читаем, логируем, но не форвардим
	if (terminal_is_frozen())
	{
		return 0;
	}

	if (buffer_concat(sock_front->write_buffer, rear_buffer) < 0)
	{
		return -1;
	}
	buffer_clear(rear_buffer);
	epoll_modify(sock_front, 1, 1);

	return 0;
}

static int tunnel_notify_connected(tunnel_t *tunnel)
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

	if (buffer_write(tunnel->client_sock->write_buffer, src, size) < 0) return -1;

	epoll_modify(tunnel->client_sock, 1, 1);
	return 0;
}

static int tunnel_connecting_handle(tunnel_t *tunnel)
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

int tunnel_connect_to_remote(tunnel_t *tunnel)
{
	uint8_t atyp = tunnel->rp.atyp;
	char *addr;
	char ip[64];
	char port[16];

	snprintf(port, sizeof(port),"%d", ntohs(tunnel->rp.port));
	switch(atyp) {
		case 0x01: // ipv4
			inet_ntop(AF_INET, tunnel->rp.addr, ip, sizeof(ip));
			addr = ip;
			break;
		case 0x04: // ipv6
			inet_ntop(AF_INET6, tunnel->rp.addr, ip, sizeof(ip));
			addr = ip;
			break;
		case 0x03: // domain
			addr = tunnel->rp.addr;
			break;
		default:
			assert(0);
			break;
	}

	LOG_INFO("Resolving %s:%s", addr, port);

	addrinfo_t ai_hint;
	memset(&ai_hint, 0, sizeof(ai_hint));

	ai_hint.ai_family = AF_UNSPEC;
	ai_hint.ai_socktype = SOCK_STREAM;
	ai_hint.ai_protocol = IPPROTO_TCP;

	addrinfo_t *ai_list;
	addrinfo_t *ai_ptr;

	// TODO: getaddrinfo is a block function, try doing it in thread
	if (getaddrinfo(addr, port, &ai_hint, &ai_list) != 0) {
		LOG_ERROR("Failed getaddrinfo, addr=%s,port=%s, error=%s", addr, port, gai_strerror(errno));
		return -1;
	}

	int newfd = -1;
	int status;
	for (ai_ptr = ai_list; ai_ptr != NULL; ai_ptr = ai_ptr->ai_next) {
		newfd = socket(ai_ptr->ai_family, ai_ptr->ai_socktype, ai_ptr->ai_protocol);
		if (newfd < 0) continue;
		sock_nonblocking(newfd);
		sock_keepalive(newfd);

		status = connect(newfd, ai_ptr->ai_addr, ai_ptr->ai_addrlen);

		LOG_INFO("Connecting to remote %s:%s → fd=%d (status=%s)", addr, port, newfd,
			(status == 0 ? "immediate" : "in progress"));

		if (status != 0 && errno != EINPROGRESS) {
			close(newfd);
			newfd = -1;
			LOG_ERROR("Connect failed to %s:%s: %s", addr, port, gai_strerror(errno));
			continue;
		}

		break;
	}
	freeaddrinfo(ai_list);

	if (newfd < 0) return -1;

	sock_t *sock = sock_create(newfd, sock_connecting, 0, tunnel);
	if (sock == NULL) {
		close(newfd);
		return -1;
	}
	tunnel->remote_sock = sock;

	epoll_add(sock);
	epoll_modify(sock, 1, 1);

	if (status == 0) {
		tunnel->state = connected_state;
		sock->state = sock_connected;
		return tunnel_notify_connected(tunnel);
	} else {
		tunnel->state = connecting_state;
		sock->state = sock_connecting;
	}

	return 0;
}
