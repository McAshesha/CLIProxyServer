#include <string.h>
#include <malloc.h>
#include <unistd.h>
#include <assert.h>

#include "buffer.h"


buffer_t* buffer_create(size_t cap)
{
	buffer_t *buff = (buffer_t*)malloc(sizeof(*buff));
	if (buff == NULL)
	{
		return NULL;
	}
	memset(buff, 0, sizeof(*buff));
	
	char *data = (char*)malloc(cap);
	if (data == NULL)
	{
		free(buff);
		return NULL;
	}

	buff->write_index = 0;
	buff->read_index = 0;
	buff->cap = cap;
	buff->data = data;

	return buff;
}

void buffer_release(buffer_t *buff)
{
	free(buff->data);
	free(buff);
}

size_t buffer_readable(buffer_t *buff)
{
	return buff->write_index - buff->read_index;
}

static size_t buffer_prependable(buffer_t* buff)
{
	return buff->read_index;
}


static size_t buffer_writable(buffer_t *buff)
{
	return buff->cap - buff->write_index;
}

static int buffer_expand(buffer_t *buff)
{
	int newcap = buff->cap * 2;
	char *newdata = realloc(buff->data, newcap);
	if (newdata == NULL)
	{
		return -1;
	}

	buff->cap = newcap;
	buff->data = newdata;
	return 0;
}

int buffer_readfd(buffer_t *buff, int fd)
{
	int writable = buffer_writable(buff);
	if (writable <= 0)
	{
		assert(writable == 0);

		if (buffer_expand(buff) < 0) return -1;
		writable = buffer_writable(buff);
	}

	int n = read(fd, buff->data + buff->write_index, writable);
	if (n <= 0)
	{
		return n;
	}

	buff->write_index += n;
	return n;
}

int buffer_writefd(buffer_t *buff, int fd)
{
	int readable = buffer_readable(buff);
	int n = write(fd, buff->data + buff->read_index, readable);
	if (n <= 0)
	{
		return n;
	}

	buff->read_index += n;
	return n;
}

void* buffer_read(buffer_t *buff, void *dst, size_t size)
{
	size_t readable = buffer_readable(buff);
	assert(size <= readable);

	memcpy(dst, buff->data + buff->read_index, size);
	buff->read_index += size;
	return dst;
}

void buffer_skip(buffer_t *buff, size_t size)
{
	size_t readable = buffer_readable(buff);
	assert(size <= readable);

	buff->read_index += size;
}

/*
 * 1. If writable >= size then append to buff
 * 2. If prepandable + writable >= size then move readable content to 0
 * 3. Otherwise, expand buff
 * 
 * repeat until 1 or 2 satisfied
 */
int buffer_write(buffer_t *buff, void *src, size_t size)
{
	for(;;)
	{
		size_t writable = buffer_writable(buff);
		if (writable >= size)
		{
			break;
		}

		size_t prependable = buffer_prependable(buff);
		if (prependable + writable >= size)
		{ // move readable content to 0
			int readable = buffer_readable(buff);
			memmove(buff->data, buff->data + buff->read_index, readable);
			buff->read_index = 0;
			buff->write_index -= prependable;
			break;
		}

		if (buffer_expand(buff) < 0)
		{
			return -1;
		}
	}

	memcpy(buff->data + buff->write_index, src, size);
	buff->write_index += size;
	return 0;
}

int buffer_concat(buffer_t *front, buffer_t *rear)
{
	return buffer_write(front, rear->data + rear->read_index, buffer_readable(rear));
}

void buffer_clear(buffer_t *buff)
{
	buff->write_index = 0;
	buff->read_index = 0;
}
