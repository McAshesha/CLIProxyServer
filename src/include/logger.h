#ifndef LOGGER_H
#define LOGGER_H

#include <stdarg.h>

// Logs a formatted INFO-level message to stdout
void log_info(const char *format, ...);

// Logs a formatted ERROR-level message to stderr
void log_error(const char *format, ...);

#endif // LOGGER_H