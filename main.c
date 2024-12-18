#include <stdio.h>
#include <stdlib.h>

#include "debug.h"
#include "config.h"
#include "setup_ib.h"
#include "client.h"
#include "server.h"

FILE *log_fp = NULL;

static int init_env() {
    if (config_info.is_server) {
        log_fp = fopen("server.log", "w");
    } else {
        log_fp = fopen("client.log", "w");
    }

    check(log_fp != NULL, "Failed to open log file");

    log(LOG_HEADER, "IB Echo Server");
    print_config_info();

    return 0;
error:
    return -1;
}

static void destroy_env() {
    log(LOG_HEADER, "Run Finished");

    if (log_fp != NULL)
        fclose(log_fp);
}

int main(int argc, char *argv[]) {
    int ret = 0;

    if (argc == 5) {
        config_info.is_server        = false;
        config_info.server_name      = argv[1];
        config_info.msg_size         = atoi(argv[2]);
        config_info.num_concurr_msgs = atoi(argv[3]);
        config_info.sock_port        = argv[4];
    } else if (argc == 4) {
        config_info.is_server        = true;
        config_info.msg_size         = atoi (argv[1]);
        config_info.num_concurr_msgs = atoi (argv[2]);
        config_info.sock_port        = argv[3];
    } else {
        printf("Server: %s msg_size num_concurr_msgs sock_port\n", argv[0]);
        printf("Client: %s server_name msg_size num_concurr_msgs sock_port\n", argv[0]);
        return 0;
    }

    ret = init_env();
    check(ret == 0, "Failed to init env");

    ret = setup_ib(NULL);
    check(ret == 0, "Failed to setup IB");

    if (config_info.is_server) {
        ret = run_server();
    } else {
        ret = run_client();
    }
    check(ret == 0, "Failed to run workload");

error:
    close_ib_connection();
    destroy_env();
    return ret;
}
