#ifndef __IB_H__
#define __IB_H__

#include <inttypes.h>
#include <sys/types.h>
#include <endian.h>
#include <byteswap.h>
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
 * DEV	PORT	INDEX	GID					IPv4  		VER	DEV
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
#define IB_SL               0
#define IB_WR_ID_STOP       0xE000000000000000
#define NUM_WARMING_UP_OPS  500000
#define TOT_NUM_OPS         5000000

#if __BYTE_ORDER == __LITTLE_ENDIAN
static inline uint64_t htonll (uint64_t x) {return bswap_64(x); }
static inline uint64_t ntohll (uint64_t x) {return bswap_64(x); }
#elif __BYTE_ORDER == __BIG_ENDIAN
static inline uint64_t htonll (uint64_t x) {return x; }
static inline uint64_t ntohll (uint64_t x) {return x; }
#else
#error __BYTE_ORDER is neither __LITTLE_ENDIAN nor __BIG_ENDIAN
#endif

struct QPInfo {
    uint16_t lid;
    uint32_t qp_num;
    union ibv_gid gid;
}__attribute__ ((packed));

enum MsgType {
    MSG_CTL_START = 0,
    MSG_CTL_STOP,
    MSG_REGULAR,
};

int modify_qp_to_rts(struct ibv_qp *qp, uint32_t qp_num, uint16_t lid);

int post_send(uint32_t req_size, uint32_t lkey, uint64_t wr_id,
              uint32_t imm_data, struct ibv_qp *qp, char *buf);

int post_recv(uint32_t req_size, uint32_t lkey, uint64_t wr_id,
              struct ibv_qp *qp, char *buf);


#endif /* __IB_H__ */
