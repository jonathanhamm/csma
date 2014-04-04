#ifndef PARSE_H_
#define PARSE_H_

#include <stdlib.h>

typedef struct buf_s buf_s;
typedef struct token_s token_s;
typedef enum tok_types_e tok_types_e;

enum tok_types_e {
    TOK_TYPE_ID = 0,
    TOK_TYPE_DOT = 1,
    TOKE_TYPE_COMMA = 2
};

struct buf_s
{
    size_t bsize, size;
    char buf[];
};

struct token_s
{
    tok_types_e type;
    char *lexeme;
    token_s *next;
    token_s *prev;
};

extern void parse(const char *file);

extern buf_s *buf_init(void);
extern void buf_addc(buf_s **b, int c);
extern void buf_addstr(buf_s **b, char *str, size_t size);
extern void buf_trim(buf_s **b);

extern void *alloc(size_t size);
extern void *allocz(size_t size);
extern void *ralloc(void *ptr, size_t size);


#endif