#include "include/utils.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include "include/logger.h"

void hexdump(const char *prefix, const unsigned char *data, size_t length) {
    printf("%s [hex, %zu bytes]: ", prefix, length);
    for (size_t i = 0; i < length; ++i) {
        printf("%02x ", data[i]);
    }
    printf("\n");
}

ssize_t safe_read(int fd, void *buf, size_t size) {
    ssize_t ret = read(fd, buf, size);
    if (ret < 0) {
        log_error("read() failed: %s", strerror(errno));
    }
    return ret;
}

ssize_t safe_write(int fd, const void *buf, size_t size) {
    ssize_t ret = write(fd, buf, size);
    if (ret < 0) {
        log_error("write() failed: %s", strerror(errno));
    }
    return ret;
}

ssize_t safe_read_n(int fd, void *buffer, size_t length) {
    size_t total_read = 0;
    while (total_read < length) {
        ssize_t bytes = read(fd, (char*)buffer + total_read, length - total_read);
        if (bytes < 0) {
            if (errno == EINTR) continue;
            log_error("safe_read_n failed: %s", strerror(errno));
            return -1;
        }
        if (bytes == 0) {
            return total_read;
        }
        total_read += bytes;
    }
    return total_read;
}

ssize_t safe_write_n(int fd, const void *buffer, size_t length) {
    size_t total_sent = 0;
    while (total_sent < length) {
        ssize_t bytes = write(fd, (const char*)buffer + total_sent, length - total_sent);
        if (bytes < 0) {
            if (errno == EINTR) continue;
            log_error("safe_write_n failed: %s", strerror(errno));
            return -1;
        }
        total_sent += bytes;
    }
    return total_sent;
}
