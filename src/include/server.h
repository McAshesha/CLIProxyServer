#ifndef SERVER_H
#define SERVER_H


typedef void read_cb(int fd, void *ud);

typedef struct server
{
    int        listenfd;
    read_cb   *read_handle;
    int        epollfd;
    char       username[255];
    char       passwd[255];
} server_t;


extern  server_t SERVER;

void    sigign();

int     server_start();

int     server_init(char *host, char *port, char *username, char *passwd);


#endif //SERVER_H
