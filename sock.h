#ifndef __SOCK_H__
#define __SOCK_H__

#include <inttypes.h>

#include "ib.h"

#define SOCK_SYNC_MSG "sync"

ssize_t sock_read(int sock_fd, void *buffer, size_t len);
ssize_t sock_write(int sock_fd, void *buffer, size_t len);

int sock_create_bind(char *port);
int sock_create_connect(char *server_name, char *port);

int sock_set_qp_info(int sock_fd, struct QPInfo *qp_info);
int sock_get_qp_info(int sock_fd, struct QPInfo *qp_info);

#endif /* __SOCK_H__ */
