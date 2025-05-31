#ifndef TUNNEL_H
#define TUNNEL_H

#include <stddef.h>
#include "protocol.h"

/*
 * Тип данных для хранения сокетов клиента и удалённого сервера,
 * а также состояния и вспомогательных данных SOCKS5-сессии.
 */
typedef struct sock sock_t;

/*
 * Доступные состояния туннеля SOCKS5:
 * open_state       — ожидаем Client Greeting
 * auth_state       — выполняем USER/PASS аутентификацию
 * request_state    — обрабатываем клиентский запрос CONNECT
 * connecting_state — идёт неблокирующее соединение к удалённому хосту
 * connected_state  — туннель установлен, двунаправленный форвардинг
 */
typedef enum tunnel_state
{
    open_state,
    auth_state,
    request_state,
    connecting_state,
    connected_state
} tunnel_state_t;

/*
 * Основная структура туннеля:
 * client_sock — сокет клиента, откуда читаем запросы
 * remote_sock — сокет удалённого сервера для форварда
 * state       — текущий стейт SOCKS5 протокола
 * op, ap, rp  — данные для greeting, auth и request этапов
 * read_count  — сколько байт прочитано на этапе
 * closed      — флаг, что туннель закрыт (пока не используется)
 */
typedef struct tunnel
{
    sock_t          *client_sock;
    sock_t          *remote_sock;
    tunnel_state_t   state;
    open_protocol_t  op;
    auth_protocol_t  ap;
    request_protocol_t rp;
    size_t           read_count;
    int              closed;
} tunnel_t;

/*
 * Создаёт новый туннель для принятого клиентского соединения.
 * В случае ошибки освобождает ресурсы и закрывает fd.
 */
tunnel_t* tunnel_create(int fd);

/*
 * Освобождает память, выделенную под tunnel_t.
 */
void tunnel_release(tunnel_t *tunnel);

/*
 * Обработчик EPOLLIN для сокетов туннеля.
 * В зависимости от tunnel->state запускает:
 * tunnel_open_handle       — greeting
 * tunnel_auth_handle       — auth
 * tunnel_request_handle    — request
 * tunnel_connecting_handle — проверка неблокинг connect
 * tunnel_connected_handle  — двунаправленный форвард
 * При ошибках              — полухлопок или форс выход.
*/
void tunnel_read_handle(int fd, void *ud);

/*
 * Обработчик события EPOLLOUT на одном из сокетов туннеля.
 * Пытается дописать накопленные данные из буфера
 */
void tunnel_write_handle(int fd, void *ud);

/*
 * Записывает произвольный блок данных клиенту в его write_buffer,
 * Возвращает 0 при успехе, <0 при ошибке.
 */
int tunnel_write_client(tunnel_t *tunnel, void *src, size_t size);

/*
 * Запускает коннект к удалённому хосту по параметрам.
 * Создаёт remote\_sock и вписывает в epoll.
 * Переходит в connecting\_state или сразу connected\_state.
 */
int tunnel_connect_to_remote(tunnel_t *tunnel);

#endif // TUNNEL_H
