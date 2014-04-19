#ifndef PARSE_H_
#define PARSE_H_

#include <stdlib.h>
#include <stdbool.h>
#include "network.h"

#include "shared.h"

#define SYM_TABLE_SIZE 53

typedef enum type_e type_e;

typedef struct object_s object_s;
typedef struct scope_s scope_s;
typedef struct buf_s buf_s;
typedef struct token_s token_s;
typedef enum tok_types_e tok_types_e;
typedef enum tok_att_s tok_att_s;
typedef union sym_data_u sym_data_u;
typedef struct sym_record_s sym_record_s;
typedef struct sym_table_s sym_table_s;
typedef struct arg_s arg_s;
typedef struct arglist_s arglist_s;
typedef struct objlist_s objlist_s;

typedef struct task_s task_s;
typedef struct send_s send_s;

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
    TOK_TYPE_NUM = 9,
    TOK_TYPE_EOF = 10,
    TOK_TYPE_OPENBRACKET = 11,
    TOK_TYPE_CLOSE_BRACKET = 12,
    TOK_TYPE_BOOL
};

enum tok_att_s {
    TOK_ATT_DEFAULT,
    TOK_ATT_INT,
    TOK_ATT_REAL,
    TOK_ATT_EQ,
    TOK_ATT_PLUSEQ,
    TOK_ATT_INF
};

enum type_e
{
    TYPE_INT = 1,
    TYPE_REAL = 2,
    TYPE_INF = 4,
    TYPE_STRING = 8,
    TYPE_NODE = 16,
    TYPE_ARGLIST = 32,
    TYPE_AGGREGATE = 64,
    TYPE_ERROR = 128,
    TYPE_NULL = 256,
    TYPE_ANY = 512,
    TYPE_VOID = 1024
};

struct object_s
{
    token_s *tok;
    scope_s *child;
    type_e type;
    arglist_s *arglist;
    bool islazy;
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
    int lineno;
    bool marked;
    token_s *next;
    token_s *prev;
};

struct arg_s
{
    char *name;
    object_s obj;
    arg_s *next;
};

struct arglist_s
{
    int size;
    arg_s *head;
    arg_s *tail;
};

struct task_s
{
    funcs_e func;
    task_s *next;
};

struct send_s
{
    task_s super;
    char *src;
    char *dst;
    size_t size;
    char *payload;
    char *period;
    bool repeat;
};

union sym_data_u
{
    void *ptr;
    pid_t pid;
};

struct sym_record_s
{
    char *key;
    sym_data_u data;
    sym_record_s *next;
};

struct sym_table_s
{
    sym_record_s *table[SYM_TABLE_SIZE];
};

struct scope_s
{
    char *id;
    int size;
    scope_s *parent;
    sym_table_s table;
    object_s **object;
};

struct objlist_s
{
    object_s *obj;
    objlist_s *next;
};

struct {
    task_s *head;
    task_s *tail;
}
tqueue;

extern sym_table_s station_table;

extern bool parse(char *src);

extern void error(const char *fs, ...);
extern char *readfile(const char *fname);
extern void closefile(void);

extern buf_s *buf_init(void);
extern void buf_addc(buf_s **b, int c);
extern void buf_addstr(buf_s **b, char *str, size_t size);
extern void buf_trim(buf_s **b);
extern void buf_reset(buf_s **b);
extern void buf_free(buf_s *b);

extern void sym_insert(sym_table_s *table, char *key, sym_data_u data);
extern sym_record_s *sym_lookup(sym_table_s *table, char *key);
extern char *sym_get(sym_table_s *table, void *obj);
extern void sym_delete(sym_table_s *table, char *key);

extern void task_enqueue(task_s *t);
extern task_s *task_dequeue(void);

extern object_s net_send(void *);
extern object_s net_node(void *);
extern object_s net_rand(void *);
extern object_s net_size(void *);
extern object_s net_kill(void *);
extern object_s net_print(void *);
extern object_s net_clear(void *);


#endif