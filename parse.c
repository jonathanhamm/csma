#include "parse.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>

#define INIT_BUF_SIZE 256
#define SYM_TABLE_SIZE 97

#define next_tok() (tokcurr = tokcurr->next)
#define tok() (tokcurr)

typedef struct sym_record_s sym_record_s;
typedef struct sym_table_s sym_table_s;

struct sym_record_s
{
    int att;
    char *string;
    sym_record_s *next;
};

struct sym_table_s
{
    sym_record_s *table[SYM_TABLE_SIZE];
};

static token_s *head;
static token_s *tokcurr;
static token_s *tail;

static sym_table_s symtable;

static char *source;

static char *functions[] = {
    
};

static void readfile(const char *name);
static void lex(const char *name);
static void add_token(char *lexeme, tok_types_e type, tok_att_s att);
static void print_tokens(void);


static bool ident_add(char *key, int att);
static sym_record_s *ident_lookup(char *key);
static uint16_t hash_pjw(char *key);

static void parse_statement(void);
static void parse_id(void);
static void parse_idsuffix(void);
static void parse_idfollow(void);
static void parse_optfollow(void);
static void parse_assignment(void);
static void parse_expression(void);
static void parse_aggregate(void);
static void parse_aggregate_list(void);


static char *strclone(char *str);

void parse(const char *file)
{
    lex(file);
}

void lex(const char *name)
{
    int idcounter = 1;
    sym_record_s *rec;
    char *bptr, *fptr, c;
    
    readfile(name);
    bptr = fptr = source;
    
    while(*fptr) {
        switch(*fptr) {
            case ' ':
            case '\n':
            case '\t':
            case '\v':
                fptr++;
                break;
            case '.':
                add_token(".", TOK_TYPE_DOT, TOK_ATT_DEFAULT);
                fptr++;
                break;
            case ',':
                add_token(",", TOK_TYPE_COMMA, TOK_ATT_DEFAULT);
                fptr++;
                break;
            case '{':
                add_token("{", TOK_TYPE_OPENBRACE, TOK_ATT_DEFAULT);
                fptr++;
                break;
            case '}':
                add_token("}", TOK_TYPE_CLOSEBRACE, TOK_ATT_DEFAULT);
                fptr++;
                break;
            case ')':
                add_token(")", TOK_TYPE_OPENPAREN, TOK_ATT_DEFAULT);
                fptr++;
                break;
            case '(':
                add_token("(", TOK_TYPE_CLOSEPAREN, TOK_ATT_DEFAULT);
                fptr++;
                break;
            case '"':
                bptr = fptr;
                while(*++fptr != '"') {
                    if(!*fptr)
                        fprintf(stderr, "Improperly closed double quote\n");
                }
                c = *++fptr;
                *fptr = '\0';
                add_token(bptr, TOK_TYPE_STRING, TOK_ATT_DEFAULT);
                bptr = fptr;
                *fptr = c;
                break;
            case '=':
                add_token("=", TOK_TYPE_ASSIGNOP, TOK_ATT_EQ);
                fptr++;
                break;
            case '+':
                if(*++fptr == '=') {
                    add_token("+=", TOK_TYPE_ASSIGNOP, TOK_ATT_PLUSEQ);
                    fptr++;
                }
                else {
                    fprintf(stderr, "Stray '+'");
                }
                break;
            default:
                bptr = fptr;
                if(isalpha(*fptr)) {
                    while(isalnum(*++fptr));
                    c = *fptr;
                    *fptr = '\0';
                    rec = ident_lookup(bptr);
                    if(rec)
                        add_token(bptr, TOK_TYPE_ID, rec->att);
                    else
                        ident_add(strclone(bptr), idcounter++);
                    *fptr = c;
                }
                else if(isdigit(*fptr)) {
                    while(isdigit(*++fptr));
                    if(*fptr == '.') {
                        while(isdigit(*++fptr));
                        c = *fptr;
                        *fptr = '\0';
                        add_token(bptr, TOK_TYPE_NUM, TOK_ATT_REAL);
                        *fptr = c;
                    }
                    else {
                        c = *fptr;
                        *fptr = '\0';
                        add_token(bptr, TOK_TYPE_NUM, TOK_ATT_INT);
                        *fptr = c;
                    }
                }
                break;
        }
    }
    add_token("EOF", TOK_TYPE_EOF, TOK_ATT_DEFAULT);
    print_tokens();
}

void add_token(char *lexeme, tok_types_e type, tok_att_s att)
{
    token_s *t = alloc(sizeof(*t));
    
    t->type = type;
    t->att = att;
    t->lexeme = strclone(lexeme);
    strcpy(t->lexeme, lexeme);
    t->next = NULL;
    
    if(head) {
        t->prev = tail;
        tail->next = t;
    }
    else {
        t->prev = NULL;
        head = t;
    }
    tail = t;
}

void print_tokens(void)
{
    token_s *t;
    
    for(t = head; t; t = t->next)
        printf("%s %d\n", t->lexeme, t->type);
}

void parse_statement(void)
{
    parse_id();
    parse_idfollow();
}

void parse_id(void)
{
    if(tok()->type == TOK_TYPE_ID) {
        next_tok();
        parse_idsuffix();
    }
    else {
        fprintf(stderr, "Syntax Error: Expected identifier but got %s\n", tokcurr->lexeme);
        next_tok();
    }
}

void parse_idsuffix(void)
{
    switch(tok()->type) {
        case TOK_TYPE_DOT:
            if(next_tok()->type == TOK_TYPE_ID) {
                next_tok();
            }
            else {
                fprintf(stderr, "Syntax Error: Expected identifier, but got %s\n", tok()->lexeme);
                next_tok();
            }
            break;
        case TOK_TYPE_OPENBRACE:
        case TOK_TYPE_CLOSEBRACE:
        case TOK_TYPE_STRING:
        case TOK_TYPE_NUM:
        case TOK_TYPE_ASSIGNOP:
        case TOK_TYPE_OPENPAREN:
        case TOK_TYPE_CLOSEPAREN:
        case TOK_TYPE_ID:
        case TOK_TYPE_EOF:
            break;
        default:
            fprintf(stderr, "Syntax Error: Expected . { } string number = += ) ( identifier or EOF but got %s\n", tok()->lexeme);
            next_tok();
            break;
    }
}

void parse_idfollow(void)
{
    switch(tok()->type) {
        case TOK_TYPE_OPENPAREN:
            parse_aggregate_list();
            if(tok()->type == TOK_TYPE_CLOSEPAREN) {
                next_tok();
            }
            else {
                fprintf(stderr, "Syntax Error: Exprected ) but got %s\n", tok()->lexeme);
                next_tok();
            }
            break;
        case TOK_TYPE_ASSIGNOP:
            parse_assignment();
            break;
        default:
            fprintf(stderr, "Syntax Error: Expected ( = or += but got: %s\n", tok()->lexeme);
            next_tok();
            break;
    }
}

void parse_optfollow(void)
{
    switch(tok()->type) {
        case TOK_TYPE_ASSIGNOP:
        case TOK_TYPE_OPENPAREN:
            parse_idfollow();
            break;
        case TOK_TYPE_CLOSEBRACE:
        case TOK_TYPE_OPENBRACE:
        case TOK_TYPE_STRING:
        case TOK_TYPE_NUM:
        case TOK_TYPE_CLOSEPAREN:
        case TOK_TYPE_ID:
        case TOK_TYPE_EOF:
            break;
        default:
            fprintf(stderr, "Syntax Error: Expected = += ( } { string number ) id or EOF but got %s\n", tok()->lexeme);
            next_tok();
            break;
    }
}

void parse_assignment(void)
{
    
}

void parse_expression(void)
{
    
}

void parse_aggregate(void)
{
    
}

void parse_aggregate_list(void)
{
    
}

bool ident_add(char *key, int att)
{
    sym_record_s *rec = symtable.table[hash_pjw(key)];
    
    while(rec->next) {
        if(!strcmp(rec->string, key))
            return false;
        rec = rec->next;
    }
    if(!strcmp(rec->string, key))
        return false;
    rec->next = alloc(sizeof(*rec));
    rec = rec->next;
    rec->string = key;
    rec->att = att;
    rec->next = NULL;
    return true;
}

sym_record_s *ident_lookup(char *key)
{
    sym_record_s *rec = symtable.table[hash_pjw(key)];
    
    while(rec) {
        if(!strcmp(rec->string, key))
            return rec;
        rec = rec->next;
    }
    return NULL;
}

uint16_t hash_pjw(char *key)
{
    uint32_t h = 0, g;
    
    while(*key) {
        h = (h << 4) + *key++;
        if((g = h & (uint32_t)0xf0000000))
            h = (h ^ (g >> 24)) ^ g;
    }
    return (uint16_t)(h % SYM_TABLE_SIZE);
}

void readfile(const char *name)
{
    int fd, status;
    struct stat stats;
    
    fd = open(name, O_RDWR);
    if(fd < 0) {
        perror("Failed to open file");
        exit(EXIT_FAILURE);
    }
    
    status = fstat(fd, &stats);
    if(status < 0) {
        perror("Failed to obtain file info");
        exit(EXIT_FAILURE);
    }
    source = mmap(0, stats.st_size, PROT_WRITE, MAP_PRIVATE, fd, 0);
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

void buf_trim(buf_s **b)
{
    (*b)->bsize = (*b)->size;
    *b = ralloc(*b, (*b)->size);
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

char *strclone(char *str)
{
    char *clone = alloc(strlen(str)+1);
    
    strcpy(clone, str);
    return clone;
}

