#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <infiniband/verbs.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include "ib.h"

int main() {
    struct ibv_context *context;
    struct ibv_pd *pd;
    struct ibv_mr *mr;
    struct ibv_cq *cq;
    struct ibv_qp *qp;
    char *buf, *recv_buf, *send_buf;
    struct ibv_port_attr port_attr;
    union ibv_gid my_gid;
    struct qp_info local_qp_info, remote_qp_info;
    int ret = 0;

    // open the device
    context = ib_open_device(NULL);
    if (!context) {
        fprintf(stderr, "Failed to open IB device: %s\n", strerror(errno));
        return -1;
    }

    // allocate a protection domain
    pd = ibv_alloc_pd(context);
    if (!pd) {
        fprintf(stderr, "Failed to allocate protection domain: %s\n",
                strerror(errno));
        ret = -1;
        goto err1;
    }

    fprintf(stdout, "[%s at %d]: Protection Domain allocated\n", __FILE__,
            __LINE__);

    // register the memory region
    //
    // the first part of memory region is for receive buffer and
    // the second patr of memory region is for send buffer
    buf = (char *)malloc(MR_SIZE);
    memset(buf, 0, MR_SIZE);
    if (!buf) {
        fprintf(stderr, "Failed to allocate memory: %s\n", strerror(errno));
        ret = -1;
        goto err2;
    }

    mr = ibv_reg_mr(pd, buf, MR_SIZE,
                    IBV_ACCESS_LOCAL_WRITE |
                    IBV_ACCESS_REMOTE_READ |
                    IBV_ACCESS_REMOTE_WRITE);
    if (!mr) {
        fprintf(stderr, "Failed to register memory region: %s\n",
                strerror(errno));
        ret = -1;
        goto err3;
    }

    recv_buf = buf;
    send_buf= buf + MSG_SIZE;

    fprintf(stdout, "[%s at %d]: Memory Region registered. "
            "Recv bufffer: %p, Send buffer: %p\n", __FILE__, __LINE__,
            recv_buf, send_buf);

    // create a completion queue
    cq = ibv_create_cq(context, 1, NULL, NULL, 0);
    if (!cq) {
        fprintf(stderr, "Failed to create Completion Queue: %s\n",
                strerror(errno));
        ret = -1;
        goto err4;
    }

    fprintf(stdout, "[%s at %d]: Completion Queue created\n", __FILE__,
            __LINE__);

    // create a queue pair
    qp = ib_create_qp(cq, pd);
    if (!qp) {
        fprintf(stderr, "Failed to create Queue Pair: %s\n", strerror(errno));
        ret = -1;
        goto err5;
    }

    fprintf(stdout, "[%s at %d]: Queue Pair created\n", __FILE__,
            __LINE__);

    // get port attributes
    if (ibv_query_port(context, IB_PORT, &port_attr)) {
        fprintf(stderr, "Failed to query port: %s\n", strerror(errno));
        ret = -1;
        goto err6;
    }

    // get GID
    if (ibv_query_gid(context, IB_PORT, IB_GID_INDEX, &my_gid)) {
        fprintf(stderr, "Failed to query GID: %s\n", strerror(errno));
        ret = -1;
        goto err6;
    }

    // exchange QP info
    local_qp_info.qp_num = qp->qp_num;
    local_qp_info.lid = port_attr.lid;
    local_qp_info.raddr = (uint64_t)recv_buf;
    local_qp_info.rkey = mr->rkey;
    memcpy(&local_qp_info.gid, &my_gid, sizeof(local_qp_info.gid));

    if (ib_ctx_xchg_qp_info_as_server(local_qp_info, &remote_qp_info) < 0) {
        fprintf(stderr, "exchange QP info failed\n");
        ret = -1;
        goto err6;
    }

    fprintf(stdout, "[%s at %d]: Queue Pair Info exchanged\n", __FILE__,
            __LINE__);

    // transition the QP to the INIT state
    if (ib_modify_qp_to_init(qp)) {
        fprintf(stderr, "Failed to modify QP to INIT: %s\n", strerror(errno));
        ret = -1;
        goto err6;
    }

    fprintf(stdout, "[%s at %d]: Queue Pair transit to INIT state\n", __FILE__,
            __LINE__);

    // transition the QP to the RTR state
    if (ib_modify_qp_to_rtr(qp, port_attr.active_mtu, remote_qp_info)) {
        fprintf(stderr, "Failed to modify QP to RTR\n");
        ret = -1;
        goto err6;
    }

    fprintf(stdout, "[%s at %d]: Queue Pair transit to RTR state\n", __FILE__,
            __LINE__);

    // transition the QP to the RTS state
    if (ib_modify_qp_to_rts(qp)) {
        fprintf(stderr, "Failed to modify QP to RTS\n");
        ret = -1;
        goto err6;
    }

    fprintf(stdout, "[%s at %d]: Queue Pair transit to RTS state\n", __FILE__,
            __LINE__);

    int num_of_loops = 1;
    int total_loops = 10;

    for (; num_of_loops <= total_loops; num_of_loops++) {
        // wait for remote info
        while (recv_buf[0] != num_of_loops && recv_buf[MSG_SIZE - 1] != num_of_loops) {}

        fprintf(stdout, "[%s at %d]: Message received for round %d\n", __FILE__,
                __LINE__, num_of_loops);

        // prepare the message
        memset(send_buf, num_of_loops, MSG_SIZE);

        // write to remote end
        if (num_of_loops % 2 == 0) {
            ret = ib_post_write_signaled(send_buf, MSG_SIZE, mr->lkey, 0,
                                   remote_qp_info.rkey, remote_qp_info.raddr, qp);
        } else {
            ret = ib_post_write_unsignaled(send_buf, MSG_SIZE, mr->lkey, 0,
                                     remote_qp_info.rkey, remote_qp_info.raddr, qp);
        }
        if (ret != 0) {
            fprintf(stderr, "Failed to write to remote end for round %d: %s\n",
                    num_of_loops, strerror(errno));
            ret = -1;
            goto err6;
        }

        fprintf(stdout, "[%s at %d]: Message sent for round %d\n", __FILE__,
                __LINE__, num_of_loops);
    }

err6:
    ibv_destroy_qp(qp);
err5:
    ibv_destroy_cq(cq);
err4:
    ibv_dereg_mr(mr);
err3:
    free(buf);
err2:
    ibv_dealloc_pd(pd);
err1:
    ibv_close_device(context);
    return ret;
}
