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
        memcpy(qp_attr.ah_attr.grh.dgid.raw, remote_qp_info->gid.raw,
               sizeof(union ibv_gid));
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
        .addr   = (uintptr_t)buf,
        .length = req_size,
        .lkey   = lkey
    };

    /*
     * The struct ibv_send_wr describes the Work Request to the Send Queue of
     * the QP, i.e., Send Request (SR).
     *
     * - opcode
     *   - IBV_WR_SEND - The content of the local memory buffers specified in
     *   sg_list is being sent to the remote QP. The sender doesn’t know where
     *   the data will be written in the remote node. A Receive Request will
     *   be consumed from the head of remote QP's Receive Queue and sent data
     *   will be written to the memory buffers which are specified in that
     *   Receive Request. The message size can be [0, 2^31] for RC and UC QPs
     *   and [0, path MTU] for UD QP IBV_WR_SEND_WITH_IMM
     *
     *   - Same as IBV_WR_SEND, and immediate data will be sent in the message.
     *   This value will be available in the Work Completion that will be
     *   generated for the consumed Receive Request in the remote QP
     *
     * - send_flags: describes the properties of the WR. It is either 0 or the
     *   bitwise OR of one or more of the following flags:
     *
     *   - IBV_SEND_FENCE - Set the fence indicator for this WR. This means
     *   that the processing of this WR will be blocked until all prior posted
     *   RDMA Read and Atomic WRs will be completed. Valid only for QPs with
     *   Transport Service Type IBV_QPT_RC
     *
     *   - IBV_SEND_SIGNALED - Set the completion notification indicator for this
     *   WR. This means that if the QP was created with sq_sig_all=0, a Work
     *   Completion will be generated when the processing of this WR will be
     *   ended. If the QP was created with sq_sig_all=1, there won't be any effect
     *   to this flag
     *
     *   - IBV_SEND_SOLICITED - Set the solicited event indicator for this WR.
     *   This means that when the message in this WR will be ended in the remote
     *   QP, a solicited event will be created to it and if in the remote side
     *   the user is waiting for a solicited event, it will be woken up.
     *   Relevant only for the Send and RDMA Write with immediate opcodes
     *
     *   - IBV_SEND_INLINE - The memory buffers specified in sg_list will be placed
     *   inline in the Send Request. This mean that the low-level driver (i.e. CPU)
     *   will read the data and not the RDMA device. This means that the L_Key won't
     *   be checked, actually those memory buffers don't even have to be registered
     *   and they can be reused immediately after ibv_post_send() will be ended.
     *   Valid only for the Send and RDMA Write opcodes
     *
     * - imm_data: (optional) A 32 bits number, in network order, in an SEND or
     *   RDMA WRITE opcodes that is being sent along with the payload to the
     *   remote side and placed in a Receive Work Completion and not in a
     *   remote memory buffer
     *
     */
    struct ibv_send_wr send_wr = {
        .wr_id      = wr_id,
        .sg_list    = &list,
        .num_sge    = 1,
        .opcode     = IBV_WR_SEND_WITH_IMM,
        .send_flags = IBV_SEND_SIGNALED,
        .imm_data   = htonl (imm_data)
    };

    /*
     * ibv_post_send() posts a linked list of Work Requests (WRs) to the Send
     * Queue of a Queue Pair (QP). ibv_post_send() go over all of the entries
     * in the linked list, one by one, check that it is valid, generate an
     * HW-specific Send Request out of it and add it to the tail of the QP's
     * Send Queue without performing any context switch. The RDMA device will
     * handle it (later) in an asynchronous way. If there is a failure in one
     * of the WRs because the Send Queue is full or one of the attributes in
     * the WR is bad, it stops immediately and returns the pointer to that WR.
     * The QP will handle Work Requests in the Send queue according to the
     * following rules:
     *
     * 1. If the QP is in RESET, INIT or RTR state, an immediate error should
     * be returned. However, some low-level drivers may not follow this rule
     * (to eliminate extra check in the data path, thus providing better
     * performance), and posting Send Requests at one or all of those states
     * may be silently ignored.
     * 2. If the QP is in RTS state, Send Requests can be posted,
     * and they will be processed.
     * 3. If the QP is in SQE or ERROR state, Send Requests can be posted,
     * and they will be completed with error.
     * 4. If the QP is in SQD state, Send Requests can be posted, but won't be
     * processed.
     *
     */

    ret = ibv_post_send(qp, &send_wr, &bad_send_wr);
    return ret;
}

int post_recv(uint32_t req_size, uint32_t lkey, uint64_t wr_id,
              struct ibv_qp *qp, char *buf) {
    int ret = 0;
    struct ibv_recv_wr *bad_recv_wr;

    /*
     * struct ibv_sge describes a scatter/gather entry.
     * The memory buffer that this entry describes must be
     * registered until any posted Work Request that uses it
     * isn't considered outstanding anymore.
     * The order in which the RDMA device access the memory
     * in a scatter/gather list isn't defined.
     * This means that if some of the entries overlap the same
     * memory address, the content of this address is undefined.
     *
     */
    struct ibv_sge list = {
        .addr   = (uintptr_t)buf, // The address of the buffer to read from
                                  // or write to
        .length = req_size,       // The length of the buffer in bytes. The
                                  // value 0 is a special value and is equal to 2^31
                                  // bytes (and not zero bytes, as one might imagine)
        .lkey   = lkey            // The Local key of the Memory Region that
                                  // this memory buffer was registered with
    };

    struct ibv_recv_wr recv_wr = {
        .wr_id   = wr_id,
        .sg_list = &list,
        .num_sge = 1
    };

    /*
     * ibv_post_recv() posts a linked list of Work Requests (WRs) to the
     * Receive Queue of a Queue Pair (QP).
     *
     * ibv_post_recv() go over all of the entries in the linked list, one by
     * one, check that it is valid, generate a HW-specific Receive Request out
     * of it and add it to the tail of the QP's Receive Queue without
     * performing any context switch. The RDMA device will take one of those
     * Work Requests as soon as an incoming opcode to that QP will
     * consume a Receive Request (RR). If there is a failure in one of the WRs
     * because the Receive Queue is full or one of the attributes in the
     * WR is bad, it stops immediately and return the pointer to that WR.
     *
     */
    ret = ibv_post_recv(qp, &recv_wr, &bad_recv_wr);
    return ret;
}
