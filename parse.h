#ifndef PARSE_H_
#define PARSE_H_

#include <stdlib.h>

typedef struct buf_s buf_s;

struct buf_s
{
    size_t bsize, size;
    char buf[];
};


extern buf_s *buf_init(void);
extern void buf_addc(buf_s **b, int c);
void buf_addstr(buf_s **b, char *str, size_t size);

extern void *alloc(size_t size);
extern void *allocz(size_t size);
extern void *ralloc(void *ptr, size_t size);


#endif