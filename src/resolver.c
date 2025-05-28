#include <unistd.h>
#include <sys/epoll.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "resolver.h"
#include "logger.h"
#include "sock.h"


typedef struct addrinfo addrinfo_t;


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
