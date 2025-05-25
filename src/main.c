#include <stdlib.h>
#include <unistd.h>

#include "include/logger.h"
#include "include/server.h"

#define LISTEN_PORT 1080
#define BACKLOG      128

int main(void)
{
    init_signal_handling();

    int listen_fd = setup_listener(LISTEN_PORT, BACKLOG);
    if (listen_fd < 0) {
        return EXIT_FAILURE;
    }

    log_info("Listening on port %d", LISTEN_PORT);
    run_server(listen_fd);

    close(listen_fd);
    return EXIT_SUCCESS;
}
