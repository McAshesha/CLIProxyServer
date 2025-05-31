#include "tunnel.h"

#include <unistd.h>
#include <sys/epoll.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <signal.h>
#include <assert.h>
#include <arpa/inet.h>

#include "tunnel.h"
#include "sock.h"
#include "logger.h"
#include "protocol_parser.h"
#include "terminal.h"


/**
 * Обработка ошибок EAGAIN и EWOULDBLOCK: если они различны,
 * добавить оба в список возможных ошибок неблокирующего ввода-вывода.
 */
#if (EAGAIN != EWOULDBLOCK)
	#define EAGAIN_EWOULDBLOCK EAGAIN : case EWOULDBLOCK
#else
	#define EAGAIN_EWOULDBLOCK EAGAIN
#endif


typedef enum protocol_atyp
{
	IPV4   = 0x01,
	IPV6   = 0x04,
	DOMAIN = 0x03
} protocol_atyp_t;

typedef struct sockaddr sockaddr_t;

typedef struct sockaddr_in sockaddr_in_t;

typedef struct sockaddr_in6 sockaddr_in6_t;

typedef struct addrinfo addrinfo_t;

/**
 * Создаёт структуру туннеля для вновь принятого клиентского соединения.
 * Переходит в состояние 'open_state' (ожидание Client Greeting).
 * Настраивает клиентский сокет неблокирующим и с keepalive.
 * В случае ошибки освобождает ресурсы и закрывает дескриптор.
 */
tunnel_t* tunnel_create(int fd)
{
	// Переводим клиентский FD в неблокирующий режим
	sock_nonblocking(fd);
	// Включаем TCP keepalive для контроля живости соединения
	sock_keepalive(fd);

	tunnel_t *tunnel = (tunnel_t*)malloc(sizeof(*tunnel));
	if (tunnel == NULL)
	{
		// При нехватке памяти закрываем сокет
		close(fd);
		return NULL;
	}
	// Обнуляем все поля структуры для корректной инициализации
	memset(tunnel, 0, sizeof(*tunnel));

	// Создаём обёртку sock_t для клиентского сокета
	sock_t *client_sock = sock_create(fd, sock_connected, 1, tunnel);
	if (client_sock == NULL)
	{
		// При ошибке освобождаем память и закрываем FD
		free(tunnel);
		close(fd);
		return NULL;
	}

	tunnel->state = open_state;             // стартовое состояние SOCKS5
	tunnel->client_sock = client_sock;      // сохраняем клиентский сокет
	tunnel->read_count = 0;                 // сбрасываем счётчик прочитанных байт
	tunnel->closed = 0;                     // флаг закрытия туннеля

	// Регистрируем клиентский сокет в epoll для чтения
	epoll_add(client_sock);

	return tunnel;
}

/**
 * Полностью освобождает память, занятую структурой туннеля.
 */
void tunnel_release(tunnel_t *tunnel)
{
	free(tunnel);
}

/**
 * Закрывает оба сокета (клиента и удалённого) мягко,
 * позволяя завершить отправку накопленных данных.
 */
static void tunnel_shutdown(tunnel_t *tunnel)
{
	if (tunnel->client_sock != NULL)
	{
		sock_shutdown(tunnel->client_sock);
	}
	if (tunnel->remote_sock != NULL)
	{
		sock_shutdown(tunnel->remote_sock);
	}
}

// Прототипы внутренних обработчиков состояний
static int tunnel_connected_handle(tunnel_t *tunnel, int is_client);

static int tunnel_connecting_handle(tunnel_t *tunnel);


/**
 * Обработчик EPOLLIN: получение данных из сокета.
 * В зависимости от текущего состояния state вызывает соответствующий
 * этап протокола SOCKS5 или 처리 туннеля.
 */
void tunnel_read_handle(int fd, void *ud)
{
	sock_t *sock = (sock_t*)ud;
	tunnel_t *tunnel = sock->tunnel;

	// Считываем доступные данные в buffer_read_buffer
	int n = buffer_readfd(sock->read_buffer, fd);
	if (n < 0)
	{
		// Обрабатываем прерывание или временную недоступность
		switch (errno)
		{
			case EINTR:
			case EAGAIN_EWOULDBLOCK:
				break;
			default:
				goto shutdown; // критическая ошибка
		}

	}
	else if (n == 0)
	{
		// peer выполнил shutdown -> полухлопок
		goto shutdown;
	}

	// В зависимости от состояния туннеля вызываем соответствующий хэндлер
	switch (tunnel->state)
	{
		case open_state:
		{
			if (tunnel_open_handle(tunnel) < 0) goto force_shutdown;
			break;
		}
		case auth_state:
		{
			if (tunnel_auth_handle(tunnel) < 0) goto force_shutdown;
			break;
		}
		case request_state:
		{
			if (tunnel_request_handle(tunnel) < 0) goto force_shutdown;
			break;
		}
		case connecting_state:
		{
			assert(sock->is_client == 0);
			if (tunnel_connecting_handle(tunnel) < 0) goto tunnel_shutdown;
			break;
		}
		case connected_state:
		{
			if (tunnel_connected_handle(tunnel, sock->is_client) < 0) goto tunnel_shutdown;
			break;
		}
		default:
		{
			assert(0); // недопустимое состояние
			break;
		}
	}

	// Логируем факт чтения и текущее состояние
	LOG_INFO("Read %d bytes from %s (fd=%d), state=%d",
		 n, sock->is_client ? "client" : "remote", fd, tunnel->state);

	return;

force_shutdown: // команда peer некорректна, принудительное завершение
	LOG_WARN("Read returned %d on fd=%d – initiating shutdown", n, fd);
	sock_force_shutdown(sock);
	return;

shutdown: // мягкое завершение после EOF или ошибки
	LOG_WARN("Read returned %d on fd=%d – initiating shutdown", n, fd);
	sock_shutdown(sock);
	return;

tunnel_shutdown: // завершение всего туннеля при ошибке
	LOG_WARN("Read returned %d on fd=%d – initiating shutdown", n, fd);
	tunnel_shutdown(tunnel);
}

/**
 * Обработчик EPOLLOUT: попытка записи накопленных данных из буфера.
 * При отсутствии данных и закрытом состоянии сокета инициирует shutdown.
 * При соединении проверяет статус неблокирующего connect.
 */
void tunnel_write_handle(int fd, void *ud)
{
	sock_t *sock = (sock_t *)ud;
	tunnel_t *tunnel = sock->tunnel;

	// Если есть данные для записи — пытаемся отправить
	if (buffer_readable(sock->write_buffer) > 0)
	{
		int n = buffer_writefd(sock->write_buffer, fd);
		if (n <= 0)
		{
			switch (errno)
			{
				case EINTR:
				case EAGAIN_EWOULDBLOCK:
					break;
				default:
					goto force_shutdown;
			}
		}
		LOG_INFO("Wrote %d bytes to %s (fd=%d)", n, sock->is_client ? "client" : "remote", fd);

	}
	else if (sock->state == sock_halfclosed)
	{
		// Если удалённый сокет закрыл запись — принудительно завершаем
		goto force_shutdown;
	}

	// В состоянии подключения проверяем завершение неблокирующего connect
	if (tunnel->state == connecting_state)
	{
		assert(sock->is_client == 0);

		if (tunnel_connecting_handle(tunnel) < 0)
		{
			goto tunnel_shutdown;
		}
	}

	// Обновляем события epoll: интерес к записи, если остались данные
	int writable = buffer_readable(sock->write_buffer) > 0;
	epoll_modify(sock, writable, 1);

	return;

// Закрываем весь туннель при серьёзной ошибке записи
tunnel_shutdown:
	tunnel_shutdown(tunnel);
	LOG_ERROR("Write error on fd=%d: %s", fd, strerror(errno));
	return;

force_shutdown: // принудительное завершение только данного сокета
	sock_force_shutdown(sock);
	LOG_ERROR("Write error on fd=%d: %s", fd, strerror(errno));
	return;
}

/**
 * Вспомогательный вывод данных в шестнадцатеричном виде.
 * Ограничиваем логирование первыми 128 байтами для читаемости.
 */
static void dump_hex(const char *label, const uint8_t *buf, size_t len)
{
	size_t max = len < 128 ? len : 128;
	char hexstr[3 * 128 + 1] = {0};
	char *p = hexstr;
	for (size_t i = 0; i < max; i++)
	{
		// Формируем текст вида "ab cd ef ..."
		p += sprintf(p, "%02x ", buf[i]);
	}
	LOG_INFO("%s hex (%zu bytes): %s% s", label, len, hexstr,
			 (len > max ? "...(truncated)" : ""));
}

/**
 * Обработка уже установленного туннеля: анализ и форвардинг данных.
 * Если не удалось распарсить HTTP/WebSocket — делаем hex-dump.
 * При активном флаге freeze пропускаем форвардинг.
 */
static int tunnel_connected_handle(tunnel_t *tunnel, int is_client)
{
	// Определяем направления read/write: front - куда писать, rear - откуда читать
	sock_t *sock_front = is_client ? tunnel->remote_sock : tunnel->client_sock;
	sock_t *sock_rear = is_client ? tunnel->client_sock : tunnel->remote_sock;

	char *label = is_client ? "Forwarded client → remote" : "Forwarded remote → client";

	if (sock_front == NULL)
	{
		// Отсутствие противоположного сокета — фатальная ошибка
		return -1;
	}

	buffer_t *rear_buffer = sock_rear->read_buffer;
	uint8_t *buffer_data = (uint8_t *) rear_buffer->data + rear_buffer->read_index;
	size_t length = buffer_readable(rear_buffer);

	// Пытаемся логировать HTTP или WebSocket
	if (!parse_and_log_http(buffer_data, length, is_client) &&
		!parse_and_log_websocket(buffer_data, length, is_client))
	{
		// Если оба разбора не сработали — выводим hex
		dump_hex(label, buffer_data, length);
	}

	// Если пользователь заморозил туннель — не пересылаем данные дальше
	if (terminal_is_frozen())
	{
		return 0;
	}

	// Переносим данные из read_buffer в write_buffer противопололожного сокета
	if (buffer_concat(sock_front->write_buffer, rear_buffer) < 0)
	{
		return -1;
	}
    buffer_clear(rear_buffer);  // очищаем буфер после успешной конкатенации

	// Обновляем epoll: включаем интерес к записи на front-сокете
	epoll_modify(sock_front, 1, 1);

	return 0;
}

/**
 * Отправляет клиенту ответ о успешном подключении SOCKS5.
 * В зависимости от семейства адреса (IPv4/IPv6) формирует тело ответа.
 */
static int tunnel_notify_connected(tunnel_t *tunnel)
{
	sockaddr_t sa;
	socklen_t len = sizeof(sa);
	uint8_t header[4];

	header[0] = 0x05;  // версия SOCKS5
	header[1] = 0x00;  // статус: успех
	header[2] = 0x00;  // зарезервировано

	// Извлекаем локальный адрес удалённого сокета
	if (getsockname(tunnel->remote_sock->fd, &sa, &len) < 0)
	{
		return -1;
	}

	if (sa.sa_family == AF_INET)
	{
        header[3] = IPV4; // тип адреса: IPv4
		if (tunnel_write_client(tunnel, header, sizeof(header)) < 0)
		{
			return -1;
		}

		sockaddr_in_t *sa_in = (sockaddr_in_t*)&sa;
		if (tunnel_write_client(tunnel, &sa_in->sin_addr, sizeof(sa_in->sin_addr)) < 0)
		{
			return -1;
		}
		if (tunnel_write_client(tunnel, &sa_in->sin_port, sizeof(sa_in->sin_port)) < 0)
		{
			return -1;
		}
	}
	else if (sa.sa_family == AF_INET6)
	{
        header[3] = IPV6; // тип адреса: IPv6
		tunnel_write_client(tunnel, header, sizeof(header));

		sockaddr_in6_t *sa_in6 = (sockaddr_in6_t*)&sa;
		tunnel_write_client(tunnel, &sa_in6->sin6_addr, sizeof(sa_in6->sin6_addr));
		tunnel_write_client(tunnel, &sa_in6->sin6_port, sizeof(sa_in6->sin6_port));
	}
	else
	{
		// Неподдерживаемое семейство адресов
		LOG_ERROR("Failed tunnel_notify_connected, unexpected family=%d", sa.sa_family);
		return -1;
	}

	LOG_INFO("Sent SOCKS5 CONNECT success to client fd=%d", tunnel->client_sock->fd);

	return 0;
}

/**
 * Записывает произвольный блок данных в буфер клиента и активирует
 * epoll-событие на запись. Используется для формирования ответов.
 */
int tunnel_write_client(tunnel_t *tunnel, void *src, size_t size)
{
	if (tunnel->client_sock == NULL)
	{
		return -1;
	}

	if (buffer_write(tunnel->client_sock->write_buffer, src, size) < 0)
	{
		return -1;
	}

	epoll_modify(tunnel->client_sock, 1, 1);
	return 0;
}

/**
 * Обрабатывает завершение неблокирующего connect(): проверка через getsockopt.
 * Если соединение установлено — переходим в connected_state и уведомляем клиент.
 */
static int tunnel_connecting_handle(tunnel_t *tunnel)
{
	int error;
	socklen_t len = sizeof(error);
	int code = getsockopt(tunnel->remote_sock->fd, SOL_SOCKET, SO_ERROR, &error, &len);
	// Разные реализации возвращают ошибки по-разному
	if (code < 0 || error)
	{
		if (error)
		{
			errno = error;
		}
		return -1;
	}

	LOG_INFO("Remote connection established on fd=%d", tunnel->remote_sock->fd);

	tunnel->state = connected_state;
	tunnel->remote_sock->state = sock_connected;
	return tunnel_notify_connected(tunnel);
}

/**
 * Инициирует подключение к удалённому хосту по параметрам из request_protocol_t.
 * Выполняет DNS-разрешение, создаёт неблокирующий сокет и запускает connect().
 */
int tunnel_connect_to_remote(tunnel_t *tunnel)
{
	uint8_t atyp = tunnel->rp.atyp;
	char *addr;
	char ip[64];
	char port[16];

	// Преобразуем порт в строковый формат
	snprintf(port, sizeof(port),"%d", ntohs(tunnel->rp.port));
	switch(atyp)
	{
        case IPV4: // IPv4
        {
			inet_ntop(AF_INET, tunnel->rp.addr, ip, sizeof(ip));
			addr = ip;
			break;
		}
        case IPV6: // IPv6
		{
			inet_ntop(AF_INET6, tunnel->rp.addr, ip, sizeof(ip));
			addr = ip;
			break;
		}
        case DOMAIN: // доменное имя
		{
			addr = tunnel->rp.addr;
			break;
		}
		default:
		{
            assert(0); // неожиданное значение atyp
			break;
		}
	}

	LOG_INFO("Resolving %s:%s", addr, port);

	// Настраиваем параметры getaddrinfo
	addrinfo_t ai_hint;
	memset(&ai_hint, 0, sizeof(ai_hint));

	ai_hint.ai_family = AF_UNSPEC;
	ai_hint.ai_socktype = SOCK_STREAM;
	ai_hint.ai_protocol = IPPROTO_TCP;

	addrinfo_t *ai_list;
	addrinfo_t *ai_ptr;

    // Перебираем все возможные адреса до первого успешного connect()
	if (getaddrinfo(addr, port, &ai_hint, &ai_list) != 0)
	{
		LOG_ERROR("Failed getaddrinfo, addr=%s,port=%s, error=%s", addr, port, gai_strerror(errno));
		return -1;
	}

	int newfd = -1;
	int status;
	for (ai_ptr = ai_list; ai_ptr != NULL; ai_ptr = ai_ptr->ai_next)
	{
		newfd = socket(ai_ptr->ai_family, ai_ptr->ai_socktype, ai_ptr->ai_protocol);
		if (newfd < 0)
		{
			continue;
		}
		sock_nonblocking(newfd);
		sock_keepalive(newfd);

		status = connect(newfd, ai_ptr->ai_addr, ai_ptr->ai_addrlen);

		LOG_INFO("Connecting to remote %s:%s → fd=%d (status=%s)", addr, port, newfd,
			(status == 0 ? "immediate" : "in progress"));

		if (status != 0 && errno != EINPROGRESS)
		{
			// Ошибка немедленного подключения
			close(newfd);
			newfd = -1;
			LOG_ERROR("Connect failed to %s:%s: %s", addr, port, gai_strerror(errno));
			continue;
		}

		break;
	}
	freeaddrinfo(ai_list);

	if (newfd < 0)
	{
		return -1;
	}

	// Создаём обёртку sock_t для удалённого сокета
	sock_t *sock = sock_create(newfd, sock_connecting, 0, tunnel);
	if (sock == NULL)
	{
		close(newfd);
		return -1;
	}
	tunnel->remote_sock = sock;

	epoll_add(sock);
	epoll_modify(sock, 1, 1);

	if (status == 0)
	{
		// Соединение завершилось мгновенно
		tunnel->state = connected_state;
		sock->state = sock_connected;
		return tunnel_notify_connected(tunnel);
	}
	else
	{
		// Ожидаем завершения неблокирующего connect
		tunnel->state = connecting_state;
		sock->state = sock_connecting;
	}

	return 0;
}
