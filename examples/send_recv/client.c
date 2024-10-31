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
    struct ibv_sge sge;
    struct ibv_send_wr send_wr, *bad_send_wr;
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

    // prepare the message
    strcpy(buf, "Hello, RDMA!");

    // post a send request
    memset(&sge, 0, sizeof(sge));
    sge.addr = (uintptr_t)buf;
    sge.length = MSG_SIZE;
    sge.lkey = mr->lkey;

    memset(&send_wr, 0, sizeof(send_wr));
    send_wr.sg_list = &sge;
    send_wr.num_sge = 1;
    send_wr.opcode = IBV_WR_SEND;
    send_wr.send_flags = IBV_SEND_SIGNALED;

    if (ibv_post_send(qp, &send_wr, &bad_send_wr)) {
        fprintf(stderr, "Failed to post send WR\n");
        ret = -1;
        goto err6;
    }

    fprintf(stdout, "[%s at %d]: A work request has been posted to send queue\n",
            __FILE__, __LINE__);

    // wait for the send completion
    struct ibv_wc wc;
    while (ibv_poll_cq(cq, 1, &wc) == 0);

    if (wc.status != IBV_WC_SUCCESS) {
        fprintf(stderr, "Failed status %s (%d) for wr_id %d\n",
                ibv_wc_status_str(wc.status), wc.status, (int)wc.wr_id);
        ret = -1;
        goto err6;
    }

    fprintf(stdout, "[%s at %d]: Message sent: %s\n", __FILE__, __LINE__,
            buf);

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
