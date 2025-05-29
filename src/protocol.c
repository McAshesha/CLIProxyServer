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
#define MAX_UNAME_LEN 20
// |VER(1)|CMD(1))|RSV(1)|ATYP(1)|DST.ADDR(variable)|DST.PORT(2)|
#define NIPV4 4
#define NIPV6 16


// |VER(1)|NMETHODS(1)|METHODS(1-255)|
int tunnel_open_handle(tunnel_t* tunnel)
{
	buffer_t *buff = tunnel->client_sock->read_buffer;
	open_protocol_t *op = &tunnel->op;
	size_t *nreaded = &tunnel->read_count;
	size_t nheader = sizeof(op->ver) + sizeof(op->nmethods);



	if (*nreaded == 0) goto header;
	else if(*nreaded == nheader) goto methods;
	else assert(0);

header:
	// VER(1)|NMETHODS(1)
	if (buffer_readable(buff) >= nheader) {
		buffer_read(buff, &op->ver, sizeof(op->ver));
		if (op->ver != 0x05) return -1;

		buffer_read(buff, &op->nmethods, sizeof(op->nmethods));
		*nreaded += nheader;
	} else return 0;

methods:
	// METHODS(1-255)
	if (buffer_readable(buff) >= op->nmethods) {
		buffer_read(buff, op->methods, op->nmethods);

		uint8_t reply[2];
		reply[0] = 0x05; // socks5
		int auth = strcmp(SERVER.username, "") != 0 && strcmp(SERVER.passwd, "");
		if (auth) {
			reply[1] = 0x02;
			tunnel->state = auth_state;
		} else {
			reply[1] = 0x00;
			tunnel->state = request_state;
		}
		*nreaded = 0;

		LOG_INFO("SOCKS5 greeting: ver=0x%02x, nmethods=%u → replying method=0x%02x",
			op->ver, op->nmethods, reply[1]);

		return tunnel_write_client(tunnel, reply, sizeof(reply));
	} else return 0;

	return 0;
}

// |VER(1)|ULEN(1)|UNAME(1-255)|PLEN(1)|PASSWD(1-255)|
int tunnel_auth_handle(tunnel_t* tunnel)
{
	buffer_t *buff = tunnel->client_sock->read_buffer;
	auth_protocol_t *ap = &tunnel->ap;
	size_t *nreaded = &tunnel->read_count;
	size_t nheader = sizeof(ap->ver) + sizeof(ap->ulen);
	size_t nplen = sizeof(ap->plen);

	LOG_INFO("Auth attempt: user=\"%.*s\"", ap->ulen, ap->uname);

	if (*nreaded == 0) goto header;
	else if(*nreaded == nheader) goto uname;
	else if(*nreaded == nheader + ap->ulen) goto plen;
	else if (*nreaded == nheader + ap->ulen + nplen) goto passwd;
	else assert(0);

header:
	// VER(1)|ULEN(1)
	if (buffer_readable(buff) >= nheader) {
		buffer_read(buff, &ap->ver, sizeof(ap->ver));
		buffer_read(buff, &ap->ulen, sizeof(ap->ulen));
		if (ap->ulen > MAX_UNAME_LEN) return -1;

		*nreaded += nheader;
	} else return 0;

uname:
	// UNAME(1-255)
	if (buffer_readable(buff) >= ap->ulen) {
		buffer_read(buff, ap->uname, ap->ulen);
		*nreaded += ap->ulen;
	} else return 0;

plen:
	// PLEN(1)
	if (buffer_readable(buff) >= nplen) {
		buffer_read(buff, &ap->plen, nplen);
		if (ap->plen > MAX_PASSWD_LEN) return -1;
		*nreaded += nplen;
	} else return 0;

passwd:
	// PASSWD(1-255)
	if (buffer_readable(buff) >= ap->plen) {
		buffer_read(buff, ap->passwd, ap->plen);
		if (strcmp(ap->uname, SERVER.username) != 0 || strcmp(ap->passwd, SERVER.passwd) != 0) return -1;

		uint8_t reply[2];
		reply[0] = ap->ver; // subversion
		reply[1] = 0x00; // success

		if (tunnel_write_client(tunnel, reply, sizeof(reply)) < 0) return -1;

		tunnel->state = request_state;
		*nreaded  = 0;
	} else return 0;

	return 0;
}

int tunnel_request_handle(tunnel_t *tunnel)
{
	buffer_t *buff = tunnel->client_sock->read_buffer;
	request_protocol_t *rp = &tunnel->rp;
	size_t *nreaded = &tunnel->read_count;
	size_t nheader = sizeof(rp->ver) + sizeof(rp->cmd) + sizeof(rp->rsv) + sizeof(rp->atyp);
	size_t ndomainlen = sizeof(rp->domainlen);
	size_t nport = sizeof(rp->port);

	LOG_INFO("Request: cmd=0x%02x, addr_type=0x%02x, dst=\"%.*s\":%u",
		 rp->cmd, rp->atyp, rp->domainlen, rp->addr, ntohs(rp->port));

	if (*nreaded == 0) goto header;
	else if(*nreaded == nheader) goto addr;
	else if (*nreaded == nheader + ndomainlen) goto domain;
	else assert(0);

header:
	// VER(1)|CMD(1))|RSV(1)|ATYP(1)
	if (buffer_readable(buff) >= nheader) {
		buffer_read(buff, &rp->ver, sizeof(rp->ver));
		if (rp->ver != 0x05) return -1;

		buffer_read(buff, &rp->cmd, sizeof(rp->cmd));
		switch (rp->cmd) {
			case 0x01: // CONNECT
				break;
			case 0x02: // TODO implement BIND
			case 0x03: // TODO implement ASSOCIATE
			default:
				LOG_ERROR("Failed tunnel_request_handle, CMD not support, cmd=%d", rp->cmd);
				return -1;
		}

		buffer_read(buff, &rp->rsv, sizeof(rp->rsv));
		buffer_read(buff, &rp->atyp, sizeof(rp->atyp));
		*nreaded += nheader;
	} else return 0;

addr:
	switch (rp->atyp) {
		case 0x01: // IPV4
			// DST.ADDR(variable)|DST.PORT(2)
			if (buffer_readable(buff) >= NIPV4 + nport) {
				buffer_read(buff, rp->addr, NIPV4);
				buffer_read(buff, &rp->port, nport);
			} else return 0;
			break;
		case 0x04: // IPV6
			// DST.ADDR(variable)|DST.PORT(2)
			if (buffer_readable(buff) >= NIPV6 + nport) {
				buffer_read(buff, rp->addr, NIPV6);
				buffer_read(buff, &rp->port, nport);
			} else return 0;
			break;
		case 0x03: // DOMAIN
			{
				// DST.ADDR[0](1)
				if (buffer_readable(buff) >= ndomainlen) {
					buffer_read(buff, &rp->domainlen, ndomainlen);
					*nreaded += ndomainlen;
				} else return 0;

domain:
				// DST.ADDR[1](DST.ADDR[0])|DST.PORT(2)
				if (buffer_readable(buff) >= rp->domainlen + nport) {
					buffer_read(buff, rp->addr, rp->domainlen);
					buffer_read(buff, &rp->port, nport);
				} else return 0;
			}
			break;
		default:
			return -1;
	}

	*nreaded = 0;
	return tunnel_connect_to_remote(tunnel);
}

