/**
 * @brief PSOCK functionality header
 *
 * @file psock.h
 */

#ifndef SRC_PSOCK_PROTO_H_
#define SRC_PSOCK_PROTO_H_

#define PSOCK_KEY                   0x58415054

#define PSOCK_PROTO_CMD_CONNECT     (0x01)
#define PSOCK_PROTO_CMD_SEND        (0x02)
#define PSOCK_PROTO_CMD_RECV        (0x03)
#define PSOCK_PROTO_CMD_DISCONNECT  (0x04)

typedef struct st_psock_min_hdr {

    uint32_t key;
    uint8_t  type;
    uint8_t  sysdep;
    uint16_t subhdr;
    uint32_t cmd;
    uint32_t data_size;

} __attribute__((aligned(1),packed)) t_psock_min_hdr, *pt_psock_min_hdr;

typedef struct st_psock_min_ack {

    uint32_t key;
    uint8_t  type;
    uint8_t  sysdep;
    uint16_t subhdr;
    uint32_t cmd;
    uint32_t ack;
    uint32_t data_size;

} __attribute__((aligned(1),packed)) t_psock_min_ack, *pt_psock_min_ack;


#endif /* SRC_PSOCK_PROTO_H_ */
