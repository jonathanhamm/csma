#ifndef SHARED_H_
#define SHARED_H_

#include <unistd.h>

#define SEM_NAME "CSMA_LINK"

typedef enum funcs_e funcs_e;
typedef struct frame_s frame_s;

enum funcs_e {
    FNET_SEND,
    FNET_NODE,
    FNET_RAND,
    FNET_SIZE,
    FNET_KILL,
    FNET_PRINT
};

struct frame_s
{
    int size;
    int type;
};

extern void *alloc(size_t size);
extern void *allocz(size_t size);
extern void *ralloc(void *ptr, size_t size);

#endif