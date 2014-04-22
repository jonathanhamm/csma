#ifndef SHARED_H_
#define SHARED_H_

#include <stdint.h>
#include <stdlib.h>

#define BPS 10000000
#define SHM_KEY 0xDEADBEAF
#define CRC_POLYNOMIAL 0x11EDC6F41

#define RTS_SIZE 20
#define CTS_ACK_SIZE 14
#define RTS_SUBTYPE 0x0b00

typedef enum funcs_e funcs_e;
typedef struct rts_s rts_s;
typedef struct cts_ack_s cts_ack_s;

enum funcs_e {
    FNET_SEND,
    FNET_NODE,
    FNET_RAND,
    FNET_SIZE,
    FNET_KILL,
    FNET_PRINT
};

struct rts_s
{
    uint16_t FC;
    uint16_t D;
    uint8_t addr1[6];
    uint8_t addr2[6];
    uint32_t FCS;
};

struct cts_ack_s
{
    uint16_t FC;
    uint16_t D;
    uint8_t addr1[6];
    uint32_t FCS;
};

extern void *alloc(size_t size);
extern void *allocz(size_t size);
extern void *ralloc(void *ptr, size_t size);

#endif