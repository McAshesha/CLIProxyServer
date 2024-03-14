#ifndef PROTOCOL_PARSER_H
#define PROTOCOL_PARSER_H


#include <stdbool.h>
#include <stdint.h>


/*
 * Проверяет буфер на HTTP-запрос или ответ.
 * Если находит HTTP, логирует заголовки и старт тела до "\r\n\r\n".
 * Возвращает true, если это HTTP, иначе false.
 */
bool parse_and_log_http(const uint8_t *data, size_t len, int is_client);

/*
 * Проверяет буфер на текстовый WebSocket-фрейм (opcode=1), без маски.
 * Если всё ок — логирует payload.
 * Возвращает true, если фрейм есть и без маски, иначе false.
 */
bool parse_and_log_websocket(const uint8_t *data, size_t len, int is_client);

#endif // PROTOCOL_PARSER_H
