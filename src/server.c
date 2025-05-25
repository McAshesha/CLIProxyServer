#include "include/server.h"
#include "include/logger.h"
#include "include/client.h"

#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/socket.h>

void init_signal_handling(void)
{
    signal(SIGPIPE, SIG_IGN);
}

int setup_listener(unsigned short port, int backlog)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        log_error("socket() failed: %s", strerror(errno));
        return -1;
    }

    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        log_error("setsockopt() failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    struct sockaddr_in srv = {0};
    srv.sin_family      = AF_INET;
    srv.sin_addr.s_addr = INADDR_ANY;
    srv.sin_port        = htons(port);

    if (bind(fd, (struct sockaddr *)&srv, sizeof(srv)) < 0) {
        log_error("bind() failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    if (listen(fd, backlog) < 0) {
        log_error("listen() failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    return fd;
}

void run_server(int listen_fd)
{
    while (true) {
        struct sockaddr_storage cli_addr;
        socklen_t             cli_len = sizeof(cli_addr);
        int                   *pfd    = malloc(sizeof(int));

        if (!pfd) {
            log_error("malloc() failed");
            continue;
        }

        *pfd = accept(listen_fd, (struct sockaddr *)&cli_addr, &cli_len);
        if (*pfd < 0) {
            log_error("accept() failed: %s", strerror(errno));
            free(pfd);
            continue;
        }

        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, pfd) != 0) {
            log_error("pthread_create() failed");
            close(*pfd);
            free(pfd);
            continue;
        }
        pthread_detach(tid);
    }
}
