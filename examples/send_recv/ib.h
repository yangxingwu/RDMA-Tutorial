#ifndef __COMMON_H__
#define __COMMON_H__

#include <stdint.h>
#include <infiniband/verbs.h>
#include <arpa/inet.h>

/*
 * IB PORT and GID INDEX
 *
 * refer to https://docs.nvidia.com/deeplearning/nccl/user-guide/docs/env.html#nccl-ib-gid-index
 *
 * the following is the output of command `show_gids`
 *
 * $ show_gids
 * DEV		PORT	INDEX	GID					IPv4  		VER	DEV
 * ---	----	-----	---					------------  	---	---
 * mlx5_0	1	0	fe80:0000:0000:0000:0ac0:ebff:fe3d:ca54			v1	eth05
 * mlx5_0	1	1	fe80:0000:0000:0000:0ac0:ebff:fe3d:ca54			v2	eth05
 * mlx5_0	1	2	0000:0000:0000:0000:0000:ffff:0a6e:0021	10.110.0.33  	v1	eth05
 * mlx5_0	1	3	0000:0000:0000:0000:0000:ffff:0a6e:0021	10.110.0.33  	v2	eth05
 * mlx5_1	1	0	fe80:0000:0000:0000:0e42:a1ff:fea4:94a8			v1	eth02
 * mlx5_1	1	1	fe80:0000:0000:0000:0e42:a1ff:fea4:94a8			v2	eth02
 * mlx5_1	1	2	0000:0000:0000:0000:0000:ffff:1a6e:b11d	26.110.177.29  	v1	eth02
 * mlx5_1	1	3	0000:0000:0000:0000:0000:ffff:1a6e:b11d	26.110.177.29  	v2	eth02
 * mlx5_2	1	0	fe80:0000:0000:0000:0e42:a1ff:fea4:94a9			v1	eth03
 * mlx5_2	1	1	fe80:0000:0000:0000:0e42:a1ff:fea4:94a9			v2	eth03
 * mlx5_2	1	2	0000:0000:0000:0000:0000:ffff:1a6e:b21d	26.110.178.29  	v1	eth03
 * mlx5_2	1	3	0000:0000:0000:0000:0000:ffff:1a6e:b21d	26.110.178.29  	v2	eth03
 * n_gids_found=12
 *
 * PORT `1` -- check PORT column
 * INDEX `3` means a RoCE V2 device based on IPv4 -- check INDEX column
 *
 */
#define IB_PORT             1
#define IB_GID_INDEX        3

#define MSG_SIZE            4096
#define TCP_PORT            12345

#define GID_LEN             16

struct qp_info {
    uint32_t        qp_num;
    uint16_t        lid;
    union ibv_gid   gid;
};

void print_gid(union ibv_gid gid);

struct ibv_context *ib_open_device(const char *dev_name);
struct ibv_qp *ib_create_qp(struct ibv_cq *cq, struct ibv_pd *pd);
int ib_modify_qp_to_init(struct ibv_qp *qp);
int ib_modify_qp_to_rtr(struct ibv_qp *qp, enum ibv_mtu path_mtu,
                        struct qp_info remote_qp_info);
int ib_modify_qp_to_rts(struct ibv_qp *qp);

int ib_ctx_xchg_qp_info_as_server(struct qp_info local_qp_info,
                                  struct qp_info *remote_qp_info);
int ib_ctx_xchg_qp_info_as_client(struct sockaddr_in *svr_addr,
                               struct qp_info local_qp_info,
                               struct qp_info *remote_qp_info);

int ib_post_send(const char *send_buf, uint32_t send_buf_size, uint32_t lkey,
                 uint64_t wr_id, struct ibv_qp *qp);
int ib_post_recv(const char *recv_buf, uint32_t recv_buf_size, uint32_t lkey,
                 uint64_t wr_id, struct ibv_qp *qp);

#endif
