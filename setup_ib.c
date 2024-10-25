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
    local_qp_info.gid = ib_res.local_gid;

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
    local_qp_info.gid = ib_res.local_gid;

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

    /*
     * ibv_open_device() creates a context for the RDMA device. This
     * context will later be used to query its resources or for creating
     * resources and should be released with ibv_close_device().
     *
     * Unlike the verb name suggests, it doesn't actually open the device,
     * this device was opened by the kernel low-level driver and may be used
     * by other user/kernel level code. This verb only opens a context to
     * allow user level applications to use it.
     *
     */
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
    /*
     * ibv_alloc_pd() allocates a Protection Domain (PD) for an RDMA device context.
     *
     * The created PD will be used for:
     *  - Create AH, SRQ, QP
     *  - Register MR (Memory Region)
     *  - Allocate MW
     *
     * PD is a mean of protection and helps you create a group of object that can
     * work together. If several objects were created using PD1, and others were
     * created using PD2, working with objects from group1 together with objects
     * from group2 will end up with completion with error.
     *
     *  1. AH (Address Handle): An Address Handle is used to encapsulate the
     *  addressing information required to send data to a remote node. It includes
     *  details such as the destination's LID (Local Identifier) and GID (Global
     *  Identifier) in InfiniBand networks. AHs are used primarily in connectionless
     *  communication modes.
     *
     *  2. SRQ (Shared Receive Queue): A Shared Receive Queue is a type of receive
     *  queue that can be shared among multiple Queue Pairs (QPs). This allows for
     *  more efficient use of resources, as multiple QPs can post receive requests
     *  to a single SRQ, reducing the need for each QP to maintain its own separate
     *  receive queue.
     *
     *  3. QP (Queue Pair): A Queue Pair consists of two queues: a Send Queue (SQ)
     *  and a Receive Queue (RQ). QPs are the fundamental communication endpoints
     *  in RDMA. They are used to manage the sending and receiving of messages
     *  between nodes. Each QP is associated with a specific PD (Protection Domain)
     *  and can be configured for different types of communication, such as
     *  reliable or unreliable connections.
     *
     *  4. MR (Memory Region): A Memory Region is a contiguous block of memory that
     *  has been registered with the RDMA device. Registering a memory region
     *  involves informing the RDMA hardware about the memory's virtual address,
     *  physical address, and access permissions. Once registered, the memory
     *  region can be accessed directly by remote nodes using RDMA operations.
     *
     *  5. MW (Memory Window): A Memory Window is a mechanism that allows a
     *  registered Memory Region (MR) to be subdivided into smaller, more manageable
     *  segments. MWs provide a way to grant remote access to specific portions of a
     *  larger memory region without needing to register each segment individually.
     *  This can be useful for fine-grained control over memory access permissions.
     *
     */
    ib_res.pd = ibv_alloc_pd(ib_res.ctx);
    check(ib_res.pd != NULL, "Failed to allocate protection domain.");

    /*
     * ibv_query_port() returns the attributes of a port of an RDMA
     * device context.
     *
     * Here is the description for part of struct ibv_port_attr:
     *
     *  lid - The base LID of this port, valid only if state is IBV_PORT_ARMED
     *  or IBV_PORT_ACTIVE. The lid field in the struct ibv_port_attr
     *  represents the base Local Identifier (LID) of the port. This value is
     *  valid only if the port's state is either IBV_PORT_ARMED or
     *  IBV_PORT_ACTIVE. The LID is a unique identifier used in InfiniBand
     *  networks to route packets to the correct destination port.
     *
     *  active_mtu - Active maximum MTU enabled on this port to transmit and
     *  receive. It can be one of the following enumerated values which
     *  specified for max_mtu. This value specify the maximum message size
     *  that can be configured in UC/RC QPs and the maximum message size
     *  that an UD QP can transmit.
     *
     *    - IBV_MTU_256 - MTU is 256 bytes
     *    - IBV_MTU_512 - MTU is 512 bytes
     *    - IBV_MTU_1024 - MTU is 1024 bytes
     *    - IBV_MTU_2048 - MTU is 2048 bytes
     *    - IBV_MTU_4096 - MTU is 4096 bytes
     *
     *  link_layer - The link layer protocol used by this port. It can be one
     *  of the following enumerated values:
     *
     *    - IBV_LINK_LAYER_UNSPECIFIED - Legacy value, used to indicate that
     *    - the link layer protocol is InfiniBand
     *    - IBV_LINK_LAYER_INFINIBAND - Link layer protocol is InfiniBand
     *    - IBV_LINK_LAYER_ETHERNET - Link layer protocol is Ethernet, thus
     *    - IBoE (or RoCE) can be used
     *
     */
    ret = ibv_query_port(ib_res.ctx, IB_PORT, &ib_res.port_attr);
    check(ret == 0, "Failed to query IB port information.");

    if (ib_res.port_attr.link_layer == IBV_LINK_LAYER_INFINIBAND) {
        // do nothing
    } else if (ib_res.port_attr.link_layer == IBV_LINK_LAYER_ETHERNET) {
        /*
         * ibv_query_gid() returns the value of an index in The GID table of
         * an RDMA device port's.
         *
         * The content of the GID table is valid only when the port_attr.state
         * is either IBV_PORT_ARMED or IBV_PORT_ACTIVE. For other states of
         * the port, the value of the GID table is indeterminate.
         *
         * GID[0] contains the port GID, which is a constant value, provided
         * by the vendor of the RDMA device manufacture.
         *
         * The entity that configures this table (except for entry 0) is the
         * SM (Subnet Manager).
         *
         */
        ret = ibv_query_gid(ib_res.ctx, IB_PORT, IB_GID_INDEX,
                            &ib_res.local_gid);
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
     * -------------------------------------------------------------------------
     *
     * ibv_reg_mr() registers a Memory Region (MR) associated with a Protection
     * Domain. By doing that, allowing the RDMA device to read and write data to
     * this memory. Performing this registration takes some time, so performing
     * memory registration isn't recommended in the data path, when fast
     * response is required.
     *
     * Every successful registration will result with a MR which has unique
     * (within a specific RDMA device) lkey and rkey values.
     *
     * The MR's starting address is addr and its size is length. The maximum
     * size of the block that can be registered is limited to device_attr.max_mr_size.
     * Every memory address in the virtual space of the calling process can be
     * registered, including, but not limited to:
     *
     *   - Local memory (either variable or array)
     *   - Global memory (either variable or array)
     *   - Dynamically allocated memory (using malloc() or mmap())
     *   - Shared memory
     *   - Addresses from the text segment
     *
     * The registered memory buffer doesn't have to be page-aligned.
     *
     */
    ib_res.mr = ibv_reg_mr(ib_res.pd, (void *)ib_res.ib_buf, ib_res.ib_buf_size,
                           IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
                           IBV_ACCESS_REMOTE_WRITE);
    check(ib_res.mr != NULL, "Failed to register mr");

    /* query IB device attr */
    /*
     * ibv_query_device() returns the attributes of an RDMA device that is
     * associated with a context.
     *
     * Here is the description for part of struct ibv_device_attr:
     *
     *  - max_qp_wr: Maximum number of outstanding work requests on any Send
     *  or Receive Queue supported by this device
     *  - max_sge: Maximum number of scatter/gather entries per Send or
     *  Receive Work Request, in a QP other than RD, supported by this device
     *  - max_sge_rd: Maximum number of scatter/gather entries per Send or
     *  Receive Work Request, in an RD QP, supported by this device.
     *  If RD (Reliable Datagram) isn’t supported by this device, this
     *  value is zero
     *
     */
    ret = ibv_query_device(ib_res.ctx, &ib_res.dev_attr);
    check(ret == 0, "Failed to query device");

    /* create cq (complete queue)
     *
     *  ibv_create_cq() creates a Completion Queue (CQ) for an RDMA device context.
     *
     *  When an outstanding Work Request, within a Send or Receive Queue, is
     *  completed, a Work Completion is being added to the CQ of that Work Queue.
     *  This Work Completion indicates that the outstanding Work Request has been
     *  completed (and no longer considered outstanding) and provides details on
     *  it (status, direction, opcode, etc.).
     *
     *  A single CQ can be shared for sending, receiving, and sharing across
     *  multiple QPs. The Work Completion holds the information to specify the QP
     *  number and the Queue (Send or Receive) that it came from.
     *
     *  The user can define the minimum size of the CQ. The actual created size
     *  can be equal or higher than this value.
     *
     */
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
            /*
             * The maximum number of scatter/gather elements in any Work Request
             * that can be posted to the Send Queue in that Queue Pair. Value can
             * be [0..dev_cap.max_sge]. There may be RDMA devices that for
             * specific transport types may support less scatter/gather
             * elements than the maximum reported value.
             *
             */
            .max_send_sge = 1,
            /*
             * The maximum number of scatter/gather elements in any Work Request
             * that can be posted to the Receive Queue in that Queue Pair.
             * Value can be [0..dev_cap.max_sge]. There may be RDMA devices that
             * for specific transport types may support less scatter/gather
             * elements than the maximum reported value. This value is ignored
             * if the Queue Pair is associated with an SRQ (Shared Receive Queue)
             *
             */
            .max_recv_sge = 1,
            /*
             * The maximum message size (in bytes) that can be posted inline to
             * the Send Queue. 0, if no inline message is requested
             *
             * Sending inline'd data is an implementation extension that isn't
             * defined in any RDMA specification: it allows send the data itself
             * in the Work Request (instead the scatter/gather entries) that is
             * posted to the RDMA device. The memory that holds this message
             * doesn't have to be registered. There isn't any verb that
             * specifies the maximum message size that can be sent inline'd in
             * a QP. Some of the RDMA devices support it. In some RDMA devices,
             * creating a QP with will set the value of max_inline_data to the
             * size of messages that can be sent using the requested number of
             * scatter/gather elements of the Send Queue. If others, one should
             * specify explicitly the message size to be sent inline before the
             * creation of a QP. for those devices, it is advised to try to
             * create the QP with the required message size and continue
             * decreasing it if the QP creation fails.
             */
            .max_inline_data = 0,
        },
        .qp_type = IBV_QPT_RC,
    };

    /*
     * ibv_create_qp() creates a Queue Pair (QP) associated with a Protection
     * Domain.
     *
     * The user can define the minimum attributes to the QP: number of Work
     * Requests and number of scatter/gather entries per Work Request to Send
     * and Receive queues. The actual attributes can be equal or higher than
     * those values.
     */
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
