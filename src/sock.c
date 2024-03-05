#include <unistd.h>
#include <sys/epoll.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>

#include "sock.h"
#include "logger.h"
#include "server.h"

#define INIT_BUFF_CAP 1024  // Стартовый размер буферов для чтения/записи


typedef struct epoll_event epoll_event_t;


/*
 * Добавляет сокет в epoll-инстанс сервера для отслеживания события чтения
 */
int epoll_add(sock_t *sock)
{
    epoll_event_t event;
    event.events   = EPOLLIN;     // Событие «готовность к чтению»
    event.data.ptr = sock;        // В user data сохраняем указатель на сокет
    return epoll_ctl(SERVER.epollfd, EPOLL_CTL_ADD, sock->fd, &event);
}

/*
 * Удаляет сокет из epoll-инстанса (статическая вспомогательная функция)
 */
static int epoll_del(const sock_t *sock)
{
    epoll_event_t event;
    return epoll_ctl(SERVER.epollfd, EPOLL_CTL_DEL, sock->fd, &event);
}

/*
 * Меняет отслеживаемые события epoll: writable=1 добавляет EPOLLOUT, readable=1 добавляет EPOLLIN
 */
int epoll_modify(sock_t *sock, int writable, int readable)
{
    epoll_event_t event;
    event.data.ptr = sock;
    event.events   = (writable ? EPOLLOUT : 0) | (readable ? EPOLLIN : 0);
    return epoll_ctl(SERVER.epollfd, EPOLL_CTL_MOD, sock->fd, &event);
}

/*
 * Создаёт и инициализирует структуру sock_t, выделяя буферы
 */
sock_t* sock_create(int fd, sock_state_t state, int is_client, tunnel_t *tunnel)
{
    // Выделяем память под структуру
    sock_t *sock = malloc(sizeof(*sock));
    if (!sock)
    {
        // Ошибка выделения памяти
        return NULL;
    }
    // Обнуляем поля, чтобы не было «мусора»
    memset(sock, 0, sizeof(*sock));

    // Создаём буфер для приёма данных
    sock->read_buffer = buffer_create(INIT_BUFF_CAP);
    if (!sock->read_buffer)
    {
        free(sock);
        return NULL;
    }

    // Создаём буфер для исходящих данных
    sock->write_buffer = buffer_create(INIT_BUFF_CAP);
    if (!sock->write_buffer)
    {
        buffer_release(sock->read_buffer);
        free(sock);
        return NULL;
    }

    // Инициализируем остальные поля
    sock->fd           = fd;
    sock->state        = state;
    sock->is_client    = is_client;
    sock->tunnel       = tunnel;
    sock->read_handle  = tunnel_read_handle;   // Назначаем колбэк для чтения
    sock->write_handle = tunnel_write_handle;  // Назначаем колбэк для записи

    return sock;
}

/*
 * Вспомогательная функция полного освобождения sock_t и связанных ресурсов
 */
static void sock_release(sock_t *sock)
{
    // Логируем закрытие сокета
    LOG_INFO("Closed and released sock fd=%d", sock->fd);

    tunnel_t *tunnel = sock->tunnel;

    // Освобождаем внутренние буферы
    buffer_release(sock->write_buffer);
    buffer_release(sock->read_buffer);

    // Обнуляем указатель в структуре туннеля
    if (sock->is_client)
    {
        tunnel->client_sock = NULL;
    }
    else
    {
        tunnel->remote_sock = NULL;
    }

    // Удаляем дескриптор из epoll и закрываем его
    epoll_del(sock);
    close(sock->fd);
    free(sock);

    // Если оба сокета туннеля закрыты, освобождаем сам туннель
    if (tunnel->remote_sock == NULL && tunnel->client_sock == NULL)
    {
        tunnel_release(tunnel);
    }
}

/*
 * Принудительное немедленное закрытие соединения и очистка ресурсов
 */
void sock_force_shutdown(sock_t *sock)
{
    LOG_ERROR("Forcing shutdown of fd=%d", sock->fd);
    sock_release(sock);
}

/*
 * Полухлопок соединения: прекращаем чтение, форвардим остаток и закрываем при пустом буфере
 */
void sock_shutdown(sock_t *sock)
{
    // Переводим состояние в полузакрытое
    sock->state = sock_halfclosed;

    LOG_INFO("Half-closing fd=%d", sock->fd);

    tunnel_t *tunnel = sock->tunnel;

    // Если туннель уже в состоянии connected, форвардим накопленные данные
    if (tunnel->state == connected_state)
    {
        if (sock->is_client && tunnel->remote_sock)
        {
            buffer_concat(tunnel->remote_sock->write_buffer, sock->read_buffer);
        }
        else if (tunnel->client_sock)
        {
            buffer_concat(tunnel->client_sock->write_buffer, sock->read_buffer);
        }
    }

    // Определяем, остались ли данные для записи
    int has_data = buffer_readable(sock->write_buffer) > 0;
    if (has_data)
    {
        // Если да, переключаем epoll на отслеживание записи
        epoll_modify(sock, 1, 0);
    }
    else
    {
        // Если буфер пуст, закрываем сразу
        sock_force_shutdown(sock);
    }
}

/*
 * Устанавливает неблокирующий режим флагов сокета
 */
int sock_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
    {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/*
 * Активирует TCP KEEPALIVE на сокете для периодической проверки доступности
 */
int sock_keepalive(int fd)
{
    int enable = 1;
    return setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &enable, sizeof(enable));
}
