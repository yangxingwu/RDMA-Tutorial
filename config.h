#ifndef __CONFIG_H__
#define __CONFIG_H__

#include <stdbool.h>
#include <inttypes.h>

struct ConfigInfo {
    bool is_server;          /* if the current node is server */

    int  msg_size;           /* the size of each echo message */
    int  num_concurr_msgs;   /* the number of messages can be sent concurrently */

    char *sock_port;         /* socket port number */
    char *server_name;       /* server name */
}__attribute__((aligned(64)));

extern struct ConfigInfo config_info;

void print_config_info();

#endif /* __CONFIG_H__ */
