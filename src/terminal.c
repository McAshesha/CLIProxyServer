#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>

#include "terminal.h"
#include "logger.h"


/* safe flag type for signal handlers and threads */
static volatile sig_atomic_t freeze_flag = 0;

/* thread entry: read lines from stdin */
static void *terminal_thread(void *arg)
{
    char line[64];
    (void)arg;

    while (fgets(line, sizeof(line), stdin) != NULL)
    {
        /* strip newline */
        line[strcspn(line, "\r\n")] = '\0';

        if (strcmp(line, "freeze") == 0)
        {
            freeze_flag = !freeze_flag;
            EXTRA_LOG_WARN("Terminal → freeze %s",
                          freeze_flag ? "ON" : "OFF");
        }
        else if (strcmp(line, "stop") == 0)
        {
            EXTRA_LOG_WARN("Terminal → stop");
            raise(SIGINT);
            break;
        }
        else
        {
            EXTRA_LOG_WARN("Unknown command: '%s'", line);
        }
    }

    return NULL;
}

void terminal_start(void)
{
    pthread_t tid;

    if (pthread_create(&tid, NULL, terminal_thread, NULL) != 0)
    {
        LOG_ERROR("Failed to launch terminal thread");
        return;
    }

    pthread_detach(tid);
}

bool terminal_is_frozen(void)
{
    return (bool)freeze_flag;
}
