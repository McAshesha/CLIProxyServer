#ifndef PROTOCOL_PARSER_H
#define PROTOCOL_PARSER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>


// Пытается распознать и залогировать HTTP-пакет, возвращает true если данные – HTTP
bool parse_and_log_http(const uint8_t *data, size_t len, int is_client);

// Пытается распознать и залогировать WebSocket-фрейм, возвращает true если данные – WS
bool parse_and_log_websocket(const uint8_t *data, size_t len, int is_client);


#endif //PROTOCOL_PARSER_H
