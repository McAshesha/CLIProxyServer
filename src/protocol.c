#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <string.h>
#include <assert.h>

#include "protocol.h"
#include "buffer.h"
#include "logger.h"
#include "server.h"
#include "tunnel.h"
#include "sock.h"


#define MAX_PASSWD_LEN 20
#define MAX_UNAME_LEN  20

#define NIPV4 4
#define NIPV6 16

#define PROTOCOL_VERSION_SOCKS5 0x05
#define SOCKS5_CONNECT          0x01
#define SOCKS5_USER_PASS        0x02
#define SOCKS5_NO_AUTH          0x00


typedef enum protocol_atyp
{
    IPV4   = 0x01,
    IPV6   = 0x04,
    DOMAIN = 0x03
} protocol_atyp_t;


/*
 * Хэндлит этап GREETING (привет от клиента)
 */
int tunnel_open_handle(tunnel_t *tunnel)
{
    buffer_t *buff    = tunnel->client_sock->read_buffer;
    open_protocol_t *op = &tunnel->op;
    size_t *nreaded   = &tunnel->read_count;
    // Сколько байт занимает заголовок (ver + nmethods)
    size_t nheader    = sizeof(op->ver) + sizeof(op->nmethods);

    // Используем goto для разделения этапов чтения: сначала заголовок, потом методы
    if (*nreaded == 0)
    {
        goto header;
    }
    else if (*nreaded == nheader)
    {
        goto methods;
    }
    else
    {
        assert(0);  // Неправильное смещение, логика сломана
    }

header:
    // Если в буфере достаточно байт, считываем ver и nmethods
    if (buffer_readable(buff) >= nheader)
    {
        buffer_read(buff, &op->ver, sizeof(op->ver));
        if (op->ver != PROTOCOL_VERSION_SOCKS5)
        {
            // Проверяем, что это SOCKS5
            return -1;
        }

        buffer_read(buff, &op->nmethods, sizeof(op->nmethods));
        *nreaded += nheader;
    }
    else
        return 0;  // Ждём новых данных

methods:
    // Читаем массив поддерживаемых клиентом методов аутентификации
    if (buffer_readable(buff) >= op->nmethods)
    {
        buffer_read(buff, op->methods, op->nmethods);

        // Формируем ответ [VER=0x05, METHOD]
        uint8_t reply[2];
        reply[0] = PROTOCOL_VERSION_SOCKS5;
        // Метод: если заданы логин/пароль — USER/PASS(0x02), иначе NO AUTH(0x00)
        int auth = strcmp(SERVER.username, "") != 0
                && strcmp(SERVER.passwd, "") != 0;
        reply[1] = auth ? SOCKS5_USER_PASS : SOCKS5_NO_AUTH;
        // Переход в следующее состояние
        tunnel->state = auth ? auth_state : request_state;
        *nreaded = 0;  // Сбрасываем счётчик прочитанных байт

        LOG_INFO("SOCKS5 greeting: ver=0x%02x, nmethods=%u → reply method=0x%02x",
                 op->ver, op->nmethods, reply[1]);

        // Пишем ответ клиенту (в буфер) и возвращаем >0 для запуска записи
        return tunnel_write_client(tunnel, reply, sizeof(reply));
    }
    else
    {
        return 0;  // Ждём методов
    }
}

/*
 * Обработка USER/PASS аутентификацию по RFC1929.
 */
int tunnel_auth_handle(tunnel_t *tunnel)
{
    buffer_t *buff    = tunnel->client_sock->read_buffer;
    auth_protocol_t *ap = &tunnel->ap;
    size_t *nreaded   = &tunnel->read_count;
    // Заголовок: ver + ulen
    size_t nheader    = sizeof(ap->ver) + sizeof(ap->ulen);
    // Размер поля plen
    size_t nplen      = sizeof(ap->plen);

    // Логируем попытку (имя пользователя ещё не заполнено, но ulen возможно было)
    LOG_INFO("Auth attempt: user=\"%.*s\"", ap->ulen, ap->uname);

    // Многоступенчатое чтение: header → username → plen → passwd
    if (*nreaded == 0)
    {
        goto header;
    }
    else if (*nreaded == nheader)
    {
        goto uname;
    }
    else if (*nreaded == nheader + ap->ulen)
     {
        goto plen;
    }
    else if (*nreaded == nheader + ap->ulen + nplen)
    {
        goto passwd;
    }
    else
    {
        assert(0);
    }

header:
    if (buffer_readable(buff) >= nheader)
    {
        buffer_read(buff, &ap->ver, sizeof(ap->ver));
        buffer_read(buff, &ap->ulen, sizeof(ap->ulen));
        if (ap->ulen > MAX_UNAME_LEN)
        {
            // Проверяем максимально допустимую длину
            return -1;
        }
        *nreaded += nheader;
    }
    return 0;

uname:
    if (buffer_readable(buff) >= ap->ulen)
    {
        buffer_read(buff, ap->uname, ap->ulen);
        *nreaded += ap->ulen;
    }
    return 0;

plen:
    if (buffer_readable(buff) >= nplen)
    {
        buffer_read(buff, &ap->plen, nplen);
        if (ap->plen > MAX_PASSWD_LEN)        // Проверяем длину пароля
            return -1;
        *nreaded += nplen;
    }
    return 0;

passwd:
    if (buffer_readable(buff) >= ap->plen)
    {
        buffer_read(buff, ap->passwd, ap->plen);
        // Сравниваем присланные учётные данные с ожидаемыми
        if (strcmp(ap->uname, SERVER.username) != 0
         || strcmp(ap->passwd, SERVER.passwd) != 0)
        {
            return -1;  // Аутентификация не пройдена
        }

        // Формируем положительный ответ [VER, STATUS=0x00]
        uint8_t reply[2] = { ap->ver, 0x00 };
        if (tunnel_write_client(tunnel, reply, sizeof(reply)) < 0)
        {
            return -1;
        }

        tunnel->state = request_state;  // Переходим к запросу CONNECT
        *nreaded = 0;
    }
    return 0;

    return 0;
}

/*
 * Обработка REQUEST (CONNECT и т.п.)
 */
int tunnel_request_handle(tunnel_t *tunnel)
{
    buffer_t *buff     = tunnel->client_sock->read_buffer;
    request_protocol_t *rp = &tunnel->rp;
    size_t *nreaded    = &tunnel->read_count;
    // Заголовок: ver + cmd + rsv + atyp
    size_t nheader     = sizeof(rp->ver) + sizeof(rp->cmd)
                       + sizeof(rp->rsv) + sizeof(rp->atyp);
    size_t ndomainlen  = sizeof(rp->domainlen);
    size_t nport       = sizeof(rp->port);

    // Предварительный лог: мы ещё не считали все поля, но выводим текущее состояние
    LOG_INFO("Request: cmd=0x%02x, addr_type=0x%02x, dst=\"%.*s\":%u",
             rp->cmd, rp->atyp, rp->domainlen, rp->addr, ntohs(rp->port));

    // Аналогично, разбиваем по этапам: header → addr/date → domain → port
    if (*nreaded == 0)
    {
        goto header;
    }
    else if (*nreaded == nheader)
    {
        goto addr;
    }
    else
    {
        assert(0);
    }

header:
    if (buffer_readable(buff) >= nheader)
    {
        buffer_read(buff, &rp->ver, sizeof(rp->ver));
        if (rp->ver != PROTOCOL_VERSION_SOCKS5)
        {
            // Версия SOCKS должна быть 5
            return -1;
        }

        buffer_read(buff, &rp->cmd, sizeof(rp->cmd));
        // Поддерживаем только CONNECT (0x01)
        switch (rp->cmd)
        {
            case SOCKS5_CONNECT: break;    // OK
            default:
                LOG_ERROR("Unsupported CMD in request: %d", rp->cmd);
                return -1;
        }

        buffer_read(buff, &rp->rsv, sizeof(rp->rsv));
        buffer_read(buff, &rp->atyp, sizeof(rp->atyp));
        *nreaded += nheader;
    }
    else return 0;

addr:
    switch (rp->atyp)
    {
        case IPV4:  // IPv4 (4 байта) + порт
            if (buffer_readable(buff) < NIPV4 + nport)
            {
                return 0;
            }
            buffer_read(buff, rp->addr, NIPV4);
            buffer_read(buff, &rp->port, nport);
            break;

        case IPV6:  // IPv6 (16 байт) + порт
            if (buffer_readable(buff) < NIPV6 + nport)
            {
                return 0;
            }
            buffer_read(buff, rp->addr, NIPV6);
            buffer_read(buff, &rp->port, nport);
            break;

        case DOMAIN:  // Доменное имя: сначала длина, потом имя и порт
            if (buffer_readable(buff) < ndomainlen)
            {
                return 0;
            }
            buffer_read(buff, &rp->domainlen, ndomainlen);
            *nreaded += ndomainlen;
            if (buffer_readable(buff) < rp->domainlen + nport)
            {
                return 0;
            }
            buffer_read(buff, rp->addr, rp->domainlen);
            buffer_read(buff, &rp->port, nport);
            break;

        default:
            // Некорректный atyp
            return -1;
    }

    *nreaded = 0;  // Сброс для следующего этапа
    // Пытаемся установить соединение с удалённым хостом
    return tunnel_connect_to_remote(tunnel);
}
