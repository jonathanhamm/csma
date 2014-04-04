#include "parse.h"
#include <stdio.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>


#define INIT_BUF_SIZE 256


static char *func_send2[][2] = {
    {"dest", NULL}
    
};

static const char *source;

static void readfile(const char *name);
static void lex(const char *name);

void parse(const char *file)
{
    lex(file);
}

void lex(const char *name)
{
    const char *bptr = source;
    readfile(name);
    
    while(*bptr) {
        
    }
    
}

void readfile(const char *name)
{
    int fd, status;
    struct stat stats;
    
    fd = open(name, O_RDONLY);
    if(fd < 0) {
        perror("Failed to open file");
        exit(EXIT_FAILURE);
    }
    
    status = fstat(fd, &stats);
    if(status < 0) {
        perror("Failed to obtain file info");
        exit(EXIT_FAILURE);
    }
    source = mmap(0, stats.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if(source == MAP_FAILED) {
        perror("Failed to read file");
        exit(EXIT_FAILURE);
    }
}

buf_s *buf_init(void)
{
    buf_s *b = malloc(sizeof(*b) + INIT_BUF_SIZE);
    b->bsize = INIT_BUF_SIZE;
    b->size = 0;
    return b;
}

void buf_addc(buf_s **b, int c)
{
    register buf_s *bb = *b;
    
    if(bb->size == bb->bsize) {
        bb->bsize *= 2;
        bb = *b = ralloc(b, sizeof(*bb) + bb->bsize);
    }
    bb->buf[bb->size++] = c;
}

void buf_addstr(buf_s **b, char *str, size_t size)
{
    register buf_s *bb = *b;
    size_t old = bb->size;
    
    bb->size += size;
    if(bb->size >= bb->size) {
        do {
            bb->bsize *= 2;
        }
        while(bb->size >= bb->size);
        bb = *b = ralloc(b, sizeof(*bb) + bb->bsize);
    }
    strcpy(&bb->buf[old], str);
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
