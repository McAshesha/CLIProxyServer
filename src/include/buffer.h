// buffer.h

#ifndef BUFF_H
#define BUFF_H


#include <stddef.h>


typedef struct buffer
{
	char   *data;
	size_t  write_index;
	size_t  read_index;
	size_t  cap;
} buffer_t;


buffer_t *buffer_create(size_t capacity);

void      buffer_release(buffer_t *buffer);

int       buffer_readfd(buffer_t *buffer, int fd);

int       buffer_writefd(buffer_t *buffer, int fd);

void     *buffer_read(buffer_t *buffer, void *dst, size_t size);

void      buffer_skip(buffer_t *buffer, size_t size);

int       buffer_write(buffer_t *buffer, void *src, size_t size);

int       buffer_concat(buffer_t *front, buffer_t *rear);

size_t    buffer_readable(buffer_t *buffer);

void      buffer_clear(buffer_t *buffer);

#endif // BUFF_H
