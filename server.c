#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <argtable2.h>

#include "ftp.h"

struct ftp_server *ftp;

void handle_sigint(int sig) {
    if(ftp != NULL) {
        ftp_close(ftp);
    }
}

int main(int argc, char **argv) {
    struct arg_int *arg_cmdport = arg_int0("p", "cmdport", "<port>",
            "Command port");
    struct arg_int *arg_dataport = arg_int0(NULL, "dataport", "<port>",
            "Data port");
    struct arg_lit *arg_help = arg_lit0("h", "help", "Print help and exit");
    struct arg_int *arg_clientsmax = arg_int0(NULL, "maxclients", "<n>",
            "Set maximum amount of simultanious connections");
    struct arg_end *end = arg_end(20);
    void *argtable[] = {
        arg_help,
        arg_cmdport,
        arg_dataport,
        arg_clientsmax,
        end,
    };

    if(arg_nullcheck(argtable) != 0) {
        printf("error: insufficient memory\n");
        return 1;
    }

    // Fill default arguments
    arg_cmdport->ival[0] = 21;
    arg_dataport->ival[0] = 20;
    arg_clientsmax->ival[0] = 8;

    int nerrors = arg_parse(argc,argv,argtable);

    if (nerrors != 0) {
        arg_print_errors(stdout, end, argv[0]);
        printf("Try %s --help for more information.\n", argv[0]);
        return 1;
    }

    if(arg_help->count) {
        printf("nova-ftp - Tiny FTP server by Marcin Wieczorek 2019\n"
               "Usage: %s", argv[0]);
        arg_print_syntaxv(stdout, argtable, "\n\nOptions:\n");
        arg_print_glossary(stdout, argtable, " %-25s %s\n");
        puts("");
        return 0;
    }

    // Server startup
    signal(SIGINT, handle_sigint);
    ftp = ftp_init();

    // Pass arguments to the server
    ftp->cmd_port = arg_cmdport->ival[0];
    ftp->data_port = arg_dataport->ival[0];
    ftp->clients_max = arg_clientsmax->ival[0];

    ftp_cmd_open(ftp);
    printf("=== Started server.\n");
    ftp_loop(ftp);
    ftp_free(ftp);
    arg_freetable(argtable, sizeof(argtable)/sizeof(argtable[0]));
    printf("=== Server closed.\n");
    return 0;
}

