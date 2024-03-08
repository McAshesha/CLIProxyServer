#include <sys/socket.h>    // socket, bind, listen, accept
#include <stdbool.h>       // булев тип
#include <signal.h>        // структуры и функции для обработки сигналов
#include <errno.h>         // errno
#include <netdb.h>         // getaddrinfo, addrinfo
#include <stdio.h>         // snprintf
#include <string.h>        // memset, strerror
#include <netinet/in.h>    // sockaddr_in и родственные типы
#include <sys/epoll.h>     // epoll_create, epoll_ctl, epoll_wait
#include <stdlib.h>        // exit, freeaddrinfo

#include "sock.h"          // обёртки для неблокирующих сокетов
#include "logger.h"        // логгирование
#include "server.h"        // заголовок модуля сервера
#include "tunnel.h"        // создание и управление туннелем SOCKS5

#define MAX_EPOLL_EVENTS 64
#define BLACKLOG         1024

// Глобальная структура сервера
server_t SERVER;


typedef struct epoll_event epoll_event_t;
typedef struct addrinfo      addrinfo_t;


/*
 * Вспомогательная функция: обработка нового входящего соединения
 */
static void accept_handle(void)
{
    // Принимаем новое соединение без получения адреса клиента
    int newfd = accept(SERVER.listenfd, NULL, NULL);
    if (newfd < 0)
    {
        // В случае ошибки логируем и выходим из функции, но всё ещё продолжаем работу сервера
        LOG_ERROR("Failed accept_handle, listenfd=%d, err=%s",
                  SERVER.listenfd, strerror(errno));
        return;
    }

    // Логируем успешное принятие нового клиента
    LOG_INFO("New client connection accepted: fd=%d", newfd);
    // Создаём новый объект туннеля, который будет обрабатывать SOCKS5 для этого клиента
    tunnel_create(newfd);
}

/*
 * Обработчик сигнала SIGINT: корректно завершаем работу сервера
 */
static void handle_signal(int signo)
{
    // Выводим предупреждение в терминал, затем выходим
    EXTRA_LOG_WARN("The proxy server was forcibly stopped");
    exit(EXIT_SUCCESS);
}

/*
 * Настройка игнорирования SIGPIPE и обработчика SIGINT
 */
void sigign(void)
{
    struct sigaction sa;
    // Игнорируем SIGPIPE, чтобы запись в закрытые сокеты не приводила к завершению процесса
    sa.sa_handler = SIG_IGN;
    sa.sa_flags   = 0;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGPIPE, &sa, NULL);

    // Для SIGINT устанавливаем наш обработчик
    signal(SIGINT, handle_signal);
}

/*
 * Запуск основного цикла обработки событий
 */
int server_start(void)
{
    // Массив для приёма событий от epoll
    epoll_event_t events[MAX_EPOLL_EVENTS];

    LOG_INFO("Entering epoll loop, epollfd=%d", SERVER.epollfd);

    // Бесконечный цикл ожидания событий
    while (true)
    {
        // Ожидаем события (блокирующий вызов)
        int n = epoll_wait(SERVER.epollfd, events, MAX_EPOLL_EVENTS, -1);
        // Если произошла ошибка, отличная от прерывания, завершаем с ошибкой
        if (n < 0 && errno != EINTR)
        {
            LOG_ERROR("Failed epoll_wait: error=%s", strerror(errno));
            return -1;
        }

        // Обрабатываем каждое событие
        for (int i = 0; i < n; ++i)
        {
            // В user data epoll мы сохраняем указатель на дескриптор fd
            void *ud         = events[i].data.ptr;
            int   current_fd = *(int *)ud;
            int   ev         = events[i].events;

            // Обработка готовности на чтение
            if (ev & EPOLLIN)
            {
                // Если событие на слушающем сокете — принимаем нового клиента
                if (current_fd == SERVER.listenfd)
                {
                    accept_handle();
                }
                else
                {
                    // Иначе передаём управление туннелю клиента
                    tunnel_read_handle(current_fd, ud);
                }
            }
            // Обработка готовности на запись
            else if (ev & EPOLLOUT)
            {
                // Передаём событие записи туннелю
                tunnel_write_handle(current_fd, ud);
            }
            else
            {
                // Логируем неожиданные флаги событий
                LOG_ERROR("Unexpected epoll events: 0x%x on fd=%d", ev, current_fd);
            }
        }
    }

    // Код не достижим, но возвращаем 0 для полноты
    return 0;
}

/*
 * Настройка слушающего сокета и epoll
 */
int server_init(char *host, char *port, char *username, char *passwd)
{
    addrinfo_t hint;
    // Обнуляем структуру перед заполнением
    memset(&hint, 0, sizeof(hint));
    // Разрешаем любой IP протокол (IPv4 или IPv6)
    hint.ai_family   = AF_UNSPEC;
    // Только TCP-соединения
    hint.ai_socktype = SOCK_STREAM;
    hint.ai_protocol = IPPROTO_TCP;

    // Получаем список адресов для привязки
    addrinfo_t *address_list = NULL;
    if (getaddrinfo(host, port, &hint, &address_list) != 0)
    {
        LOG_ERROR("Failed init_server, getaddrinfo failed error=%s", gai_strerror(errno));
        return -1;
    }

    int listenfd = -1;
    addrinfo_t *ai;
    // Перебираем все возможные адреса до успешного создания сокета
    for (ai = address_list; ai != NULL; ai = ai->ai_next)
    {
        listenfd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (listenfd < 0)
            continue;

        // Делаем сокет неблокирующим
        sock_nonblocking(listenfd);
        break;
    }

    if (listenfd < 0)
    {
        LOG_ERROR("Failed init_server, listenfd create failed errno=%s", strerror(errno));
        freeaddrinfo(address_list);
        return -1;
    }

    // Позволяем быстро перезапустить сервер, не дожидаясь освобождения адреса
    int reuse_flag = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse_flag, sizeof(reuse_flag));

    // Привязываем сокет к адресу и порту
    if (bind(listenfd, ai->ai_addr, ai->ai_addrlen) != 0)
    {
        LOG_ERROR("Failed bind, errno=%s", strerror(errno));
        freeaddrinfo(address_list);
        return -1;
    }
    freeaddrinfo(address_list);

    // Начинаем прослушивание входящих соединений
    if (listen(listenfd, BLACKLOG) != 0)
    {
        LOG_ERROR("Failed listen, errno=%s", strerror(errno));
        return -1;
    }

    LOG_INFO("Listening socket fd=%d bound to %s:%s", listenfd, host, port);

    // Создаём epoll-инстанс для мониторинга событий
    int epollfd = epoll_create(1);
    if (epollfd < 0)
    {
        LOG_ERROR("Failed epoll_create, errno=%s", strerror(errno));
        return -1;
    }

    // Сохраняем дескрипторы и данные в глобальной структуре
    SERVER.listenfd = listenfd;
    SERVER.epollfd  = epollfd;
    // Сохраняем учётные данные для аутентификации клиентов
    snprintf(SERVER.username, sizeof(SERVER.username), "%s", username);
    snprintf(SERVER.passwd,   sizeof(SERVER.passwd),   "%s", passwd);

    // Регистрируем слушающий сокет в epoll на событие готовности чтения
    epoll_event_t event;
    event.events   = EPOLLIN;
    event.data.ptr = &SERVER.listenfd;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, listenfd, &event);

    return 0;
}
