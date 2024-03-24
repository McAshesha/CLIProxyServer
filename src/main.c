#define _POSIX_C_SOURCE 200112L  // Определяем макрос для врубания расширенных POSIX-фич

#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "logger.h"
#include "server.h"
#include "terminal.h"

/*
 * Задаём размеры буферов для адреса, порта и других строк.
 * Используем enum для удобства и size_t, но это константы.
 */
typedef enum size_var
{
    SIZE_ADDR = 64,  // Макс длина IP или хоста
    SIZE_PORT = 16,  // Макс длина порта
    SIZE_OTH  = 255  // Размер для остального — логин, пароль, имя лога
} size_var_t;

/*
 * Триггерится, когда аргументы командной строки невалидные или неполные.
 * В лог кладёт предупреждения с описанием всех доступных опций.
 */
static void usage()
{
    LOG_WARN("WOWOWOWOW Usage:");
    LOG_WARN("  -o <optional> : file name for log (if not specified, log is output to stdout)");
    LOG_WARN("  -a <required> : IP address or host name for server bind address");
    LOG_WARN("  -p <required> : port for server bind address");
    LOG_WARN("  -u <optional> : login for SOCKS5 authentication (can be omitted if not required)");
    LOG_WARN("  -k <optional> : password for SOCKS5 authentication (can be omitted if not required)");
}

/*
 * Функция parse_args: парсит аргументы командной строки через getopt.
 */
static void parse_args(int n, char **args,
                       char addr[SIZE_ADDR], char port[SIZE_PORT],
                       char username[SIZE_OTH], char passwd[SIZE_OTH],
                       char outfile[SIZE_OTH])
{
    char option;
    // getopt выдаёт следующий символ опции или -1, когда все опции обработаны.
    while ((option = getopt(n, args, "a:p:u:k:o:")) > 0)
    {
        switch (option)
        {
            case 'a':
            {
                // Копируем строку адреса в буфер addr
                strncpy(addr, optarg, SIZE_ADDR);
                break;
            }
            case 'p':
            {
                // Аналогично копируем порт
                strncpy(port, optarg, SIZE_PORT);
                break;
            }
            case 'u':
            {
                // Логин для SOCKS5-аутентификации
                strncpy(username, optarg, SIZE_OTH);
                break;
            }
            case 'k':
            {
                // Пароль для SOCKS5-аутентификации
                strncpy(passwd, optarg, SIZE_OTH);
                break;
            }
            case 'o':
            {
                // Имя файла для логирования
                strncpy(outfile, optarg, SIZE_OTH);
                break;
            }
        }
    }
}

/*
 * Возвращает EXIT_SUCCESS при успешной работе, EXIT_FAILURE при возникновении ошибки.
 */
int main(int n, char **args)
{
    // Буферы для хранения параметров. Инициализируем пустыми строками.
    char addr[SIZE_ADDR]       = "";
    char port[SIZE_PORT]       = "";
    char username[SIZE_OTH]    = "";
    char passwd[SIZE_OTH]      = "";
    char outfile[SIZE_OTH]     = "";

    // Разбираем аргументы командной строки и заполняем буферы
    parse_args(n, args,
               addr, port,
               username, passwd,
               outfile);

    // Инициализируем логгер: если outfile пуст, лог при старте будет записываться в stdout
    log_init(outfile, INFO);

    // Проверяем, что обязательные параметры заданы: и addr, и port должно быть хоть че т
    if (strcmp(port, "") == 0 || strcmp(addr, "") == 0)
    {
        // Если хотя бы один из них пуст, выводим гайд для дауна
        usage();
        return EXIT_FAILURE;
    }

    // Настраиваем обработку сигналов: игнорить SIGPIPE и ловить SIGINT
    sigign();

    // Запускаем фоновый поток для чтения команд из терминала (freeze/stop)
    terminal_start();

    // Логируем информацию о конфигурации сервера
    LOG_INFO("Configured server at %s:%s (user=%s)", addr, port, username[0] ? username : "<none>");

    // Инициализируем сервер
    if (server_init(addr, port, username, passwd) < 0)
    {
        // server_init уже записал ошибку в лог внутри себя (наверное? ну должен наверно, хз), просто завершаемся
        return EXIT_FAILURE;
    }

    LOG_INFO("Server initialization OK on %s:%s", addr, port);

    // Запускаем основной цикл обработки событий через epoll (крутая штука неблокирующая поток)
    if (server_start() < 0)
    {
        // В случае ошибки epoll или другого сбоя — завершаемся с ошибкой (а с какой - похер)
        return EXIT_FAILURE;
    }

    // кидаем EXIT_SUCCESS, сообщая оболочке об успешном завершении.
    return EXIT_SUCCESS;
}
