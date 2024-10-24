#include <arpa/inet.h>
#include <unistd.h>

#include "ib.h"
#include "debug.h"
#include "setup_ib.h"

struct IBRes ib_res;

static int __modify_qp_to_init(struct ibv_qp *qp) {
    struct ibv_qp_attr qp_attr;

    memset(&qp_attr, 0, sizeof(struct ibv_qp_attr));

    qp_attr.qp_state = IBV_QPS_INIT;
    qp_attr.pkey_index = 0;
    qp_attr.port_num = IB_PORT;
    qp_attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
        IBV_ACCESS_REMOTE_ATOMIC | IBV_ACCESS_REMOTE_WRITE;

    return ibv_modify_qp(qp, &qp_attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX |
                         IBV_QP_PORT | IBV_QP_ACCESS_FLAGS);
}

static int __modify_qp_to_rtr(struct ibv_qp *qp, uint8_t link_layer,
                              struct QPInfo *remote_qp_info) {
    struct ibv_qp_attr qp_attr;

    memset(&qp_attr, 0, sizeof(struct ibv_qp_attr));

    qp_attr.qp_state = IBV_QPS_RTR;
    qp_attr.path_mtu = ib_res.port_attr.active_mtu;
    qp_attr.dest_qp_num = remote_qp_info->qp_num;
    qp_attr.rq_psn = 0;
    qp_attr.max_dest_rd_atomic = 1;
    qp_attr.min_rnr_timer = 12;

    if (link_layer == IBV_LINK_LAYER_ETHERNET) {
        qp_attr.ah_attr.is_global = 1;
        memcpy(qp_attr.ah_attr.grh.dgid.raw, remote_qp_info->gid.raw, sizeof(union ibv_gid));
        qp_attr.ah_attr.grh.flow_label = 0;
        qp_attr.ah_attr.grh.sgid_index = IB_GID_INDEX;
        qp_attr.ah_attr.grh.hop_limit = 255;
        qp_attr.ah_attr.grh.traffic_class = 0;
    } else {
        qp_attr.ah_attr.is_global = 0;
        qp_attr.ah_attr.dlid = remote_qp_info->lid;
    }

    qp_attr.ah_attr.sl = IB_SL;
    qp_attr.ah_attr.src_path_bits = 0;
    qp_attr.ah_attr.port_num = IB_PORT;

    return ibv_modify_qp(qp, &qp_attr, IBV_QP_STATE | IBV_QP_AV |
                         IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
                         IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER);
}

static int __modify_qp_to_rts(struct ibv_qp *qp) {
    struct ibv_qp_attr qp_attr;

    memset(&qp_attr, 0, sizeof(struct ibv_qp_attr));

    qp_attr.qp_state = IBV_QPS_RTS;
    qp_attr.timeout = 14;
    qp_attr.retry_cnt = 7;
    qp_attr.rnr_retry = 7;
    qp_attr.sq_psn = 0;
    qp_attr.max_rd_atomic = 1;

    return ibv_modify_qp(qp, &qp_attr, IBV_QP_STATE | IBV_QP_TIMEOUT |
                         IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY |
                         IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC);
}

int modify_qp_to_rts(struct ibv_qp *qp, struct QPInfo *remote_qp_info) {
    /* change QP state to INIT */
    check(__modify_qp_to_init(qp) == 0, "Failed to modify qp to INIT.");

    /* Change QP state to RTR */
    check(__modify_qp_to_rtr(qp, ib_res.port_attr.link_layer, remote_qp_info)
          == 0, "Failed to change qp to rtr.");

    /* Change QP state to RTS */
    check(__modify_qp_to_rts(qp) == 0, "Failed to modify qp to RTS.");

    return 0;
error:
    return -1;
}

int post_send(uint32_t req_size, uint32_t lkey, uint64_t wr_id,
              uint32_t imm_data, struct ibv_qp *qp, char *buf) {
    int ret = 0;
    struct ibv_send_wr *bad_send_wr;

    struct ibv_sge list = {
        .addr   = (uintptr_t) buf,
        .length = req_size,
        .lkey   = lkey
    };

    struct ibv_send_wr send_wr = {
        .wr_id      = wr_id,
        .sg_list    = &list,
        .num_sge    = 1,
        .opcode     = IBV_WR_SEND_WITH_IMM,
        .send_flags = IBV_SEND_SIGNALED,
        .imm_data   = htonl (imm_data)
    };

    ret = ibv_post_send(qp, &send_wr, &bad_send_wr);
    return ret;
}

int post_recv(uint32_t req_size, uint32_t lkey, uint64_t wr_id,
              struct ibv_qp *qp, char *buf) {
    int ret = 0;
    struct ibv_recv_wr *bad_recv_wr;

    struct ibv_sge list = {
        .addr   = (uintptr_t) buf,
        .length = req_size,
        .lkey   = lkey
    };

    struct ibv_recv_wr recv_wr = {
        .wr_id   = wr_id,
        .sg_list = &list,
        .num_sge = 1
    };

    ret = ibv_post_recv(qp, &recv_wr, &bad_recv_wr);
    return ret;
}
