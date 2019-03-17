#define _GNU_SOURCE
#include <errno.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>

#include "ftp.h"

struct ftp_server *ftp_init() {
    struct ftp_server *ftp = malloc(sizeof(struct ftp_server));
    ftp->clients_max = 8;
    ftp->clients = malloc(ftp->clients_max * sizeof(struct ftp_client));
    ftp->isopen = true;
    ftp->passive = false;
    ftp->data_addr[0] = 127;
    ftp->data_addr[1] = 0;
    ftp->data_addr[2] = 0;
    ftp->data_addr[3] = 1;

    ftp->pwd = malloc(PATH_MAX);
    getcwd(ftp->pwd, PATH_MAX);

    for(int i = 0; i < ftp->clients_max; i++) {
        ftp->clients[i] = NULL;
    }
    return ftp;
}

void ftp_free(struct ftp_server *ftp) {
    close(ftp->socket_cmd);
    for(int i = 0; i < ftp->clients_max; i++) {
        if(ftp->clients[i] != NULL) {
            free(ftp->clients[i]);
        }
    }

    free(ftp->clients);
    free(ftp);
}

void ftp_close(struct ftp_server *ftp) {
    ftp->isopen = false;
}

void ftp_send(struct ftp_client *c, char *msg) {
    char buf[128];
    strcpy(buf, msg);
    strcat(buf, "\r\n");

    if(c->conn_cmd < 3) {
        printf("ERROR: Bad file descriptor!\n");
        exit(1);
    }

    write(c->conn_cmd, buf, strlen(buf));
    ftp_client_print(c, true, false);
    printf("%s\n", msg);
}

void ftp_data_send(struct ftp_client *c, char *buf, int size) {
    if(c->conn_data == -1) {
        printf("ERROR: Not connected to data socket.\n");
        return;
    }

    if(c->conn_data < 3) {
        printf("ERROR: Bad file descriptor!\n");
        exit(1);
    }

    write(c->conn_data, buf, size);
    ftp_client_print(c, true, true);
    printf("Sent %d bytes.\n", size);
}

char *ftp_data_read(struct ftp_client *c, size_t *size) {
    int total_size = 0;
    int data_size = 0;
    int buf_size = 128;
    char *buf = malloc(buf_size);
    int to_read = buf_size;

    do {
        if(data_size == to_read) {
            to_read = buf_size;
            buf_size *= 2;
            buf = realloc(buf, buf_size);
        }

        data_size = read(c->conn_data, buf + total_size, to_read);

        if(data_size == -1) {
            if(total_size == 0) {
                continue;
            }

            total_size += data_size;
            break;
        }

        total_size += data_size;
    }
    while(data_size != 0);

    ftp_client_print(c, false, true);
    printf("Received %d bytes.\n", total_size);
    *size = total_size;
    return buf;
}

bool ftp_passive_open(struct ftp_server *ftp) {
    if(ftp->passive) {
        return false;
    }

    struct sockaddr_in servaddr;
    ftp->socket_passive = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);

    if(ftp->socket_passive == -1) {
        printf("=== Unable to create passive socket\n");
        return false;
    }

    int one = 1;
    setsockopt(ftp->socket_passive, SOL_SOCKET, SO_REUSEADDR, &one,
            sizeof(one));

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(ftp->data_port);

    if(bind(ftp->socket_passive, (struct sockaddr *) &servaddr,
             sizeof(servaddr)) != 0) {
        printf("=== Unable to bind passive port %hu.\n", ftp->data_port);
        return false;
    }

    if(listen(ftp->socket_passive, 5) != 0) {
        printf("=== Passive listen failed.\n");
        return false;
    }

    printf("=== Opened new passive socket on port %hu\n", ftp->data_port);
    ftp->passive = true;
    return true;
}

bool ftp_data_open(struct ftp_client *c) {
    if(c->server->passive && c->passive) {
        return true;
    }

    struct sockaddr_in servaddr;
    c->conn_data = socket(AF_INET, SOCK_STREAM, 0);

    if(c->conn_data == -1) {
        printf("=== Unable to create DATA socket on %d.%d.%d.%d:%hu\n",
               c->data_addr[0], c->data_addr[1], c->data_addr[2],
               c->data_addr[3], c->data_port);
        ftp_client_close(c);
        return false;
    }

    bzero(&servaddr, sizeof(servaddr));

    char addr_str[INET_ADDRSTRLEN];
    sprintf(addr_str, "%u.%u.%u.%u", c->data_addr[0], c->data_addr[1],
            c->data_addr[2], c->data_addr[3]);
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = inet_addr(addr_str);
    servaddr.sin_port = htons(c->data_port);

    char str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(c->addr.sin_addr), str, INET_ADDRSTRLEN);

    if(connect(c->conn_data, (struct sockaddr *) &servaddr,
               sizeof(servaddr)) != 0) {
        printf("=== Unable to connect to DATA port on %d.%d.%d.%d:%hu\n",
               c->data_addr[0], c->data_addr[1], c->data_addr[2],
               c->data_addr[3], c->data_port);
        ftp_client_close(c);
        return false;
    }

    int flags = fcntl(c->conn_data, F_GETFL, 0);
    fcntl(c->conn_data, F_SETFL, flags | O_NONBLOCK);

    printf("=== Opened new DATA connection with %d.%d.%d.%d:%hu\n",
           c->data_addr[0], c->data_addr[1], c->data_addr[2],
           c->data_addr[3], c->data_port);
    return true;
}

void ftp_data_close(struct ftp_client *c) {
    close(c->conn_data);
    c->conn_data = -1;
}

void ftp_cmd_open(struct ftp_server *ftp) {
    struct sockaddr_in servaddr;

    ftp->socket_cmd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    int one = 1;
    setsockopt(ftp->socket_cmd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    if(ftp->socket_cmd == -1) {
        printf("socket creation failed...\n");
        exit(0);
    }
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(ftp->cmd_port);

    if((bind(ftp->socket_cmd, (struct sockaddr *) &servaddr,
             sizeof(servaddr))) != 0) {
        printf("=== Unable to bind port %hu.\n", ftp->cmd_port);
        exit(0);
    }

    if(listen(ftp->socket_cmd, 5) != 0) {
        printf("=== Listen failed.\n");
        exit(0);
    }

    printf("=== Opened CMD socket on port %hu.\n", ftp->cmd_port);
}

void ftp_accept_passive(struct ftp_client *client) {
    unsigned int len = sizeof(struct sockaddr_in);
    int conn;
    struct sockaddr_in addr;
    /* conn_cmd = accept(ftp->socket_cmd, (struct sockaddr *) &addr, &len); */
    conn = accept4(client->server->socket_passive, (struct sockaddr *) &addr,
            &len, SOCK_NONBLOCK);

    if(errno == EAGAIN) {
        errno = 0;
        return;
    }

    if(conn < 0) {
        printf("Server acccept failed...\n");
        return;
    }

    char addr_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client->addr.sin_addr), addr_str, INET_ADDRSTRLEN);

    printf("=== New client connected to passive DATA from %s:%hu\n", addr_str,
           client->addr.sin_port);
    client->conn_data = conn;
}

struct ftp_client *ftp_accept(struct ftp_server *ftp) {
    unsigned int len = sizeof(struct sockaddr_in);
    int conn_cmd;
    struct sockaddr_in addr;
    /* conn_cmd = accept(ftp->socket_cmd, (struct sockaddr *) &addr, &len); */
    conn_cmd = accept4(ftp->socket_cmd, (struct sockaddr *) &addr, &len,
                       SOCK_NONBLOCK);

    if(errno == EAGAIN) {
        errno = 0;
        return NULL;
    }

    if(conn_cmd < 0) {
        printf("Server acccept failed...\n");
        return NULL;
    }

    struct ftp_client *client = malloc(sizeof(struct ftp_client));
    client->conn_cmd = conn_cmd;
    client->addr = addr;
    client->server = ftp;
    client->cwd = malloc(PATH_MAX);
    strcpy(client->cwd, "/");

    // Add to clients array
    struct ftp_client **clients_element = NULL;
    for(int i = 0; i < ftp->clients_max; i++) {
        if(ftp->clients[i] != NULL) {
            continue;
        }

        clients_element = &ftp->clients[i];
    }

    char addr_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client->addr.sin_addr), addr_str, INET_ADDRSTRLEN);

    if(clients_element == NULL) {
        printf("=== Can't accept %s:%hu, too many connections\n", addr_str,
               client->addr.sin_port);
        close(conn_cmd);
        return NULL;
    }

    *clients_element = client;

    printf("=== New client connected from %s:%hu\n", addr_str,
           client->addr.sin_port);

    ftp_send(client, "210 Ready to accept user.");
    return client;
}

void exec_to_buffer(char *cmd, char **argv, char *buf, int buf_size) {
    bzero(buf, buf_size);
    int pipefd[2];
    pipe(pipefd);

    if(fork() == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], 1);
        dup2(pipefd[1], 2);
        close(pipefd[1]);
        execv(cmd, argv);
    }
    else {
        close(pipefd[1]);
        read(pipefd[0], buf, buf_size);
    }
}

void handle_cmd_USER(struct ftp_client *c, char *arg) {
    printf("=== Attemping to log in with user: %s\n", arg);
    ftp_send(c, "331 User name okay, need password.");
}

void handle_cmd_PASS(struct ftp_client *c, char *arg) {
    ftp_send(c, "230 User logged in, proceed");
}

void handle_cmd_SYST(struct ftp_client *c, char *arg) {
    ftp_send(c, "215 UNIX");
}

void handle_cmd_QUIT(struct ftp_client *c, char *arg) {
    ftp_send(c, "221 Bye!");
}

void handle_cmd_PORT(struct ftp_client *c, char *arg) {
    c->data_addr[0] = atoi(strtok(arg, ","));
    c->data_addr[1] = atoi(strtok(NULL, ","));
    c->data_addr[2] = atoi(strtok(NULL, ","));
    c->data_addr[3] = atoi(strtok(NULL, ","));
    c->data_port = atoi(strtok(NULL, ",")) << 8;
    c->data_port |= atoi(strtok(NULL, ","));
    ftp_send(c, "200 Command okay.");
}

void handle_cmd_LIST(struct ftp_client *c, char *arg) {
    char buf[1024] = { 0 };
    char path[PATH_MAX];
    strcpy(path, c->server->pwd);
    strcat(path, c->cwd);
    char *argv[] = { "ls", "-al", path, NULL };
    exec_to_buffer("/usr/bin/ls", argv, buf, 1024);
    ftp_send(c, "125 Sending directory list.");
    ftp_data_open(c);
    ftp_data_send(c, buf, strlen(buf));
    ftp_data_close(c);
    ftp_send(c, "226 Finished sending directory list.");
}

void handle_cmd_PWD(struct ftp_client *c, char *arg) {
    char msg[PATH_MAX] = "257 \"";
    strcat(msg, c->cwd);
    strcat(msg, "\"");
    ftp_send(c, msg);
}

void handle_cmd_PASV(struct ftp_client *c, char *arg) {
    ftp_passive_open(c->server);
    char addr_str[27 + INET_ADDRSTRLEN + 9];
    c->passive = true;
    sprintf(addr_str, "227 Entering Passive mode. %u,%u,%u,%u,%hu,%hu",
            c->server->data_addr[0],
            c->server->data_addr[1],
            c->server->data_addr[2],
            c->server->data_addr[3],
            c->server->data_port >> 8,
            c->server->data_port & 0xFF);

    ftp_send(c, addr_str);
}

void handle_cmd_RETR(struct ftp_client *c, char *arg) {
    char path[PATH_MAX];
    strcpy(path, c->server->pwd);
    strcat(path, arg);

    int fd = open(path, O_RDONLY);
    if(fd == -1) {
        return;
    }

    size_t size = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    char *buf = malloc(size);
    read(fd, buf, size);
    ftp_send(c, "125 Sending file.");
    ftp_data_open(c);
    ftp_data_send(c, buf, size);
    ftp_data_close(c);
    ftp_send(c, "226 Transfer complete.");
    free(buf);
}

void handle_cmd_CWD(struct ftp_client *c, char *arg) {
    strcpy(c->cwd, arg);
    ftp_send(c, "250 Directory changed.");
}

int handle_command(struct ftp_client *c, char *buf) {
    char *chr = strchr(buf, ' ');
    char *cmd, *arg;
    if(chr != NULL) {
        int space  = chr - buf;
        arg = buf + space  + 1;
        cmd = malloc(space + 1);
        strncpy(cmd, buf, space);
        cmd[space] = 0;
    }
    else {
        cmd = buf;
    }

    for(int i = 0; i < sizeof(cmd_map)/sizeof(cmd_map)[0]; i++) {
        if(strcmp(cmd_map[i].cmd, cmd) == 0) {
            (*cmd_map[i].fptr)(c, arg);
            if(chr != NULL) {
                free(cmd);
            }
            return 0;
        }
    }

    if(chr != NULL) {
        free(cmd);
    }

    ftp_send(c, "215 Unrecognized command.");
    return 0;
}

void ftp_client_handle(struct ftp_client *c) {
    char buf[128];
    bzero(buf, 128);
    int buf_len = read(c->conn_cmd, buf, 128);

    if(errno != 0 && errno != EAGAIN) {
        perror("Client Handle Error");
        errno = 0;
        return;
    }

    if(errno != 0) {
        errno = 0;
        return;
    }

    if(buf_len == 0) {
        ftp_client_close(c);
        return;
    }

    if(buf_len == -1) {
        return;
    }

    buf[buf_len - 2] = 0;
    ftp_client_print(c, false, false);
    printf("%s\n", buf);
    handle_command(c, buf);
}

void ftp_client_close(struct ftp_client *c) {
    char addr_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(c->addr.sin_addr), addr_str, INET_ADDRSTRLEN);
    printf("=== Connection with %s:%hu closed.\n", addr_str, c->addr.sin_port);
    close(c->conn_cmd);

    for(int i = 0; i < c->server->clients_max; i++) {
        if(c->server->clients[i] == c) {
            c->server->clients[i] = NULL;
            break;
        }
    }

    free(c);
}

void ftp_client_print(struct ftp_client *c, bool server, bool data) {
    if(data) {
        printf("[%d.%d.%d.%d:%hu] ", c->data_addr[0], c->data_addr[1],
               c->data_addr[2], c->data_addr[3], c->data_port);
    }
    else {
        char str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(c->addr.sin_addr), str, INET_ADDRSTRLEN);
        printf("[%s:%hu] ", str, c->addr.sin_port);
    }

    if(server) {
        printf("%s", "> ");
    }
    else {
        printf("%s", "< ");
    }
    if(data) {
        printf("[DATA] ");
    }
}

void ftp_loop(struct ftp_server *ftp) {
    while(ftp->isopen) {
        ftp_accept(ftp);

        for(int i = 0; i < ftp->clients_max; i++) {
            if(ftp->clients[i] == NULL) {
                continue;
            }

            if(ftp->passive) {
                ftp_accept_passive(ftp->clients[i]);
            }
            ftp_client_handle(ftp->clients[i]);
        }
    }
}

