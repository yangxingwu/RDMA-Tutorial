#include <arpa/inet.h>
#include <infiniband/verbs.h>
#include <unistd.h>
#include <malloc.h>

#include "sock.h"
#include "ib.h"
#include "debug.h"
#include "config.h"
#include "setup_ib.h"

struct IBRes ib_res;

int connect_qp_server() {
    int ret = 0, n = 0;
    int sockfd = 0;
    int peer_sockfd = 0;
    struct sockaddr_in peer_addr;
    socklen_t peer_addr_len = sizeof(struct sockaddr_in);
    char sock_buf[64] = {'\0'};
    struct QPInfo local_qp_info, remote_qp_info;

    sockfd = sock_create_bind(config_info.sock_port);
    check(sockfd > 0, "Failed to create server socket.");

    listen(sockfd, 5);

    peer_sockfd = accept(sockfd, (struct sockaddr *)&peer_addr,
                         &peer_addr_len);
    check(peer_sockfd > 0, "Failed to create peer_sockfd");

    /* init local qp_info */
    /*
     * LID - The lid field in the struct ibv_port_attr represents the base Local
     * Identifier (LID) of the port. This value is valid only if the port's
     * state is either IBV_PORT_ARMED or IBV_PORT_ACTIVE. The LID is a unique
     * identifier used in InfiniBand networks to route packets to the correct
     * destination port.
     *
     * In InfiniBand networks, both Local Identifier (LID) and Global Identifier
     * (GID) are used for addressing, but they serve different purposes and
     * have different characteristics:
     *
     * LID is a shorter, locally unique identifier used within a single
     * InfiniBand subnet for efficient routing.
     *
     * GID is a longer, globally unique identifier used for routing across
     * multiple subnets, ensuring global uniqueness.
     *
     */
    local_qp_info.lid  = ib_res.port_attr.lid;
    local_qp_info.qp_num = ib_res.qp->qp_num;
    local_qp_info.gid = ib_res.gid_info.local_gid;

    /* get qp_info from client */
    ret = sock_get_qp_info(peer_sockfd, &remote_qp_info);
    check(ret == 0, "Failed to get qp_info from client");

    /* send qp_info to client */
    ret = sock_set_qp_info(peer_sockfd, &local_qp_info);
    check(ret == 0, "Failed to send qp_info to client");

    /* change send QP state to RTS (Ready To Send) */
    ret = modify_qp_to_rts(ib_res.qp, &remote_qp_info);
    check(ret == 0, "Failed to modify qp to rts");

    log(LOG_SUB_HEADER, "IB Config");
    log("\tqp[%"PRIu32"] <-> qp[%"PRIu32"]",
        ib_res.qp->qp_num, remote_qp_info.qp_num);
    log("local address: LID %#04x QPN %#06x", local_qp_info.lid, local_qp_info.qp_num);
    log("GID: %02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d",
        local_qp_info.gid.raw[0], local_qp_info.gid.raw[1],
        local_qp_info.gid.raw[2], local_qp_info.gid.raw[3],
        local_qp_info.gid.raw[4], local_qp_info.gid.raw[5],
        local_qp_info.gid.raw[6], local_qp_info.gid.raw[7],
        local_qp_info.gid.raw[8], local_qp_info.gid.raw[9],
        local_qp_info.gid.raw[10], local_qp_info.gid.raw[11],
        local_qp_info.gid.raw[12], local_qp_info.gid.raw[13],
        local_qp_info.gid.raw[14], local_qp_info.gid.raw[15]);
    log("remote address: LID %#04x QPN %#06x", remote_qp_info.lid, remote_qp_info.qp_num);
    log("GID: %02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d",
        remote_qp_info.gid.raw[0], remote_qp_info.gid.raw[1],
        remote_qp_info.gid.raw[2], remote_qp_info.gid.raw[3],
        remote_qp_info.gid.raw[4], remote_qp_info.gid.raw[5],
        remote_qp_info.gid.raw[6], remote_qp_info.gid.raw[7],
        remote_qp_info.gid.raw[8], remote_qp_info.gid.raw[9],
        remote_qp_info.gid.raw[10], remote_qp_info.gid.raw[11],
        remote_qp_info.gid.raw[12], remote_qp_info.gid.raw[13],
        remote_qp_info.gid.raw[14], remote_qp_info.gid.raw[15]);
    log(LOG_SUB_HEADER, "End of IB Config");

    /* sync with clients */
    n = sock_read(peer_sockfd, sock_buf, sizeof(SOCK_SYNC_MSG));
    check(n == sizeof(SOCK_SYNC_MSG), "Failed to receive sync from client");

    n = sock_write(peer_sockfd, sock_buf, sizeof(SOCK_SYNC_MSG));
    check(n == sizeof(SOCK_SYNC_MSG), "Failed to write sync to client");

    close(peer_sockfd);
    close(sockfd);

    return 0;

error:
    if (peer_sockfd > 0)
        close (peer_sockfd);
    if (sockfd > 0)
        close (sockfd);

    return -1;
}

int connect_qp_client() {
    int ret = 0, n = 0;
    int peer_sockfd = 0;
    char sock_buf[64] = {'\0'};

    struct QPInfo local_qp_info, remote_qp_info;

    peer_sockfd = sock_create_connect(config_info.server_name,
                                       config_info.sock_port);
    check(peer_sockfd > 0, "Failed to create peer_sockfd");

    local_qp_info.lid = ib_res.port_attr.lid;
    local_qp_info.qp_num = ib_res.qp->qp_num;
    local_qp_info.gid = ib_res.gid_info.local_gid;

    /* send qp_info to server */
    ret = sock_set_qp_info(peer_sockfd, &local_qp_info);
    check(ret == 0, "Failed to send qp_info to server");

    /* get qp_info from server */
    ret = sock_get_qp_info(peer_sockfd, &remote_qp_info);
    check(ret == 0, "Failed to get qp_info from server");

    /* change QP state to RTS */
    ret = modify_qp_to_rts(ib_res.qp, &remote_qp_info);
    check(ret == 0, "Failed to modify qp to rts");

    log(LOG_SUB_HEADER, "IB Config");
    log("\tqp[%"PRIu32"] <-> qp[%"PRIu32"]",
        ib_res.qp->qp_num, remote_qp_info.qp_num);
    log("local address: LID %#04x QPN %#06x", local_qp_info.lid, local_qp_info.qp_num);
    log("GID: %02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d",
        local_qp_info.gid.raw[0], local_qp_info.gid.raw[1],
        local_qp_info.gid.raw[2], local_qp_info.gid.raw[3],
        local_qp_info.gid.raw[4], local_qp_info.gid.raw[5],
        local_qp_info.gid.raw[6], local_qp_info.gid.raw[7],
        local_qp_info.gid.raw[8], local_qp_info.gid.raw[9],
        local_qp_info.gid.raw[10], local_qp_info.gid.raw[11],
        local_qp_info.gid.raw[12], local_qp_info.gid.raw[13],
        local_qp_info.gid.raw[14], local_qp_info.gid.raw[15]);
    log("remote address: LID %#04x QPN %#06x", remote_qp_info.lid, remote_qp_info.qp_num);
    log("GID: %02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d",
        remote_qp_info.gid.raw[0], remote_qp_info.gid.raw[1],
        remote_qp_info.gid.raw[2], remote_qp_info.gid.raw[3],
        remote_qp_info.gid.raw[4], remote_qp_info.gid.raw[5],
        remote_qp_info.gid.raw[6], remote_qp_info.gid.raw[7],
        remote_qp_info.gid.raw[8], remote_qp_info.gid.raw[9],
        remote_qp_info.gid.raw[10], remote_qp_info.gid.raw[11],
        remote_qp_info.gid.raw[12], remote_qp_info.gid.raw[13],
        remote_qp_info.gid.raw[14], remote_qp_info.gid.raw[15]);
    log(LOG_SUB_HEADER, "End of IB Config");

    /* sync with server */
    n = sock_write(peer_sockfd, sock_buf, sizeof(SOCK_SYNC_MSG));
    check(n == sizeof(SOCK_SYNC_MSG), "Failed to write sync to client");

    n = sock_read(peer_sockfd, sock_buf, sizeof(SOCK_SYNC_MSG));
    check(n == sizeof(SOCK_SYNC_MSG), "Failed to receive sync from client");

    close(peer_sockfd);
    return 0;

error:
    if (peer_sockfd > 0)
        close (peer_sockfd);

    return -1;
}

static struct ibv_context *__ctx_open_device(const char *ib_devname) {
    int num_of_devices;
    struct ibv_device **list;
    struct ibv_device *dev = NULL;
    struct ibv_context *ctx = NULL;

    list = ibv_get_device_list(&num_of_devices);
    check(list != NULL, "Failed to get ib device list.");
    check(num_of_devices > 0, "No IB devices found.");

    if (ib_devname == NULL || ib_devname[0] == '\0') {
        // return the first available device
        dev = list[0];
    } else {
        for (; (dev = *list); ++list)
            if (!strcmp(ibv_get_device_name(dev), ib_devname))
                break;
        check(dev != NULL, "IB device %s not found\n", ib_devname);
    }

    ctx = ibv_open_device(dev);

error:
    if (list != NULL)
        ibv_free_device_list(list);
    return ctx;
}

int setup_ib(const char *ib_devname) {
    int ret = 0;

    memset(&ib_res, 0, sizeof(struct IBRes));

    // refer to https://www.rdmamojo.com/2012/05/24/ibv_fork_init/
    // ibv_fork_init() should be called before calling any other
    // function in libibverbs.
    ret = ibv_fork_init();
    check(ret == 0, "Failed to ibv_fork_init.");

    /* create IB context */
    ib_res.ctx = __ctx_open_device(ib_devname);
    check(ib_res.ctx != NULL, "Failed to open ib device.");

    /* allocate protection domain */
    ib_res.pd = ibv_alloc_pd(ib_res.ctx);
    check(ib_res.pd != NULL, "Failed to allocate protection domain.");

    /* query IB port attribute */
    ret = ibv_query_port(ib_res.ctx, IB_PORT, &ib_res.port_attr);
    check(ret == 0, "Failed to query IB port information.");

    ib_res.gid_info.link_layer = ib_res.port_attr.link_layer;
    if (ib_res.gid_info.link_layer == IBV_LINK_LAYER_INFINIBAND) {
        // do nothing
    } else if (ib_res.gid_info.link_layer == IBV_LINK_LAYER_ETHERNET) {
        ret = ibv_query_gid(ib_res.ctx, IB_PORT, IB_GID_INDEX,
                            &ib_res.gid_info.local_gid);
        check(ret == 0, "Failed to query GID information.");
    } else { // IBV_LINK_LAYER_UNSPECIFIED
        // do nothing
    }

    /* register mr (memory region) */
    ib_res.ib_buf_size = config_info.msg_size * config_info.num_concurr_msgs;
    ib_res.ib_buf      = (char *)memalign(4096, ib_res.ib_buf_size);
    check(ib_res.ib_buf != NULL, "Failed to allocate ib_buf");

    /*
     * struct ibv_mr *, Pointer to the newly allocated Memory Region.
     *
     * This pointer also contains the following fields:
     *
     * lkey - The value that will be used to refer to this MR using a local access
     * rkey - The value that will be used to refer to this MR using a remote access
     *
     * Those values may be equal, but this isn't always guaranteed.
     *
     */
    ib_res.mr = ibv_reg_mr(ib_res.pd, (void *)ib_res.ib_buf, ib_res.ib_buf_size,
                           IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
                           IBV_ACCESS_REMOTE_WRITE);
    check(ib_res.mr != NULL, "Failed to register mr");

    /* query IB device attr */
    ret = ibv_query_device(ib_res.ctx, &ib_res.dev_attr);
    check(ret == 0, "Failed to query device");

    /* create cq (complete queue) */
    ib_res.cq = ibv_create_cq(ib_res.ctx, ib_res.dev_attr.max_cqe,
                              NULL, NULL, 0);
    check(ib_res.cq != NULL, "Failed to create cq");

    /* create qp (queue pair) */
    /*
     * refer to https://www.rdmamojo.com/2012/12/21/ibv_create_qp/
     *
     * for max_send_wr and max_recv_wr
     *
     * the maximum number of outstanding Work Requests that can be
     * posted to the Send/receive Queue in that Queue Pair. Value can be
     * [0..dev_cap.max_qp_wr]. There may be RDMA devices that for
     * specific transport types may support less outstanding Work
     * Requests than the maximum reported value.
     *
     */
    struct ibv_qp_init_attr qp_init_attr = {
        .send_cq = ib_res.cq,
        .recv_cq = ib_res.cq,
        .cap = {
            .max_send_wr = 2, // [0..ib_res.dev_attr.max_qp_wr]
            .max_recv_wr = 2, // [0..ib_res.dev_attr.max_qp_wr]
            .max_send_sge = 1,
            .max_recv_sge = 1,
            .max_inline_data = 0,
        },
        .qp_type = IBV_QPT_RC,
    };

    ib_res.qp = ibv_create_qp(ib_res.pd, &qp_init_attr);
    check(ib_res.qp != NULL, "Failed to create qp");

    /* connect QP */
    if (config_info.is_server) {
        ret = connect_qp_server();
    } else {
        ret = connect_qp_client();
    }
    check(ret == 0, "Failed to connect qp");

    return 0;

error:
    return -1;
}

void close_ib_connection() {
    if (ib_res.qp != NULL)
        ibv_destroy_qp(ib_res.qp);

    if (ib_res.cq != NULL)
        ibv_destroy_cq(ib_res.cq);

    if (ib_res.mr != NULL)
        ibv_dereg_mr(ib_res.mr);

    if (ib_res.pd != NULL)
        ibv_dealloc_pd(ib_res.pd);

    if (ib_res.ctx != NULL)
        ibv_close_device(ib_res.ctx);

    if (ib_res.ib_buf != NULL)
        free(ib_res.ib_buf);
}
