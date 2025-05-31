#include <ctype.h>
#include <string.h>
#include "protocol_parser.h"
#include "logger.h"

/*
 *  Чекает, начинается ли буфер с HTTP-метода (четыре буквы) или префикса "HTTP".
 *  Если данных достаточно, находит границу заголовков "\r\n\r\n" и логирует
 *  весь блок от начала до нее.
 */
bool parse_and_log_http(const uint8_t *data, size_t len, int is_client)
{
    // На любое корректное HTTP-сообщение уходит минимум 4 байта
    if (len < 4)
    {
        return false;
    }

    // Проверяем первые 4 байта на буквы или на строку "HTTP"
    bool starts_with_letters = (isalpha(data[0]) && isalpha(data[1]) &&
                                isalpha(data[2]) && isalpha(data[3]));
    bool starts_with_http    = (memcmp(data, "HTTP", 4) == 0);
    if (starts_with_letters || starts_with_http) {

        // Приводим указатель к строковому виду для поиска разделителя
        const char *text   = (const char *)data;
        const char *split  = strstr(text, "\r\n\r\n");
        // Если разделитель найден, логируем до его конца (split+4),
        // иначе логируем весь буфер целиком.
        size_t      to_log  = split ? (split + 4 - text) : len;
        // Определяем метку направления передачи
        const char *label   = is_client
                             ? "client → remote"
                             : "remote → client";

        // Вывод в лог первого блока HTTP-сообщения
        LOG_INFO("HTTP %s, %zu байт:\n%.*s",
                 label, to_log, (int)to_log, text);
        return true;
    }

    // Если ни условие не сработало — это не HTTP
    return false;
}

/*
 * parse_and_log_websocket
 * Распознаёт текстовый WebSocket-фрейм без маски (opcode = 1).
 * Не робит фрагментацию и маскирование. При успешном разборе
 * логирует содержимое payload.
 */
bool parse_and_log_websocket(const uint8_t *data, size_t len, int is_client)
{
    // Минимальный размер заголовка WebSocket — 2 байта
    if (len < 2) {
        return false;
    }

    // Первый байт: FIN (старший бит) и opcode (младшие 4 бита)
    uint8_t fin_and_opcode = data[0];
    uint8_t opcode         = fin_and_opcode & 0x0F;
    // Поддерживаем только текстовые фреймы (opcode = 1)
    if (opcode != 0x1) {
        return false;
    }

    // Второй байт: старший бит — флаг маски, младшие 7 бит — длина payload (если <= 125)
    bool    has_mask      = (data[1] & 0x80) != 0;
    uint8_t raw_length    = data[1] & 0x7F;
    size_t  header_length = 2 + (has_mask ? 4 : 0);

    // Проверяем, что в буфере есть полный фрейм (заголовок + payload)
    if (len < header_length + raw_length) {
        return false;
    }

    // Сдвигаем указатель на начало payload
    const uint8_t *payload = data + header_length;
    // Направление передачи для логирования
    const char   *label    = is_client
                             ? "client → remote"
                             : "remote → client";

    // Логируем полезную нагрузку как текстовую строку
    LOG_INFO("WebSocket %s, %u байт:\n%.*s",
             label, raw_length, raw_length, (const char *)payload);
    return true;
}
