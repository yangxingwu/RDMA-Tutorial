#include <stdio.h>
#include "ib.h"

void print_gid(union ibv_gid gid) {
    for (int i = 0; i < GID_LEN; i++) {
        printf("%02d", gid.raw[i]);
        if (i < (GID_LEN - 1))
            printf(":");
    }
    printf("\n");
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
