#ifndef SOCK_H
#define SOCK_H

#include "buffer.h"
#include "tunnel.h"

/*
 * Тип функции для чтения события:
 * fd — дескриптор сокета с данными
 * ud — указатель на контекст пользователя (обычно sock\_t\*)
 */
typedef void read_cb(int fd, void *ud);

/*
 * Тип функции для записи события:
 * fd — дескриптор сокета, готов к записи
 * ud — указатель на пользовательский контекст (обычно sock\_t\*)
 */
typedef void write_cb(int fd, void *ud);

/*
 * Состояния сокета внутри туннеля
 */
typedef enum sock_state {
    sock_connecting,   // Неблокирующее подключение к удалённому серверу
    sock_connected,    // Сокет готов к обмену данными
    sock_halfclosed,   // Получен FIN — чтение оканчивается, но можно ещё писать
    sock_closed        // Сокет полностью закрыт
} sock_state_t;

/*
 * Основная структура для руля одного сокета
 */
struct sock {
    int            fd;             // Сокет-дескриптор
    read_cb       *read_handle;    // Хэндлер на EPOLLIN
    write_cb      *write_handle;   // Хэндлер на EPOLLOUT
    buffer_t      *read_buffer;    // Буфер для входящих данных
    buffer_t      *write_buffer;   // Буфер для исходящих данных
    tunnel_t      *tunnel;         // Связь с родительским туннелем
    sock_state_t   state;          // Текущий стейт соединения
    int            is_client;      // Флаг: клиент (1) или удалённый (0) сокет
};

/*
 * Вкидывает сокет в epoll сервера для слежки за чтением
 * Возвращает 0, если всё чётко, или <0 при баге
 */
int epoll_add(sock_t *sock);

/*
 * Апдейтит набор epoll-событий: writable=1 — врубаем EPOLLOUT, readable=1 — врубаем EPOLLIN
 * Возвращает 0 при норме, <0 при ошибке
 */
int epoll_modify(sock_t *sock, int writable, int readable);

/*
 * Создаёт sock\_t с буферами для чтения/записи
 * fd — уже открытый дескриптор
 * state — стартовый стейт (напр. sock\_connecting)
 * isclient — клиентский (1) или удалённый (0) сокет
 * tunnel — указатель на родительский туннель
 * Возвращает указатель на sock\_t при успехе, или NULL при ошибке
 */
sock_t* sock_create(int fd, sock_state_t state, int is_client, tunnel_t *tunnel);

/*
 * Выполняет полухлопок соединения: помечает сокет как закрытый на приём,
 * форвардит оставшиеся данные и, когда буфер пуст, закрывает соединение
*/
void sock_shutdown(sock_t *sock);

/*
 * Принудительно закрывает соединение и освобождает все ресурсы
 */
void sock_force_shutdown(sock_t *sock);

/*
 * Переводит файловый дескриптор в неблокирующий режим с помощью fcntl
 * Возвращает 0 при успехе или отрицательное число при ошибке
 */
int sock_nonblocking(int fd);

/*
 * Включает TCP KEEPALIVE на сокете для ловли «мертвых» соединений
 * Возвращает 0, если ок, или <0 при ошибке
 */
int sock_keepalive(int fd);

#endif // SOCK_H
