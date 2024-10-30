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

#define MSG_SIZE 4096
#define TCP_PORT 12345
#define IB_PORT             1
#define IB_GID_INDEX        3

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

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <server address>\n", argv[0]);
        return 1;
    }

    context = ib_open_device(NULL);
    if (!context) {
        perror("Failed to open device");
        return 1;
    }

    // Allocate a protection domain
    pd = ibv_alloc_pd(context);
    if (!pd) {
        perror("Failed to allocate PD");
        return 1;
    }

    // Allocate memory
    buf = malloc(MSG_SIZE);
    if (!buf) {
        perror("Failed to allocate memory");
        return 1;
    }

    // Register the memory region
    mr = ibv_reg_mr(pd, buf, MSG_SIZE,
                    IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
                    IBV_ACCESS_REMOTE_WRITE);
    if (!mr) {
        perror("Failed to register MR");
        return 1;
    }

    // Create a completion queue
    cq = ibv_create_cq(context, 1, NULL, NULL, 0);
    if (!cq) {
        perror("Failed to create CQ");
        return 1;
    }

    // Create a queue pair
    qp = ib_create_qp(cq, pd);
    if (!qp) {
        perror("Failed to create QP");
        return 1;
    }

    // Get port attributes
    if (ibv_query_port(context, IB_PORT, &port_attr)) {
        perror("Failed to query port");
        return 1;
    }

    // Get GID
    if (ibv_query_gid(context, IB_PORT, IB_GID_INDEX, &my_gid)) {
        perror("Failed to query GID");
        return 1;
    }

    // Transition the QP to the INIT state
    if (ib_modify_qp_to_init(qp)) {
        perror("Failed to modify QP to INIT");
        return 1;
    }

    // Prepare local QP info
    local_qp_info.qp_num = qp->qp_num;
    local_qp_info.lid = port_attr.lid;
    memcpy(&local_qp_info.gid, &my_gid, sizeof(local_qp_info.gid));

    if (ib_ctx_xchg_qp_info_as_client(argv[1], TCP_PORT, local_qp_info,
                                      &remote_qp_info) < 0) {
        fprintf(stderr, "exchange QP info failed\n");
        return 1;
    }

    // Transition the QP to the RTR state
    if (ib_modify_qp_to_rtr(qp, port_attr.active_mtu, remote_qp_info)) {
        perror("Failed to modify QP to RTR");
        return 1;
    }

    // Transition the QP to the RTS state
    if (ib_modify_qp_to_rts(qp)) {
        perror("Failed to modify QP to RTS");
        return 1;
    }

    // Prepare the message
    strcpy(buf, "Hello, RDMA!");

    // Post a send request
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
        perror("Failed to post send WR");
        return 1;
    }

    // Wait for the send completion
    struct ibv_wc wc;
    while (ibv_poll_cq(cq, 1, &wc) == 0);

    if (wc.status != IBV_WC_SUCCESS) {
        fprintf(stderr, "Failed status %s (%d) for wr_id %d\n",
                ibv_wc_status_str(wc.status), wc.status, (int)wc.wr_id);
        return 1;
    }

    printf("Message sent: %s\n", buf);

    // Clean up
    ibv_destroy_qp(qp);
    ibv_destroy_cq(cq);
    ibv_dereg_mr(mr);
    free(buf);
    ibv_dealloc_pd(pd);
    ibv_close_device(context);

    return 0;
}
