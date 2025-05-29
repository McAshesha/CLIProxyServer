#include <ctype.h>
#include <string.h>

#include "protocol_parser.h"
#include "logger.h"


// Очень упрощённая проверка: HTTP-запрос/ответ всегда начинается с метода или "HTTP/"
bool parse_and_log_http(const uint8_t *data, size_t len, int is_client)
{
    if (len < 4)
    {
        return false;
    }
    // Смотрим первые 4 символа на ASCII-буквы или "HTTP"
    if ((isalpha(data[0]) && isalpha(data[1]) &&
         isalpha(data[2]) && isalpha(data[3]))
        || (memcmp(data, "HTTP", 4) == 0))
    {
        // Логируем до первого "\r\n\r\n"
        const char *s = (const char*)data;
        const char *end = strstr(s, "\r\n\r\n");
        size_t text_len = end ? (end + 4 - s) : len;
	    char *label = (is_client != 0) ? "client → remote" : "remote → client";
        LOG_INFO("HTTP text %s (%zu bytes):\n%.*s",
                 label, text_len, (int)text_len, s);
        return true;
    }
    return false;
}

// Упрощённый парсер WebSocket-фрейма: только текстовые opcode=1 и unmasked payload
bool parse_and_log_websocket(const uint8_t *data, size_t len, int is_client)
{
    if (len < 2)
    {
        return false;
    }
    uint8_t fin_opcode = data[0];
    uint8_t opcode = fin_opcode & 0x0F;
    if (opcode != 0x1)
    {
        return false; // текстовый фрейм
    }
    uint8_t mask_len = (data[1] & 0x80) ? 4 : 0;
    size_t payload_len = data[1] & 0x7F;
    size_t header_len = 2 + mask_len;
    if (len < header_len + payload_len)
    {
        return false;
    }
    const uint8_t *payload = data + header_len;
    // Если есть маска, нужно размаскировать – здесь пропускаем для простоты
    char *label = (is_client != 0) ? "client → remote" : "remote → client";
    LOG_INFO("WebSocket text payload %s (%zu bytes):\n%.*s",
             label, payload_len, (int)payload_len, (const char*)payload);
    return true;
}