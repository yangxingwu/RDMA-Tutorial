#ifndef __SETUP_IB_H__
#define __SETUP_IB_H__

#include <infiniband/verbs.h>

struct IBRes {
    struct ibv_context      *ctx;
    struct ibv_pd           *pd;
    struct ibv_mr           *mr;
    struct ibv_cq           *cq;
    struct ibv_qp           *qp;
    struct ibv_port_attr    port_attr;
    struct ibv_device_attr  dev_attr;
    union  ibv_gid          local_gid;


    char    *ib_buf;
    size_t  ib_buf_size;
};

extern struct IBRes ib_res;

int setup_ib(const char *ib_devname);
void close_ib_connection();

int connect_qp_server();
int connect_qp_client();

#endif /* __SETUP_IB_H__ */
