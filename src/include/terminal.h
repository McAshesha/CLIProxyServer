#ifndef TERMINAL_H
#define TERMINAL_H

#include <stdbool.h>

/**
 * Start background thread to read console commands.
 * Commands:
 *   freeze – toggle freeze mode
 *   stop   – gracefully shutdown via SIGINT
 */
void  terminal_start(void);

/**
 * Check if proxy is currently frozen.
 * User code (e.g. tunnel_connected_handle) can skip forwarding when true.
 */
bool  terminal_is_frozen(void);

#endif // TERMINAL_H
