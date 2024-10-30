#include <infiniband/verbs.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>
#include <net/if.h>
#include "ib.h"

void print_gid(union ibv_gid gid) {
    for (int i = 0; i < GID_LEN; i++) {
        printf("%02d", gid.raw[i]);
        if (i < (GID_LEN - 1))
            printf(":");
    }
    printf("\n");
}

static int __ibdev_2_netdev(const char *ib_dev_name,
                             char *net_dev_name, const int net_dev_name_len) {
    char command[256] = {'\0'};
    FILE *fp = NULL;

    snprintf(command, sizeof(command),
             "ls /sys/class/infiniband/%s/device/net", ib_dev_name);

    fp = popen(command, "r");
    if (fp == NULL) {
        fprintf(stderr, "Failed to list sysfs path %s: %s\n", command,
                strerror(errno));
        return -1;
    }

    if (fgets(net_dev_name, net_dev_name_len, fp) == NULL) {
        fprintf(stderr, "Failed to get net device name from '%s' for %s: %s\n",
                command, ib_dev_name, strerror(errno));
        fclose(fp);
        return -1;
    }

    // remove the trailing newline character
    net_dev_name[strlen(net_dev_name) - 1] = '\0';

    fclose(fp);
    return 0;
}

struct ibv_context *ib_open_device(const char *dev_name) {
    struct ibv_device **dev_list    = NULL;
    struct ibv_device  *ib_dev      = NULL;
    struct ibv_context *context     = NULL;
    int                 num_devices = 0;

    // get the list of devices
    dev_list = ibv_get_device_list(&num_devices);
    if (!dev_list) {
        fprintf(stderr, "Failed to get IB devices list: %s\n",
                strerror(errno));
        return NULL;
    }
    if (num_devices <= 0) {
        fprintf(stderr, "NO IB device found\n");
        return NULL;
    }

    if (dev_name == NULL || dev_name[0] == '\0') {
        // select the first device
        ib_dev = dev_list[0];
        dev_name = ibv_get_device_name(ib_dev);
    } else {
        // select by device name
        for (ib_dev = *dev_list; ib_dev != NULL; dev_list++) {
            if (!strcmp(ibv_get_device_name(ib_dev), dev_name))
                break;
        }
    }
    if (!ib_dev) {
        fprintf(stderr, "IB devices %s NOT found\n", dev_name ? dev_name : "");
        goto err;
    }

    // open the device
    context = ibv_open_device(ib_dev);
    if (context) {
        char net_dev_name[IF_NAMESIZE] = {'\0'};
        if (__ibdev_2_netdev(dev_name, net_dev_name, IF_NAMESIZE) == 0)
            fprintf(stdout, "[%s at %d]: IB device %s <-> Ethernet device %s "
                    "opened\n", __FILE__, __LINE__, dev_name, net_dev_name);
    }


err:
    ibv_free_device_list(dev_list);
    return context;
}


struct ibv_qp *ib_create_qp(struct ibv_cq *cq, struct ibv_pd *pd) {
    struct ibv_qp_init_attr qp_init_attr;

    memset(&qp_init_attr, 0, sizeof(qp_init_attr));

    qp_init_attr.send_cq            = cq;
    qp_init_attr.recv_cq            = cq;
    qp_init_attr.cap.max_send_wr    = 1;
    qp_init_attr.cap.max_recv_wr    = 1;
    qp_init_attr.cap.max_send_sge   = 1;
    qp_init_attr.cap.max_recv_sge   = 1;
    qp_init_attr.qp_type            = IBV_QPT_RC;

    return ibv_create_qp(pd, &qp_init_attr);
}


int ib_modify_qp_to_init(struct ibv_qp *qp) {
    struct ibv_qp_attr qp_attr;

    memset(&qp_attr, 0, sizeof(qp_attr));

    qp_attr.qp_state        = IBV_QPS_INIT;
    qp_attr.pkey_index      = 0;
    qp_attr.port_num        = IB_PORT;
    qp_attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE |
                              IBV_ACCESS_REMOTE_WRITE;

    return ibv_modify_qp(qp, &qp_attr,
                         IBV_QP_STATE           |
                         IBV_QP_PKEY_INDEX      |
                         IBV_QP_PORT            |
                         IBV_QP_ACCESS_FLAGS);
}

int ib_modify_qp_to_rtr(struct ibv_qp *qp, enum ibv_mtu path_mtu,
                        struct qp_info remote_qp_info) {
    struct ibv_qp_attr qp_attr;

    memset(&qp_attr, 0, sizeof(qp_attr));

    qp_attr.qp_state                    = IBV_QPS_RTR;
    qp_attr.path_mtu                    = path_mtu;
    qp_attr.dest_qp_num                 = remote_qp_info.qp_num;
    qp_attr.rq_psn                      = 0;
    qp_attr.max_dest_rd_atomic          = 1;
    qp_attr.min_rnr_timer               = 12;

    qp_attr.ah_attr.is_global           = 1;
    qp_attr.ah_attr.dlid                = remote_qp_info.lid;
    qp_attr.ah_attr.sl                  = 0;
    qp_attr.ah_attr.src_path_bits       = 0;
    qp_attr.ah_attr.port_num            = IB_PORT;

    qp_attr.ah_attr.grh.flow_label      = 0;
    qp_attr.ah_attr.grh.hop_limit       = 1;
    qp_attr.ah_attr.grh.sgid_index      = IB_GID_INDEX;
    qp_attr.ah_attr.grh.traffic_class   = 0;

    memcpy(&qp_attr.ah_attr.grh.dgid, &remote_qp_info.gid, 16);

    return ibv_modify_qp(qp, &qp_attr,
                         IBV_QP_STATE               |
                         IBV_QP_AV                  |
                         IBV_QP_PATH_MTU            |
                         IBV_QP_DEST_QPN            |
                         IBV_QP_RQ_PSN              |
                         IBV_QP_MAX_DEST_RD_ATOMIC  |
                         IBV_QP_MIN_RNR_TIMER);
}

int ib_modify_qp_to_rts(struct ibv_qp *qp) {
    struct ibv_qp_attr qp_attr;

    memset(&qp_attr, 0, sizeof(qp_attr));

    qp_attr.qp_state        = IBV_QPS_RTS;
    qp_attr.timeout         = 14;
    qp_attr.retry_cnt       = 7;
    qp_attr.rnr_retry       = 7;
    qp_attr.sq_psn          = 0;
    qp_attr.max_rd_atomic   = 1;

    return ibv_modify_qp(qp, &qp_attr,
                         IBV_QP_STATE               |
                         IBV_QP_TIMEOUT             |
                         IBV_QP_RETRY_CNT           |
                         IBV_QP_RNR_RETRY           |
                         IBV_QP_SQ_PSN              |
                         IBV_QP_MAX_QP_RD_ATOMIC);
}

int ib_ctx_xchg_qp_info_as_server(uint16_t listen_port,
                               struct qp_info local_qp_info,
                               struct qp_info *remote_qp_info) {
    int ret = 0;
    int listen_fd, cli_fd;
    struct sockaddr_in svr_addr, cli_addr;
    socklen_t addr_len;
    char ipv4_addr_str[INET_ADDRSTRLEN];

    // create a TCP socket
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        fprintf(stderr, "Failed to create TCP socket: %s\n", strerror(errno));
        return -1;
    }

    // bind the socket to the port
    memset(&svr_addr, 0, sizeof(svr_addr));
    svr_addr.sin_family = AF_INET;
    svr_addr.sin_addr.s_addr = INADDR_ANY;
    svr_addr.sin_port = htons(listen_port);

    ret = bind(listen_fd, (struct sockaddr *)&svr_addr, sizeof(svr_addr));
    if (ret < 0) {
        fprintf(stderr, "Failed to bind socket: %s\n", strerror(errno));
        goto err1;
    }

    // listen for incoming connections
    ret = listen(listen_fd, 1);
    if (ret < 0) {
        fprintf(stderr, "Failed to listen on port %d: %s\n", listen_port,
                strerror(errno));
        goto err1;
    }

    addr_len = sizeof(cli_addr);
    cli_fd = accept(listen_fd, (struct sockaddr *)&cli_addr, &addr_len);
    if (cli_fd < 0) {
        fprintf(stderr, "Failed to accept connection: %s\n", strerror(errno));
        goto err1;
    }

    inet_ntop(AF_INET, &cli_addr.sin_addr.s_addr, ipv4_addr_str,
              INET_ADDRSTRLEN);
    fprintf(stdout, "[%s at %d]: accept connection from %s\n", __FILE__,
            __LINE__, ipv4_addr_str);

    // send local QP info to the client
    ret = write(cli_fd, &local_qp_info, sizeof(local_qp_info));
    if (ret < 0) {
        fprintf(stderr, "Failed to send QP info to %s: %s\n", ipv4_addr_str,
                strerror(errno));
        goto err2;
    }

    fprintf(stdout, "[%s at %d]: send QP info "
            "[LID %#04x QPN %#06x GID %02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:"
            "%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d] to %s\n", __FILE__,
            __LINE__,local_qp_info.lid, local_qp_info.qp_num,
            local_qp_info.gid.raw[0], local_qp_info.gid.raw[1],
            local_qp_info.gid.raw[2], local_qp_info.gid.raw[3],
            local_qp_info.gid.raw[4], local_qp_info.gid.raw[5],
            local_qp_info.gid.raw[6], local_qp_info.gid.raw[7],
            local_qp_info.gid.raw[8], local_qp_info.gid.raw[9],
            local_qp_info.gid.raw[10], local_qp_info.gid.raw[11],
            local_qp_info.gid.raw[12], local_qp_info.gid.raw[13],
            local_qp_info.gid.raw[14], local_qp_info.gid.raw[15],
            ipv4_addr_str);

    // receive remote QP info from the client
    ret = read(cli_fd, remote_qp_info, sizeof(struct qp_info));
    if (ret < 0) {
        fprintf(stderr, "Failed to receive QP info from %s: %s\n",
                ipv4_addr_str, strerror(errno));
        goto err2;
    }

    fprintf(stdout, "[%s at %d]: receive QP info "
            "[LID %#04x QPN %#06x GID %02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:"
            "%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d] from %s\n", __FILE__,
            __LINE__, remote_qp_info->lid, remote_qp_info->qp_num,
            remote_qp_info->gid.raw[0], remote_qp_info->gid.raw[1],
            remote_qp_info->gid.raw[2], remote_qp_info->gid.raw[3],
            remote_qp_info->gid.raw[4], remote_qp_info->gid.raw[5],
            remote_qp_info->gid.raw[6], remote_qp_info->gid.raw[7],
            remote_qp_info->gid.raw[8], remote_qp_info->gid.raw[9],
            remote_qp_info->gid.raw[10], remote_qp_info->gid.raw[11],
            remote_qp_info->gid.raw[12], remote_qp_info->gid.raw[13],
            remote_qp_info->gid.raw[14], remote_qp_info->gid.raw[15],
            ipv4_addr_str);

err2:
    close(cli_fd);
err1:
    close(listen_fd);
    return ret;
}

int ib_ctx_xchg_qp_info_as_client(struct sockaddr_in *svr_addr,
                                  struct qp_info local_qp_info,
                                  struct qp_info *remote_qp_info) {
    int ret = 0;
    int fd;
    char svr_addr_str[INET_ADDRSTRLEN];

    inet_ntop(AF_INET, &(svr_addr->sin_addr.s_addr), svr_addr_str,
              INET_ADDRSTRLEN);

    // create a TCP socket
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "Failed to create TCP socket: %s\n", strerror(errno));
        return -1;
    }

    // connect to the server
    ret = connect(fd, (struct sockaddr *)svr_addr, sizeof(struct sockaddr));
    if (ret < 0) {
        fprintf(stderr, "Connect to %s failed: %s\n", svr_addr_str,
                strerror(errno));
        goto err;
    }

    fprintf(stdout, "[%s at %d]: connect to %s\n", __FILE__, __LINE__,
            svr_addr_str);

    // Send local QP info to the server
    ret = write(fd, &local_qp_info, sizeof(local_qp_info));
    if (ret < 0) {
        fprintf(stderr, "Failed to send QP info to %s: %s\n", svr_addr_str,
                strerror(errno));
        goto err;
    }

    fprintf(stdout, "[%s at %d]: send QP info "
            "[LID %#04x QPN %#06x GID %02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:"
            "%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d] to %s\n", __FILE__,
            __LINE__,local_qp_info.lid, local_qp_info.qp_num,
            local_qp_info.gid.raw[0], local_qp_info.gid.raw[1],
            local_qp_info.gid.raw[2], local_qp_info.gid.raw[3],
            local_qp_info.gid.raw[4], local_qp_info.gid.raw[5],
            local_qp_info.gid.raw[6], local_qp_info.gid.raw[7],
            local_qp_info.gid.raw[8], local_qp_info.gid.raw[9],
            local_qp_info.gid.raw[10], local_qp_info.gid.raw[11],
            local_qp_info.gid.raw[12], local_qp_info.gid.raw[13],
            local_qp_info.gid.raw[14], local_qp_info.gid.raw[15],
            svr_addr_str);

    // Receive remote QP info from the server
    ret = read(fd, remote_qp_info, sizeof(struct qp_info));
    if (ret < 0) {
        fprintf(stderr, "Failed to receive QP info from %s: %s\n",
                svr_addr_str, strerror(errno));
        goto err;
    }

    fprintf(stdout, "[%s at %d]: receive QP info "
            "[LID %#04x QPN %#06x GID %02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d:"
            "%02d:%02d:%02d:%02d:%02d:%02d:%02d:%02d] from %s\n", __FILE__,
            __LINE__, remote_qp_info->lid, remote_qp_info->qp_num,
            remote_qp_info->gid.raw[0], remote_qp_info->gid.raw[1],
            remote_qp_info->gid.raw[2], remote_qp_info->gid.raw[3],
            remote_qp_info->gid.raw[4], remote_qp_info->gid.raw[5],
            remote_qp_info->gid.raw[6], remote_qp_info->gid.raw[7],
            remote_qp_info->gid.raw[8], remote_qp_info->gid.raw[9],
            remote_qp_info->gid.raw[10], remote_qp_info->gid.raw[11],
            remote_qp_info->gid.raw[12], remote_qp_info->gid.raw[13],
            remote_qp_info->gid.raw[14], remote_qp_info->gid.raw[15],
            svr_addr_str);

err:
    close(fd);
    return ret;
}
