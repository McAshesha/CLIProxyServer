#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>

#include "buffer.h"


#define ERROR_RETURN -1
#define OK_RETURN 0


/*
 * Инициализируем буфер с запасом в capacity байт
 * чтобы сразу можно было жёстко стартануть с нужным размером.
 */
buffer_t *buffer_create(size_t capacity)
{
    // Память чтобы завести контейнер для данных и всех нужных фишек буфера.
    buffer_t *buffer = malloc(sizeof(*buffer));
    if (buffer == NULL)
    {
        return NULL;
    }
    // Обнуляем все поля, чтобы не было мусора — так надежнее и без подвохов.
    memset(buffer, 0, sizeof(*buffer));

    // Резервим память под сам массив данных — чтоб туда паковать весь наш трафик без тормозов.
    char *data = malloc(capacity);
    if (data == NULL)
    {
        // Если что пошло не так — чистим память, чтобы не было утечек, и не ломаем систему.
        free(buffer);
        return NULL;
    }

    // Заливаем в буфер метаинфу — чтобы знать, что и как внутри рулит.
    buffer->write_index = 0;
    buffer->read_index  = 0;
    buffer->cap         = capacity;
    buffer->data        = data;

    return buffer;
}

/*
 * Чистим буфер, чтобы не было утечек памяти.
 */
void buffer_release(buffer_t *buffer)
{
    if (buffer == NULL)
    {
        return;
    }
    free(buffer->data);
    free(buffer);
}

/*
 * Хелперы
 */
size_t buffer_readable(buffer_t *buffer)
{
    assert(buffer != NULL);
    return buffer->write_index - buffer->read_index;
}

static size_t buffer_prependable(buffer_t *buffer)
{
    assert(buffer != NULL);
    return buffer->read_index;
}

static size_t buffer_writable(buffer_t *buffer)
{
    assert(buffer != NULL);
    return buffer->cap - buffer->write_index;
}

/*
 * Дублируем буфер, обновляем указатель data
 */
static int buffer_expand(buffer_t *buffer)
{
    if (buffer == NULL)
    {
        return ERROR_RETURN;
    }
    int   newcap  = buffer->cap * 2;   // новая ёмкость в 2 раза больше
    char *newdata = realloc(buffer->data, newcap); // попытка расширения
    if (newdata == NULL)
    {
        return ERROR_RETURN;
    }

    // Обновляем структуру буфера
    buffer->cap  = newcap;
    buffer->data = newdata;
    return OK_RETURN;
}

/*
 * Чтение из файлового дескриптора в буфер
 */
int buffer_readfd(buffer_t *buffer, int fd)
{
    size_t writable = buffer_writable(buffer);
    if (writable == 0)
    {
        // Нет места: пробуем расширить
        if (buffer_expand(buffer) == ERROR_RETURN)
        {
            return ERROR_RETURN;
        }
        writable = buffer_writable(buffer);
    }

    // Считываем не более writable байт
    ssize_t n = read(fd, buffer->data + buffer->write_index, writable);
    if (n <= 0)
    {
        // n == 0 — EOF, n < 0 — ошибка
        return n;
    }

    // Сдвигаем write_index на число прочитанных байт
    buffer->write_index += n;
    return n;
}

/*
 * Пишем из буфера в файловый дескриптор
 */
int buffer_writefd(buffer_t *buffer, int fd)
{
    size_t  readable = buffer_readable(buffer);
    ssize_t n        = write(fd, buffer->data + buffer->read_index, readable);
    if (n <= 0)
    {
        // n == 0 — не пашет, n < 0 — ошибка
        return n;
    }

    // Сдвигаем read_index на число записанных байт
    buffer->read_index += n;
    return n;
}

/*
 * Забираем size байт из буфера в dst
 */
void *buffer_read(buffer_t *buffer, void *dst, size_t size)
{
    size_t readable = buffer_readable(buffer);
    // Гарантируем, что доступно необходимое число байт
    assert(size <= readable);

    memcpy(dst, buffer->data + buffer->read_index, size);
    buffer->read_index += size;
    return dst;
}

/*
 * Скипаем size байт, двигая указатель чтения
 */
void buffer_skip(buffer_t *buffer, size_t size)
{
    size_t readable = buffer_readable(buffer);
    assert(size <= readable);
    buffer->read_index += size;
}

/*
 * Кидаем size байт из src в буфер, сдвигаем или расширяем при нужде
 */
int buffer_write(buffer_t *buffer, void *src, size_t size)
{
    while (true)
    {
        size_t writable   = buffer_writable(buffer);
        if (writable >= size)
        {
            // Места хватает без напряга
            break;
        }

        size_t prependable = buffer_prependable(buffer);
        if (prependable + writable >= size)
        {
            // Можно подвинуть данные в начало и освободить место
            size_t readable = buffer_readable(buffer);
            memmove(buffer->data, buffer->data + buffer->read_index, readable);
            buffer->read_index  = 0;
            buffer->write_index = readable;
            break;
        }

        // Если сдвинуть нельзя и места мало — увеличиваем буфер
        if (buffer_expand(buffer) == ERROR_RETURN)
        {
            return ERROR_RETURN; // Ошибка расширения
        }
    }

    // Копируем новые данные и двигаем write_index вперед
    memcpy(buffer->data + buffer->write_index, src, size);
    buffer->write_index += size;
    return OK_RETURN;
}

/*
 * Склеиваем буферы: переносим все байты из rear в front
 */
int buffer_concat(buffer_t *front, buffer_t *rear)
{
    return buffer_write(front,
                        rear->data + rear->read_index,
                        buffer_readable(rear));
}

/*
 * Полный ресет буфера — возвращаем индексы к старту
 */
void buffer_clear(buffer_t *buffer)
{
    buffer->write_index = 0;
    buffer->read_index  = 0;
}
