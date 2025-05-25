#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>
#include <sys/types.h>

// Dumps 'length' bytes of 'data' in hex, prefixed by 'prefix'
void hexdump(const char *prefix, const unsigned char *data, size_t length);

// Safe wrappers around read()/write(), logging errors on failure
ssize_t safe_read(int fd, void *buf, size_t size);
ssize_t safe_write(int fd, const void *buf, size_t size);

// Read or write exactly 'length' bytes (retrying EINTR), or return ≤0 on error/EOF
ssize_t safe_read_n(int fd, void *buffer, size_t length);
ssize_t safe_write_n(int fd, const void *buffer, size_t length);

#endif // UTILS_H
