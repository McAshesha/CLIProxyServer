#ifndef SERVER_H
#define SERVER_H

/*
 * Тип callback для события готовых данных:
 * fd — дескриптор с данными
 * ud — пользовательский контекст (обычно структура сокета)
 */
typedef void read_cb(int fd, void *ud);

/*
 * Основная структура сервера-прокси
 */
typedef struct server {
    int        listenfd;      // Дескриптор слушающего сокета
    read_cb   *read_handle;   // Функция-хэндлер для входящих данных
    int        epollfd;       // Дескриптор epoll для мультиплексинга
    char       username[255]; // Логин для SOCKS5 ауты
    char       passwd[255];   // Пароль для SOCKS5 ауты
} server_t;

/*
 * Глобальный серверный объект для всей проги
 */
extern server_t SERVER;

/*
 * Настройка сигналов:
 * игнорит SIGPIPE, чтобы write не падал при закрытии соединения
 * ставит свой хэндлер на SIGINT для правильного выхода
 */
void sigign(void);

/*
 * Запускает главный event-цикл через epoll
 * Возвращает 0 при норме или <0 при ошибке
 */
int server_start(void);

/*
 * Инициализирует сервер:
 * host, port — для bind слушающего сокета
 * username, passwd — для SOCKS5 ауты
 * Внутри: getaddrinfo, неблокирующий сокет, bind, listen, создаёт epoll, добавляет сокет в мониторинг
 * Возвращает 0 при успехе, <0 при ошибке
 */
int server_init(char *host, char *port, char *username, char *passwd);

#endif // SERVER_H
