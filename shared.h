#ifndef SHARED_H_
#define SHARED_H_

#include <unistd.h>

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

#endif