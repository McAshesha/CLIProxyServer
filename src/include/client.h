#ifndef CLIENT_H
#define CLIENT_H

#include <stdlib.h>

/**
 * Thread entry for each SOCKS5 client.
 * arg is a pointer to an int holding the client socket fd.
 */
void *handle_client(void *arg);

#endif // CLIENT_H
