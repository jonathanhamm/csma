#ifndef PARSE_H_
#define PARSE_H_

#include <stdlib.h>

typedef struct buf_s buf_s;
typedef struct token_s token_s;
typedef enum tok_types_e tok_types_e;
typedef enum tok_att_s tok_att_s;

enum tok_types_e {
    TOK_TYPE_ID = 0,
    TOK_TYPE_DOT = 1,
    TOK_TYPE_COMMA = 2,
    TOK_TYPE_OPENBRACE = 3,
    TOK_TYPE_CLOSEBRACE = 4,
    TOK_TYPE_OPENPAREN = 5,
    TOK_TYPE_CLOSEPAREN = 6,
    TOK_TYPE_STRING = 7,
    TOK_TYPE_ASSIGNOP = 8,
    TOK_TYPE_NUM = 9
};

enum tok_att_s {
    TOK_ATT_DEFAULT,
    TOK_ATT_INT,
    TOK_ATT_REAL
};

struct buf_s
{
    size_t bsize, size;
    char buf[];
};

struct token_s
{
    tok_types_e type;
    tok_att_s att;
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