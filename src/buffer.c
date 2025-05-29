#include <assert.h>
#include <malloc.h>
#include <string.h>
#include <unistd.h>

#include "buffer.h"


buffer_t *buffer_create(size_t capacity)
{
    buffer_t *buff = malloc(sizeof(*buff));
    if (buff == NULL)
    {
        return NULL;
    }
    memset(buff, 0, sizeof(*buff));

    char *data = malloc(capacity);
    if (data == NULL)
    {
        free(buff);
        return NULL;
    }

    buff->write_index = 0;
    buff->read_index  = 0;
    buff->cap         = capacity;
    buff->data        = data;

    return buff;
}

void buffer_release(buffer_t *buffer)
{
    free(buffer->data);
    free(buffer);
}

size_t buffer_readable(buffer_t *buffer)
{
    return buffer->write_index - buffer->read_index;
}

static size_t buffer_prependable(buffer_t *buff)
{
    return buff->read_index;
}

static size_t buffer_writable(buffer_t *buff)
{
    return buff->cap - buff->write_index;
}

static int buffer_expand(buffer_t *buff)
{
    int   newcap = buff->cap * 2;
    char *newdata = realloc(buff->data, newcap);
    if (newdata == NULL)
    {
        return -1;
    }

    buff->cap  = newcap;
    buff->data = newdata;
    return 0;
}

int buffer_readfd(buffer_t *buffer, int fd)
{
    size_t writable = buffer_writable(buffer);
    if (writable == 0)
    {
        if (buffer_expand(buffer) < 0)
        {
            return -1;
        }
        writable = buffer_writable(buffer);
    }

    ssize_t n = read(fd, buffer->data + buffer->write_index, writable);
    if (n <= 0)
    {
        return n;
    }

    buffer->write_index += n;
    return n;
}

int buffer_writefd(buffer_t *buffer, int fd)
{
    size_t  readable = buffer_readable(buffer);
    ssize_t n        = write(fd, buffer->data + buffer->read_index, readable);
    if (n <= 0)
    {
        return n;
    }

    buffer->read_index += n;
    return n;
}

void *buffer_read(buffer_t *buffer, void *dst, size_t size)
{
    size_t readable = buffer_readable(buffer);
    assert(size <= readable);

    memcpy(dst, buffer->data + buffer->read_index, size);
    buffer->read_index += size;
    return dst;
}

void buffer_skip(buffer_t *buffer, size_t size)
{
    size_t readable = buffer_readable(buffer);
    assert(size <= readable);

    buffer->read_index += size;
}

int buffer_write(buffer_t *buffer, void *src, size_t size)
{
    for (;;)
    {
        size_t writable   = buffer_writable(buffer);
        if (writable >= size)
        {
            break;
        }

        size_t prependable = buffer_prependable(buffer);
        if (prependable + writable >= size)
        {
            size_t readable = buffer_readable(buffer);
            memmove(buffer->data, buffer->data + buffer->read_index, readable);
            buffer->read_index  = 0;
            buffer->write_index -= prependable;
            break;
        }

        if (buffer_expand(buffer) < 0)
        {
            return -1;
        }
    }

    memcpy(buffer->data + buffer->write_index, src, size);
    buffer->write_index += size;
    return 0;
}

int buffer_concat(buffer_t *front, buffer_t *rear)
{
    return buffer_write(front,
                        rear->data + rear->read_index,
                        buffer_readable(rear));
}

void buffer_clear(buffer_t *buffer)
{
    buffer->write_index = 0;
    buffer->read_index  = 0;
}
