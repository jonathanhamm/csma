#include "shared.h"
#include <stdio.h>

bool addr_cmp(char *addr1, char *addr2)
{
    uint32_t *a32 = (uint32_t *)addr1,
             *b32 = (uint32_t *)addr2;
    uint16_t *a16 = (uint16_t *)(a32 + 1);
    uint16_t *b16 = (uint16_t *)(b32 + 1);
    
    return (*a32 == *b32) && (*a16 == *b16);
}


void *alloc(size_t size)
{
    void *ptr = malloc(size);
    if(!ptr){
        perror("Memory Allocation Error");
        exit(EXIT_FAILURE);
    }
    return ptr;
}

void *allocz(size_t size)
{
    void *ptr = calloc(size, 1);
    if(!ptr){
        perror("Memory Allocation Error");
        exit(EXIT_FAILURE);
    }
    return ptr;
}

void *ralloc(void *ptr, size_t size)
{
    ptr = realloc(ptr, size);
    if(!ptr){
        perror("Memory Allocation Error");
        exit(EXIT_FAILURE);
    }
    return ptr;
}