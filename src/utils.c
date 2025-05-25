#include "include/utils.h"
#include "include/logger.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

void hexdump(const char *prefix, const unsigned char *data, size_t length)
{
    /* Allocate on heap to avoid large VLAs on stack */
    size_t buffer_size = length * 3 + 1;
    char *hex_string = malloc(buffer_size);
    if (!hex_string)
    {
        log_error("hexdump: malloc failed");
        return;
    }

    size_t pos = 0;
    for (size_t i = 0; i < length; ++i)
    {
        int written = snprintf(
            hex_string + pos,
            buffer_size - pos,
            "%02x ",
            data[i]);
        if (written < 0)
        {
            log_error("hexdump: snprintf failed");
            free(hex_string);
            return;
        }
        pos += (size_t)written;
    }

    log_info("%s [hex, %zu bytes]: %s", prefix, length, hex_string);
    free(hex_string);
}

ssize_t safe_read(int file_descriptor, void *buffer, size_t count)
{
    ssize_t bytes_read = read(file_descriptor, buffer, count);

    if (bytes_read < 0)
    {
        log_error("read() failed: %s", strerror(errno));
    }

    return bytes_read;
}

ssize_t safe_write(int file_descriptor, const void *buffer, size_t count)
{
    ssize_t bytes_written = write(file_descriptor, buffer, count);

    if (bytes_written < 0)
    {
        log_error("write() failed: %s", strerror(errno));
    }

    return bytes_written;
}

ssize_t safe_read_n(int file_descriptor, void *buffer, size_t total_size)
{
    size_t bytes_read_total = 0;

    while (bytes_read_total < total_size)
    {
        ssize_t bytes_read = read(
            file_descriptor,
            (char *)buffer + bytes_read_total,
            total_size - bytes_read_total);

        if (bytes_read < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            log_error("safe_read_n failed: %s", strerror(errno));
            return -1;
        }

        if (bytes_read == 0)
        {
            return bytes_read_total;
        }

        bytes_read_total += (size_t)bytes_read;
    }

    return bytes_read_total;
}

ssize_t safe_write_n(int file_descriptor, const void *buffer, size_t total_size)
{
    size_t bytes_written_total = 0;

    while (bytes_written_total < total_size)
    {
        ssize_t bytes_written = write(
            file_descriptor,
            (const char *)buffer + bytes_written_total,
            total_size - bytes_written_total);

        if (bytes_written < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            log_error("safe_write_n failed: %s", strerror(errno));
            return -1;
        }

        bytes_written_total += (size_t)bytes_written;
    }

    return bytes_written_total;
}
