#include <stdlib.h>
#include <stdio.h>
#include <signal.h>

#include "ftp.h"

struct ftp_server *ftp;

void handle_sigint(int sig) {
    ftp_free(ftp);
    printf("Interrupted, closing server.\n");
    exit(0);
}

int main() {
    signal(SIGINT, handle_sigint);
    ftp = ftp_init();
    ftp_cmd_open(ftp);
    printf("Started server.\n");
    ftp_loop(ftp);
    ftp_free(ftp);
    return 0;
}

