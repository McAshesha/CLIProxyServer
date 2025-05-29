#include <sys/socket.h>
#include <stdbool.h>
#include <signal.h>
#include <bits/signum-generic.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <stdlib.h>

#include "sock.h"
#include "logger.h"
#include "server.h"


#define MAX_EPOLL_EVENTS 64
#define BLACKLOG 1024


server_t SERVER;


typedef struct epoll_event epoll_event_t;
typedef struct addrinfo addrinfo_t;


static void accept_handle()
{
	int newfd;
	if ((newfd = accept(SERVER.listenfd, NULL, NULL)) < 0) {
		LOG_ERROR("Failed accept_handle, listenfd=%d, err=%s", SERVER.listenfd, strerror(errno));
		return;
	}

	LOG_INFO("New client connection accepted: fd=%d", newfd);

	tunnel_create(newfd);
}

static void handle_signal()
{
	EXTRA_LOG_WARN("The proxy server was forcibly stopped");
	exit(EXIT_SUCCESS);
}

void sigign()
{
	struct sigaction sa;
	sa.sa_handler = SIG_IGN;
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGPIPE, &sa, 0);
	signal(SIGINT, handle_signal);
}

int server_start()
{
	epoll_event_t events[MAX_EPOLL_EVENTS];

	LOG_INFO("Entering epoll loop, epollfd=%d", SERVER.epollfd);

	while (true) {
		int n = epoll_wait(SERVER.epollfd, events, MAX_EPOLL_EVENTS, -1);
		if (n < 0 && errno != EINTR) {
			LOG_ERROR("Failed epoll_wait: error=%s", strerror(errno));
			return -1;
		}

		for (int i = 0; i < n; ++i) {
			void *cur_ud = events[i].data.ptr;
			int cur_fd = *(int*)cur_ud;
			int cur_events = events[i].events; // NOLINT(*-narrowing-conversions)
			if (cur_events & EPOLLIN) {
				if (cur_fd == SERVER.listenfd) {
					accept_handle();
				} else {
					tunnel_read_handle(cur_fd, cur_ud);
				}
			} else if(cur_events & EPOLLOUT) {
				tunnel_write_handle(cur_fd, cur_ud);
			} else {
				LOG_ERROR("Unexpected epoll events: 0x%x on fd=%d", cur_events, cur_fd);
			}
		}
	}

	return 0;
}

int server_init(char *host, char *port, char *username, char *passwd)
{
	addrinfo_t ai_hint;
	memset(&ai_hint, 0, sizeof(ai_hint));

	ai_hint.ai_family = AF_UNSPEC;
	ai_hint.ai_socktype = SOCK_STREAM;
	ai_hint.ai_protocol = IPPROTO_TCP;

	addrinfo_t *ai_list;
	addrinfo_t *ai_ptr;

	if (getaddrinfo(host, port, &ai_hint, &ai_list) != 0) {
		LOG_ERROR("Failed init_server, getaddrinfo failed error=%s", gai_strerror(errno));
		return -1;
	}

	int listenfd = -1;
	for (ai_ptr = ai_list; ai_ptr != NULL; ai_ptr = ai_ptr->ai_next) {
		listenfd = socket(ai_ptr->ai_family, ai_ptr->ai_socktype, ai_ptr->ai_protocol);
		if (listenfd < 0) continue;
		sock_nonblocking(listenfd);

		break;
	}

	if (listenfd < 0) {
		LOG_ERROR("Failed init_server, listenfd create failed errno=%s", strerror(errno));
		return -1;
	}

	int reuse = 1;
	setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (void*)&reuse, sizeof(reuse));

	if (bind(listenfd, ai_ptr->ai_addr, ai_ptr->ai_addrlen)) {
		LOG_ERROR("Failed bind, errno=%s", strerror(errno));
		return -1;
	}
	freeaddrinfo(ai_list);

	if (listen(listenfd, BLACKLOG) != 0) {
		LOG_ERROR("Failed listen, errno=%s", strerror(errno));
		return -1;
	}

	LOG_INFO("Listening socket fd=%d bound to %s:%s", listenfd, host, port);

	int epollfd = -1;
	if ((epollfd = epoll_create(1024)) < 0) {
		LOG_ERROR("Failed epoll_create, errno=%s", strerror(errno));
		return -1;
	}

	SERVER.epollfd = epollfd;
	SERVER.listenfd = listenfd;

	epoll_event_t event;
	event.events = EPOLLIN;
	event.data.ptr = &SERVER;
	epoll_ctl(epollfd, EPOLL_CTL_ADD, listenfd, &event);

	snprintf(SERVER.username, sizeof(SERVER.username), "%s", username);
	snprintf(SERVER.passwd, sizeof(SERVER.passwd), "%s", passwd);

	return 0;
}
