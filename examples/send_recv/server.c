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

int main() {
    struct ibv_context *context;
    struct ibv_pd *pd;
    struct ibv_mr *mr;
    struct ibv_cq *cq;
    struct ibv_qp *qp;
    struct ibv_sge sge;
    struct ibv_recv_wr recv_wr, *bad_recv_wr;
    char *buf;
    struct ibv_port_attr port_attr;
    union ibv_gid my_gid;
    struct qp_info local_qp_info, remote_qp_info;
    int sockfd, newsockfd;
    struct sockaddr_in serv_addr, cli_addr;
    socklen_t clilen;

    // open the device
    context = ib_open_device(NULL);
    if (!context) {
        perror("Failed to open device");
        return 1;
    }

    // allocate a protection domain
    pd = ibv_alloc_pd(context);
    if (!pd) {
        perror("Failed to allocate PD");
        return 1;
    }

    // allocate memory
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
    memcpy(local_qp_info.gid.raw, my_gid.raw, sizeof(local_qp_info.gid));

    // Create a TCP socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Failed to create socket");
        return 1;
    }

    // Bind the socket to the port
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(TCP_PORT);

    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Failed to bind socket");
        return 1;
    }

    // Listen for incoming connections
    listen(sockfd, 1);
    clilen = sizeof(cli_addr);
    newsockfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
    if (newsockfd < 0) {
        perror("Failed to accept connection");
        return 1;
    }

    // Send local QP info to the client
    if (write(newsockfd, &local_qp_info, sizeof(local_qp_info)) < 0) {
        perror("Failed to send QP info");
        return 1;
    }

    // Receive remote QP info from the client
    if (read(newsockfd, &remote_qp_info, sizeof(remote_qp_info)) < 0) {
        perror("Failed to receive QP info");
        return 1;
    }

    close(newsockfd);
    close(sockfd);

    print_gid(my_gid);
    print_gid(remote_qp_info.gid);

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

    // Post a receive request
    memset(&sge, 0, sizeof(sge));
    sge.addr = (uintptr_t)buf;
    sge.length = MSG_SIZE;
    sge.lkey = mr->lkey;

    memset(&recv_wr, 0, sizeof(recv_wr));
    recv_wr.sg_list = &sge;
    recv_wr.num_sge = 1;

    if (ibv_post_recv(qp, &recv_wr, &bad_recv_wr)) {
        perror("Failed to post receive WR");
        return 1;
    }

    // Wait for the receive completion
    struct ibv_wc wc;
    while (ibv_poll_cq(cq, 1, &wc) == 0);

    if (wc.status != IBV_WC_SUCCESS) {
        fprintf(stderr, "Failed status %s (%d) for wr_id %d\n",
                ibv_wc_status_str(wc.status), wc.status, (int)wc.wr_id);
        return 1;
    }

    printf("Received message: %s\n", buf);

    // Clean up
    ibv_destroy_qp(qp);
    ibv_destroy_cq(cq);
    ibv_dereg_mr(mr);
    free(buf);
    ibv_dealloc_pd(pd);
    ibv_close_device(context);

    return 0;
}
