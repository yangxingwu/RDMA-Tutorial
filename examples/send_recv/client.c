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

int main(int argc, char *argv[]) {
    struct ibv_context *context;
    struct ibv_pd *pd;
    struct ibv_mr *mr;
    struct ibv_cq *cq;
    struct ibv_qp *qp;
    char *buf;
    struct ibv_port_attr port_attr;
    union ibv_gid my_gid;
    struct qp_info local_qp_info, remote_qp_info;
    struct sockaddr_in svr_addr;
    int ret = 0;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <server address>\n", argv[0]);
        return -1;
    }

    memset(&svr_addr, 0, sizeof(struct sockaddr_in));
    svr_addr.sin_family = AF_INET;
    svr_addr.sin_port = htons(TCP_PORT);
    if (inet_pton(AF_INET, argv[1], &svr_addr.sin_addr) != 1) {
        fprintf(stderr, "Invalid address or address (%s) not supported",
                argv[1]);
        return -1;
    }

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
    buf = malloc(MSG_SIZE);
    if (!buf) {
        fprintf(stderr, "Failed to allocate memory: %s\n", strerror(errno));
        ret = -1;
        goto err2;
    }

    mr = ibv_reg_mr(pd, buf, MSG_SIZE,
                    IBV_ACCESS_LOCAL_WRITE |
                    IBV_ACCESS_REMOTE_READ |
                    IBV_ACCESS_REMOTE_WRITE);
    if (!mr) {
        fprintf(stderr, "Failed to register memory region: %s\n",
                strerror(errno));
        ret = -1;
        goto err3;
    }

    fprintf(stdout, "[%s at %d]: Memory Region registered\n", __FILE__,
            __LINE__);

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
    memcpy(&local_qp_info.gid, &my_gid, sizeof(local_qp_info.gid));

    if (ib_ctx_xchg_qp_info_as_client(&svr_addr, local_qp_info,
                                      &remote_qp_info) < 0) {
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
    int num_of_cq = 0;
    struct ibv_wc wc;

    for (; num_of_loops <= total_loops; num_of_loops++) {
        // prepare the message
        snprintf(buf, MSG_SIZE, "Hello from client: round %d!", num_of_loops);

        // post a send request
        if (ib_post_send(buf, MSG_SIZE, mr->lkey, 0, qp)) {
            fprintf(stderr, "Failed to post send WR for round %d\n",
                    num_of_loops);
            ret = -1;
            goto err6;
        }

        // wait for the send completion
        do {
            num_of_cq = ibv_poll_cq(cq, 1, &wc);
        } while (num_of_cq == 0);

        if (num_of_cq < 0) {
            fprintf(stderr, "Failed to poll completion queue: %s\n",
                    strerror(errno));
            goto err6;
        }

        // verify the completion status
        if (wc.status != IBV_WC_SUCCESS) {
            fprintf(stderr, "Failed status %s (%d) for wr_id %d\n",
                    ibv_wc_status_str(wc.status), wc.status, (int)wc.wr_id);
            goto err6;
        }

        fprintf(stdout, "[%s at %d]: Message sent: %s\n", __FILE__, __LINE__,
                buf);

        // post a recv request
        if (ib_post_recv(buf, MSG_SIZE, mr->lkey, 0, qp)) {
            fprintf(stderr, "Failed to post receive WR\n");
            ret = -1;
            goto err6;
        }

        // wait for the recv completion
        do {
            num_of_cq = ibv_poll_cq(cq, 1, &wc);
        } while (num_of_cq == 0);

        if (num_of_cq < 0) {
            fprintf(stderr, "Failed to poll completion queue: %s\n",
                    strerror(errno));
            goto err6;
        }

        // verify the completion status
        if (wc.status != IBV_WC_SUCCESS) {
            fprintf(stderr, "Failed status %s (%d) for wr_id %d\n",
                    ibv_wc_status_str(wc.status), wc.status, (int)wc.wr_id);
            goto err6;
        }

        fprintf(stdout, "[%s at %d]: Received message: %s\n", __FILE__, __LINE__,
                buf);
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
