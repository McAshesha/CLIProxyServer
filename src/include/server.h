#ifndef SERVER_H
#define SERVER_H

/**
 * Ignore SIGPIPE so failed writes don't kill the process.
 */
void init_signal_handling(void);

/**
 * Create, bind and listen on a TCP socket.
 * @param port    TCP port to bind.
 * @param backlog listen backlog.
 * @return socket fd, or -1 on error.
 */
int setup_listener(unsigned short port, int backlog);

/**
 * Accept loop: on each incoming connection, spawn a detached client handler thread.
 * Does not return.
 */
void run_server(int listen_fd);

#endif // SERVER_H
