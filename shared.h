#ifndef SHARED_H_
#define SHARED_H_

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

#include <signal.h>
#include <pthread.h>

#define SHM_KEY_C 0xDEADBEAC
#define SHM_KEY_S 0xDEADBEA5
#define CRC_POLYNOMIAL 0x11EDC6F41

#define MEDIUM_SIZE 2048
#define WAIT_TIME 2.0
#define TIME_SLOT 10
#define RTS_SIZE 1000
#define CTS_ACK_SIZE 14
#define RTS_SUBTYPE 0x0b00
#define CTS_SUBTYPE 0x0c00
#define ACK_SUBTYPE 0x0d00

typedef enum funcs_e funcs_e;
typedef struct rts_s rts_s;
typedef struct cts_ack_s cts_ack_s;
typedef struct frame_s frame_s;
typedef struct timerarg_s timerarg_s;
typedef struct medium_s medium_s;

enum funcs_e {
    FNET_SEND,
    FNET_NODE,
    FNET_RAND,
    FNET_SIZE,
    FNET_KILL,
    FNET_PRINT,
    FNET_RECEIVE
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

struct medium_s
{
    bool isbusy;
    size_t size;
    char buf[MEDIUM_SIZE];
};

extern FILE *logfile;
extern char *name;
extern char *name_stripped;
extern size_t name_len;
extern medium_s *mediums;
extern medium_s *mediumc;

extern ssize_t slowread(medium_s *medium, void *buf, size_t size);
extern void slowwrite(medium_s *medium, void *data, size_t size);
extern pthread_t timer_thread;

extern size_t write_shm(medium_s *medium, char *data, size_t size);
extern size_t read_shm(medium_s *medium, char *data, size_t start, size_t size);

extern void set_busy(medium_s *medium, bool isbusy);

extern bool addr_cmp(char *addr1, char *addr2);
extern void start_timer(double time);
extern void logevent(char *fs, ...);
extern void sigALARM(int sig);
extern void *alloc(size_t size);
extern void *allocz(size_t size);
extern void *ralloc(void *ptr, size_t size);

#endif