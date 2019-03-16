#ifndef _FTP_H
#define _FTP_H

#include <stdbool.h>
#include <netinet/in.h>
#include <sys/socket.h>

struct ftp_server {
    short cmd_port;
    struct ftp_client **clients;
    int clients_max;
    int socket_cmd;
    bool passive;
    bool isopen;
};

struct ftp_client {
    struct ftp_server *server;
    struct sockaddr_in addr;
    uint8_t  data_addr[4];
    uint16_t data_port;
    int conn_data;
    int conn_cmd;
};

struct ftp_cmd {
    char *cmd;
    void (*fptr)(struct ftp_client *, char *);
};

void ftp_client_print(struct ftp_client *, bool server, bool data);

struct ftp_server *ftp_init();

void ftp_free(struct ftp_server *);

void ftp_close(struct ftp_server *);

void ftp_send(struct ftp_client *, char *msg);

void ftp_data_send(struct ftp_client *, char *buf, int size);

bool ftp_data_open(struct ftp_client *);

void ftp_data_close(struct ftp_client *);

void ftp_cmd_open(struct ftp_server *);

struct ftp_client *ftp_accept(struct ftp_server *);

void exec_to_buffer(char *cmd, char **argv, char *buf, int buf_size);

void handle_cmd_USER(struct ftp_client *, char *arg);

void handle_cmd_PASS(struct ftp_client *, char *arg);

void handle_cmd_SYST(struct ftp_client *, char *arg);

void handle_cmd_QUIT(struct ftp_client *, char *arg);

void handle_cmd_PORT(struct ftp_client *, char *arg);

void handle_cmd_LIST(struct ftp_client *, char *arg);

void handle_cmd_PWD(struct ftp_client *, char *arg);

void handle_cmd_PASV(struct ftp_client *, char *arg);

void handle_cmd_RETR(struct ftp_client *, char *arg);

int handle_command(struct ftp_client *, char *buf);

void ftp_client_handle(struct ftp_client *);

void ftp_client_print(struct ftp_client *, bool server, bool data);

void ftp_client_close(struct ftp_client *);

void ftp_loop(struct ftp_server *);

const static struct ftp_cmd cmd_map[] = {
    { .cmd="USER", .fptr=&handle_cmd_USER },
    { .cmd="PASS", .fptr=&handle_cmd_PASS },
    { .cmd="SYST", .fptr=&handle_cmd_SYST },
    { .cmd="QUIT", .fptr=&handle_cmd_QUIT },
    { .cmd="PORT", .fptr=&handle_cmd_PORT },
    { .cmd="LIST", .fptr=&handle_cmd_LIST },
    { .cmd="PWD",  .fptr=&handle_cmd_PWD  },
    { .cmd="PASV", .fptr=&handle_cmd_PASV },
    { .cmd="RETR", .fptr=&handle_cmd_RETR },
};

#endif
