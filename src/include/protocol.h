#ifndef PROTOCOL_H
#define PROTOCOL_H


#include <stdint.h>


/*
 * Подрубаем главную структуру туннеля — хранит состояние SOCKS-сессии и нужные сокеты
 */
typedef struct tunnel tunnel_t;

/*
 * Структура для хранения приветственного сообщения от клиента (Client Greeting)
 * Согласно RFC1928:
 * +----+----------+----------+
 * |VER | NMETHODS | METHODS  |
 * +----+----------+----------+
 * | 1  |    1     | 1 to 255 |
 * +----+----------+----------+
 */
typedef struct open_protocol {
    uint8_t ver;           // Версия протокола (должна быть 0x05 для SOCKS5)
    uint8_t nmethods;      // Сколько методов ауты юзер скинул
    uint8_t methods[255];  // Список методов (каждый по 1 байту)
} open_protocol_t;

/*
 * Структура для аутентификации по USER/PASS (RFC1929)
 * +----+------+----------+------+----------+
 * |VER | ULEN |  UNAME   | PLEN |  PASSWD  |
 * +----+------+----------+------+----------+
 * | 1  |  1   | 1 to 255 |  1   | 1 to 255 |
 * +----+------+----------+------+----------+
 */
typedef struct auth_protocol {
    uint8_t ver;           // Версия схемы (чаще всего 0x01)
    uint8_t ulen;          // Длина юзернейма (байт)
    char    uname[255];    // Сам юзернейм
    uint8_t plen;          // Длина пароля (байт)
    char    passwd[255];   // Пароль
} auth_protocol_t;

/*
 * Структура для запроса клиента (CONNECT, BIND, UDP ASSOCIATE)
 * +----+-----+-------+------+----------+------+----------+
 * |VER | CMD |  RSV  | ATYP | DST.ADDR | DST.PORT |
 * +----+-----+-------+------+----------+------+----------+
 * | 1  |  1  | X'00' |  1   | Variable |    2    |
 * +----+-----+-------+------+----------+------+----------+
 */
typedef struct request_protocol {
    uint8_t  ver;         // Протокол версия — 0x05
    uint8_t  cmd;         // Команда: 0x01=CONNECT, 0x02=BIND, 0x03=UDP ASSOCIATE
    uint8_t  rsv;         // Зарезерв — всегда 0x00
    uint8_t  atyp;        // Тип адреса: 0x01 IPv4, 0x03 домен, 0x04 IPv6
    uint8_t  domainlen;   // Длина домена, если atyp == 0x03
    char     addr[255];   // Адрес: IPv4 (4 байта), домен (domainlen байт) или IPv6 (16 байт)
    uint16_t port;        // Порт в big-endian формате
} request_protocol_t;


/*
 * Обработчик GREETING:
 * - Ловит версию и число методов
 * - Забирает массив методов
 * - Выбирает ауту (NO AUTH или USER/PASS)
 * - Шлет ответ клиенту [VER, METHOD]
 * Возвращает:
 *   >0  — байты ответа записаны в буфер, можно инициировать запись
 *    0  — нужно дозапросить данных (недостаточно байт)
 *   <0  — критическая ошибка (некорректный формат)
 */
int tunnel_open_handle(tunnel_t *tunnel);

/*
 * Обработчик AUTH (USER/PASS):
 *  Читает схему, юзернейм и пароль
 *  Проверяет длины (MAX_UNAME_LEN, MAX_PASSWD_LEN)
 *  Сравнивает с SERVER.username и SERVER.passwd
 *  Шлёт ответ [VER, STATUS]
 * Возвращает:
 *   0   — OK или нужно дождаться следующих байт
 *  <0   — ошибка аутентификации или форматирования
 */
int tunnel_auth_handle(tunnel_t *tunnel);

/*
 * Обработчик REQUEST:
 * - Читает заголовок (ver, cmd, rsv, atyp)
 * - В зависимости от atyp считывает адрес (IPv4/IPv6/домен) и порт
 * - Проверяет поддержку cmd (только CONNECT)
 * - Вызывает функцию установки соединения с удалённым сервером
 * Возвращает:
 *   >0  — инициирован CONNECT, можно перейти в состояние connecting_state
 *    0  — нужно дозапросить ещё байт
 *   <0  — ошибка формата или неподдерживаемая команда
 */
int tunnel_request_handle(tunnel_t *tunnel);

#endif // PROTOCOL_H
