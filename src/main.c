#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <poll.h>
#include "include/socks5.h"

#define LISTEN_PORT 1080
#define BACKLOG     128

// Hex-dump helper
static void hexdump(const char *prefix, const unsigned char *data, size_t len) {
    printf("%s [hex, %zu bytes]: ", prefix, len);
    for (size_t i = 0; i < len; ++i) printf("%02x ", data[i]);
    printf("\n");
}

// Relay TCP bidirectionally with logging
static void relay_data(int fd1, int fd2) {
    struct pollfd fds[2] = {{fd1, POLLIN, 0}, {fd2, POLLIN, 0}};
    unsigned char buf[4096];
    printf("[INFO] Starting TCP relay loop\n");
    while (1) {
        if (poll(fds, 2, -1) < 0) { perror("poll"); break; }
        if (fds[0].revents & POLLIN) {
            ssize_t n = read(fd1, buf, sizeof(buf));
            if (n <= 0) { printf("[INFO] Client TCP closed connection\n"); break; }
            hexdump("TCP from client", buf, n);
            if (write(fd2, buf, n) != n) { printf("[ERROR] Failed write to remote\n"); break; }
        }
        if (fds[1].revents & POLLIN) {
            ssize_t n = read(fd2, buf, sizeof(buf));
            if (n <= 0) { printf("[INFO] Remote TCP closed connection\n"); break; }
            hexdump("TCP from remote", buf, n);
            if (write(fd1, buf, n) != n) { printf("[ERROR] Failed write to client\n"); break; }
        }
    }
    printf("[INFO] Exiting TCP relay loop\n");
}

void *handle_client(void *arg) {
    int client_fd = *(int*)arg;
    free(arg);

    struct sockaddr_in cli_addr;
    socklen_t cli_len = sizeof(cli_addr);
    if (getpeername(client_fd, (struct sockaddr*)&cli_addr, &cli_len) == 0) {
        printf("[INFO] Client connected: %s:%d\n", inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));
    }

    int cmd;
    struct sockaddr_in target;
    if (socks5_handshake(client_fd, &cmd, &target) < 0) {
        printf("[ERROR] SOCKS5 handshake failed, closing client\n");
        close(client_fd);
        return NULL;
    }
    printf("[INFO] SOCKS5 command %s to %s:%d\n",
           cmd==SOCKS5_CMD_CONNECT?"CONNECT":"UDP_ASSOCIATE",
           inet_ntoa(target.sin_addr), ntohs(target.sin_port));

    if (cmd == SOCKS5_CMD_CONNECT) {
        // TCP relay
        int remote_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (remote_fd < 0) {
            perror("socket(remote)");
            close(client_fd);
            return NULL;
        }
        if (connect(remote_fd, (struct sockaddr*)&target, sizeof(target)) < 0) {
            perror("connect(remote)");
            close(remote_fd);
            close(client_fd);
            return NULL;
        }
        printf("[INFO] TCP relay established\n");
        relay_data(client_fd, remote_fd);
        close(remote_fd);
    }
    else if (cmd == SOCKS5_CMD_UDP_ASSOCIATE) {
        // UDP relay
        int udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (udp_fd < 0) { perror("socket(udp)"); close(client_fd); return NULL; }
        struct sockaddr_in udp_bind = {0};
        udp_bind.sin_family = AF_INET;
        udp_bind.sin_addr.s_addr = INADDR_ANY;
        udp_bind.sin_port = 0;
        if (bind(udp_fd, (struct sockaddr*)&udp_bind, sizeof(udp_bind)) < 0) {
            perror("bind(udp)"); close(udp_fd); close(client_fd); return NULL; }
        socklen_t len = sizeof(udp_bind);
        if (getsockname(udp_fd, (struct sockaddr*)&udp_bind, &len) < 0) {
            perror("getsockname"); close(udp_fd); close(client_fd); return NULL; }
        unsigned char rep[10];
        rep[0] = SOCKS5_VERSION; rep[1] = 0x00; rep[2] = 0x00; rep[3] = SOCKS5_ATYP_IPV4;
        memcpy(rep+4, &udp_bind.sin_addr.s_addr, 4);
        memcpy(rep+8, &udp_bind.sin_port, 2);
        if (write(client_fd, rep, sizeof(rep)) != sizeof(rep)) {
            perror("send udp reply"); close(udp_fd); close(client_fd); return NULL; }
        printf("[INFO] UDP proxy bound on %s:%d\n", inet_ntoa(udp_bind.sin_addr), ntohs(udp_bind.sin_port));

        struct sockaddr_in peer, client_addr;
        socklen_t peer_len = sizeof(peer), client_len = sizeof(client_addr);
        unsigned char buf[65536];
        int have_client = 0;
        printf("[INFO] Starting UDP relay loop\n");
        while (1) {
            ssize_t n = recvfrom(udp_fd, buf, sizeof(buf), 0, (struct sockaddr*)&peer, &peer_len);
            if (n < 0) { perror("recvfrom"); break; }
            if (!have_client) {
                client_addr = peer; client_len = peer_len; have_client = 1;
                printf("[INFO] Registered UDP client %s:%d\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
            }
            if (peer.sin_addr.s_addr == client_addr.sin_addr.s_addr && peer.sin_port == client_addr.sin_port) {
                if ((size_t)n < 10) continue;
                hexdump("UDP from client", buf, n);
                unsigned char atyp = buf[3]; int off = 4;
                struct sockaddr_in dest = {0}; dest.sin_family = AF_INET;
                if (atyp == SOCKS5_ATYP_IPV4) { memcpy(&dest.sin_addr.s_addr, buf+off, 4); off += 4; }
                memcpy(&dest.sin_port, buf+off, 2); off += 2;
                size_t payload_len = n - off;
                if (sendto(udp_fd, buf+off, payload_len, 0, (struct sockaddr*)&dest, sizeof(dest)) < 0)
                    perror("sendto(dest)");
            } else {
                hexdump("UDP from remote", buf, n);
                unsigned char sendbuf[65536]; size_t off = 0;
                sendbuf[off++]=0;sendbuf[off++]=0;sendbuf[off++]=0;sendbuf[off++]=SOCKS5_ATYP_IPV4;
                memcpy(sendbuf+off,&peer.sin_addr.s_addr,4); off+=4;
                memcpy(sendbuf+off,&peer.sin_port,2); off+=2;
                memcpy(sendbuf+off,buf,n); off+=n;
                if (sendto(udp_fd, sendbuf, off, 0, (struct sockaddr*)&client_addr, client_len)<0)
                    perror("sendto(client)");
            }
        }
        printf("[INFO] Exiting UDP relay loop\n");
        close(udp_fd);
    }
    close(client_fd);
    printf("[INFO] Client handler thread exiting\n");
    return NULL;
}

int main() {
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); return EXIT_FAILURE; }
    struct sockaddr_in srv = {0}; srv.sin_family=AF_INET; srv.sin_addr.s_addr=INADDR_ANY; srv.sin_port=htons(LISTEN_PORT);
    if (bind(listen_fd,(struct sockaddr*)&srv,sizeof(srv))<0){ perror("bind"); return EXIT_FAILURE; }
    if (listen(listen_fd,BACKLOG)<0){ perror("listen"); return EXIT_FAILURE; }
    printf("[INFO] SOCKS5 proxy listening on port %d...\n", LISTEN_PORT);
    while (1) {
        struct sockaddr_in cli; socklen_t cli_len=sizeof(cli);
        int *client_fd=malloc(sizeof(int)); *client_fd=accept(listen_fd,(struct sockaddr*)&cli,&cli_len);
        if (*client_fd<0){ perror("accept"); free(client_fd); continue; }
        pthread_t tid;
        pthread_create(&tid,NULL,handle_client,client_fd);
        pthread_detach(tid);
    }
    close(listen_fd);
    return EXIT_SUCCESS;
}
