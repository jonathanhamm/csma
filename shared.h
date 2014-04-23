#ifndef SHARED_H_
#define SHARED_H_

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>

#define BPS 10000000
#define SHM_KEY 0xDEADBEAF
#define CRC_POLYNOMIAL 0x11EDC6F41

#define WAIT_TIME 0.5
#define TIME_SLOT 20
#define RTS_SIZE 20
#define CTS_ACK_SIZE 14
#define RTS_SUBTYPE 0x0b00
#define CTS_SUBTYPE 0x0c00
#define ACK_SUBTYPE 0x0d00

typedef enum funcs_e funcs_e;
typedef struct rts_s rts_s;
typedef struct cts_ack_s cts_ack_s;
typedef struct frame_s frame_s;
typedef struct timerarg_s timerarg_s;

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
    char addr1[6];
    char addr2[6];
    uint32_t FCS;
};

struct cts_ack_s
{
    uint16_t FC;
    uint16_t D;
    char addr1[6];
    uint32_t FCS;
};

struct frame_s
{
    size_t size;
    uint32_t FCS;
    char payload[];
};

struct timerarg_s
{
    double time;
    pthread_t sender;
};

extern FILE *logfile;
extern char *name;
extern char *name_stripped;
extern size_t name_len;
extern volatile sig_atomic_t timed_out;
extern int medium[2];

extern ssize_t slowread(void *buf, size_t size);
extern void slowwrite(void *data, size_t size);

extern bool addr_cmp(char *addr1, char *addr2);
extern void start_timer(double time);
extern void logevent(char *fs, ...);
extern void sigALARM(int sig);
extern void *alloc(size_t size);
extern void *allocz(size_t size);
extern void *ralloc(void *ptr, size_t size);

#endif