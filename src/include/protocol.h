#ifndef PROTOCOL_H
#define PROTOCOL_H


#include <stdint.h>


typedef struct tunnel tunnel_t;

typedef struct open_protocol
{
    uint8_t ver;
    uint8_t nmethods;
    uint8_t methods[255];
} open_protocol_t;

typedef struct auth_protocol
{
    uint8_t ver;
    uint8_t ulen;
    char uname[255];
    uint8_t plen;
    char passwd[255];
} auth_protocol_t;

typedef struct request_protocol
{
    uint8_t ver;
    uint8_t cmd;
    uint8_t rsv;
    uint8_t atyp;
    uint8_t domainlen;
    char addr[255];
    uint16_t port;
} request_protocol_t;


int     tunnel_open_handle(tunnel_t *tunnel);

int     tunnel_auth_handle(tunnel_t *tunnel);

int     tunnel_request_handle(tunnel_t *tunnel);


#endif // PROTOCOL_H
