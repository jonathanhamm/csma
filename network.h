#ifndef NETWORK_H_
#define NETWORK_H_

#include <stdlib.h>

typedef struct node_s node_s;
typedef struct network_s network_s;
typedef struct schedule_s schedule_s;


struct node_s
{
    char name[32];
    
    node_s *next;
};

struct network_s
{
    int nnodes;
    node_s *nodes;
};

struct schedule_s
{
    
};

#endif