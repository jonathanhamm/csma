/* Parser for Reading Network File */
#include "parse.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

#define INIT_BUF_SIZE 256
#define SYM_TABLE_SIZE 53

#define next_tok() (tokcurr = tokcurr->next)
#define tok() (tokcurr)

typedef enum type_e type_e;

typedef struct sym_record_s sym_record_s;
typedef struct sym_table_s sym_table_s;
typedef struct object_s object_s;
typedef struct exp_s exp_s;
typedef struct aggnode_s aggnode_s;
typedef struct aggregate_s aggregate_s;
typedef struct scope_s scope_s;
typedef struct access_list_s access_list_s;

typedef struct params_s params_s;
typedef struct function_s function_s;


enum type_e
{
    TYPE_INT,
    TYPE_REAL,
    TYPE_STRING,
    TYPE_AGGREGATE,
    TYPE_NODE
};

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

struct object_s
{
    union {
        token_s *tok;
        aggregate_s *agg;
    };
    type_e type;
    bool islazy;
};

struct exp_s
{
    char *name;
    object_s obj;
};

struct aggnode_s
{
    exp_s exp;
    aggnode_s *next;
};

struct aggregate_s
{
    int size;
    aggnode_s *head;
    aggnode_s *tail;
};

struct scope_s
{
    sym_table_s table;
    char *ident;
    scope_s *parent;
    scope_s **children;
    object_s object;
    int nchildren;
};

struct access_list_s
{
    bool isindex;
    int index;
    char *name;
    bool islazy;
    access_list_s *next;
};

struct params_s
{
    char *name;
    type_e type;
};

struct function_s
{
    char name[16];
    bool isLazy;
    int nargs;
    struct {
        bool is_object_func;
        type_e acts_on;
    }
    object;
    params_s paramlist;
};

static token_s *head;
static token_s *tokcurr;
static token_s *tail;

static sym_table_s symtable;

static scope_s *global;

static char *source;
static int source_fd;
static struct stat fstats;

static function_s funcs[] = {
    {"node", false, -1, {false, 1}, {"node", TYPE_STRING}}
};


static char *functions[] = {
    "rand"
};

static void readfile(const char *name);
static void lex(const char *name);
static void add_token(char *lexeme, tok_types_e type, tok_att_s att, int lineno);
static void print_tokens(void);


static bool ident_add(char *key, int att);
static sym_record_s *ident_lookup(char *key);
static uint16_t hash_pjw(char *key);

static void parse_statement(void);
static access_list_s *parse_id(void);
static void parse_idsuffix(access_list_s **acc);
static void parse_index(access_list_s **acc);
static void parse_idfollow(access_list_s *acc);
static void parse_optfollow(access_list_s *acc);
static object_s parse_assignment(void);
static exp_s parse_expression(void);
static aggregate_s *parse_aggregate(void);
static aggregate_s *parse_aggregate_list(void);
static void parse_aggregate_list_(aggregate_s *agg);

static scope_s *make_scope(scope_s *parent, char *ident);

static void print_accesslist(access_list_s *list);

static char *strclone(char *str);

void parse(const char *file)
{
    lex(file);
    tokcurr = head;
    global = make_scope(NULL, "root");
    parse_statement();
}

void lex(const char *name)
{
    int idcounter = 1, lineno = 1;;
    sym_record_s *rec;
    char *bptr, *fptr, c;
    
    readfile(name);
    bptr = fptr = source;
    
    if(*fptr == '#') {
        while(*fptr && *fptr != '\n')
            fptr++;
    }
    while(*fptr) {
        switch(*fptr) {
            case '\n':
                lineno++;
                fptr++;
                if(*fptr == '#') {
                    while(*fptr && *fptr != '\n')
                        fptr++;
                }
                break;
            case ' ':
            case '\t':
            case '\v':
                fptr++;
                break;
            case '.':
                add_token(".", TOK_TYPE_DOT, TOK_ATT_DEFAULT, lineno);
                fptr++;
                break;
            case ',':
                add_token(",", TOK_TYPE_COMMA, TOK_ATT_DEFAULT, lineno);
                fptr++;
                break;
            case '{':
                add_token("{", TOK_TYPE_OPENBRACE, TOK_ATT_DEFAULT, lineno);
                fptr++;
                break;
            case '}':
                add_token("}", TOK_TYPE_CLOSEBRACE, TOK_ATT_DEFAULT, lineno);
                fptr++;
                break;
            case ')':
                add_token(")", TOK_TYPE_CLOSEPAREN, TOK_ATT_DEFAULT, lineno);
                fptr++;
                break;
            case '(':
                add_token("(", TOK_TYPE_OPENPAREN, TOK_ATT_DEFAULT, lineno);
                fptr++;
                break;
            case '[':
                add_token("[", TOK_TYPE_OPENBRACKET, TOK_ATT_DEFAULT, lineno);
                fptr++;
                break;
            case ']':
                add_token("]", TOK_TYPE_CLOSE_BRACKET, TOK_ATT_DEFAULT, lineno);
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
                add_token(bptr, TOK_TYPE_STRING, TOK_ATT_DEFAULT, lineno);
                bptr = fptr;
                *fptr = c;
                break;
            case '=':
                add_token("=", TOK_TYPE_ASSIGNOP, TOK_ATT_EQ, lineno);
                fptr++;
                break;
            case '+':
                if(*++fptr == '=') {
                    add_token("+=", TOK_TYPE_ASSIGNOP, TOK_ATT_PLUSEQ, lineno);
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
                        add_token(bptr, TOK_TYPE_ID, rec->att, lineno);
                    else {
                        if(!strcmp(bptr, "inf"))
                            add_token(bptr, TOK_TYPE_NUM, TOK_ATT_INF, lineno);
                        else {
                            ident_add(strclone(bptr), idcounter);
                            add_token(bptr, TOK_TYPE_ID, idcounter, lineno);
                            idcounter++;
                        }
                    }
                    *fptr = c;
                }
                else if(isdigit(*fptr)) {
                    while(isdigit(*++fptr));
                    if(*fptr == '.') {
                        while(isdigit(*++fptr));
                        c = *fptr;
                        *fptr = '\0';
                        add_token(bptr, TOK_TYPE_NUM, TOK_ATT_REAL, lineno);
                        *fptr = c;
                    }
                    else {
                        c = *fptr;
                        *fptr = '\0';
                        add_token(bptr, TOK_TYPE_NUM, TOK_ATT_INT, lineno);
                        *fptr = c;
                    }
                }
                else {
                    fprintf(stderr, "Lexical Error at line %d: Unknown symbol %c\n", lineno, *fptr);
                    fptr++;
                }
                break;
        }
    }
    add_token("EOF", TOK_TYPE_EOF, TOK_ATT_DEFAULT, lineno);
    munmap(source, fstats.st_size);
    close(source_fd);
    //print_tokens();
}

void add_token(char *lexeme, tok_types_e type, tok_att_s att, int lineno)
{
    token_s *t = alloc(sizeof(*t));
    
    t->type = type;
    t->att = att;
    t->lineno = lineno;
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
    access_list_s *list;
    
    if(tok()->type == TOK_TYPE_ID) {
        list = parse_id();
        parse_idfollow(list);
        parse_statement();
    }
    else if(tok()->type == TOK_TYPE_EOF) {
        
    }
    else {
        fprintf(stderr, "Syntax Error at line %d: Expected EOF but got %s\n", tok()->lineno, tok()->lexeme);
    }
}

access_list_s *parse_id(void)
{
    access_list_s *list = NULL, *acc;
    
    if(tok()->type == TOK_TYPE_ID) {
        list = alloc(sizeof(*list));
        list->name = tok()->lexeme;
        list->isindex = false;
        list->next = NULL;
        acc = list;
        next_tok();
        parse_index(&acc);
        parse_idsuffix(&acc);
    }
    else {
        fprintf(stderr, "Syntax Error at line %d: Expected identifier but got %s\n", tok()->lineno, tokcurr->lexeme);
    }
    return list;
}

void parse_idsuffix(access_list_s **acc)
{
    switch(tok()->type) {
        case TOK_TYPE_DOT:
            if(next_tok()->type == TOK_TYPE_ID) {
                (*acc)->next = alloc(sizeof(**acc));
                *acc = (*acc)->next;
                (*acc)->name = tok()->lexeme;
                (*acc)->isindex = false;
                (*acc)->next = NULL;
                next_tok();
                parse_index(acc);
                parse_idsuffix(acc);
            }
            else {
                fprintf(stderr, "Syntax Error at line %d: Expected identifier, but got %s\n", tok()->lineno, tok()->lexeme);
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
        case TOK_TYPE_COMMA:
        case TOK_TYPE_CLOSE_BRACKET:
        case TOK_TYPE_EOF:
            break;
        default:
            fprintf(stderr, "Syntax Error at line %d: Expected . { } string number = += ) ( identifier , or EOF but got %s\n", tok()->lineno, tok()->lexeme);
            break;
    }
}

void parse_index(access_list_s **acc)
{
    token_s *tbackup;
    object_s exp;
    
    switch(tok()->type) {
        case TOK_TYPE_OPENBRACKET:
            tbackup = tok();
            next_tok();
            exp = parse_expression().obj;
            if(tok()->type == TOK_TYPE_CLOSE_BRACKET) {
                if(exp.type == TYPE_INT) {
                    (*acc)->next = alloc(sizeof(**acc));
                    *acc = (*acc)->next;
                    (*acc)->index = atoi(exp.tok->lexeme);
                    (*acc)->isindex = true;
                    (*acc)->next = NULL;
                }
                else {
                    fprintf(stderr, "Invalid Type Used to index aggregate object near line %d. Expected integer but got ", tbackup->lineno);
                    switch(exp.type) {
                        case TYPE_REAL:
                            puts("real type.");
                            break;
                        case TYPE_STRING:
                            puts("string type.");
                            break;
                        case TYPE_AGGREGATE:
                            puts("aggregate type.");
                            break;
                        default:
                            puts("unknown type.");
                            break;
                    }
                }
                next_tok();
                parse_index(acc);
            }
            else {
                printf("Syntax Error at line %d: Expected [ but got %s\n", tok()->lineno, tok()->lexeme);
            }
            break;
        case TOK_TYPE_COMMA:
        case TOK_TYPE_CLOSEBRACE:
        case TOK_TYPE_ASSIGNOP:
        case TOK_TYPE_CLOSEPAREN:
        case TOK_TYPE_OPENPAREN:
        case TOK_TYPE_CLOSE_BRACKET:
        case TOK_TYPE_DOT:
        case TOK_TYPE_ID:
        case TOK_TYPE_EOF:
            break;
        default:
            printf("Syntax Error at line %d: Expected [ , } = += ) ( ] . identifier or EOF but got %s\n", tok()->lineno, tok()->lexeme);
            break;
    }
}


void parse_idfollow(access_list_s *acc)
{
    object_s obj;
    aggregate_s *agg;
    
    switch(tok()->type) {
        case TOK_TYPE_OPENPAREN:
            next_tok();
            agg = parse_aggregate_list();
            if(tok()->type == TOK_TYPE_CLOSEPAREN) {
                next_tok();
            }
            else {
                fprintf(stderr, "Syntax Error at line %d: Exprected ) but got %s\n", tok()->lineno,  tok()->lexeme);
            }
            break;
        case TOK_TYPE_ASSIGNOP:
            obj = parse_assignment();
            break;
        default:
            fprintf(stderr, "Syntax Error at line %d: Expected ( = or += but got: %s\n", tok()->lineno, tok()->lexeme);
            break;
    }
}

void parse_optfollow(access_list_s *acc)
{
    switch(tok()->type) {
        case TOK_TYPE_ASSIGNOP:
        case TOK_TYPE_OPENPAREN:
            parse_idfollow(acc);
            break;
        case TOK_TYPE_CLOSEBRACE:
        case TOK_TYPE_OPENBRACE:
        case TOK_TYPE_STRING:
        case TOK_TYPE_NUM:
        case TOK_TYPE_CLOSEPAREN:
        case TOK_TYPE_ID:
        case TOK_TYPE_COMMA:
        case TOK_TYPE_CLOSE_BRACKET:
        case TOK_TYPE_EOF:
            break;
        default:
            fprintf(stderr, "Syntax Error at line %d: Expected = += ( } { string number ) id , or EOF but got %s\n", tok()->lineno, tok()->lexeme);
            break;
    }
}

object_s parse_assignment(void)
{
    object_s obj;
    
    if(tok()->type == TOK_TYPE_ASSIGNOP) {
        next_tok();
        obj = parse_expression().obj;
    }
    else {
        fprintf(stderr, "Syntax Error at line %d: Expected += or = but got %s\n", tok()->lineno, tok()->lexeme);
    }
    return obj;
}

exp_s parse_expression(void)
{
    exp_s exp;
    access_list_s *acc;
    
    exp.name = NULL;
    switch(tok()->type) {
        case TOK_TYPE_NUM:
            exp.obj.tok = tok();
            if(tok()->att == TOK_ATT_INT)
                exp.obj.type = TYPE_INT;
            else
                exp.obj.type = TYPE_REAL;
            next_tok();
            break;
        case TOK_TYPE_STRING:
            exp.obj.tok = tok();
            exp.obj.type = TYPE_STRING;
            next_tok();
            break;
        case TOK_TYPE_ID:
            acc = parse_id();
            parse_optfollow(acc);
            break;
        case TOK_TYPE_OPENBRACE:
            exp.obj.tok = tok();
            exp.obj.agg = parse_aggregate();
            exp.obj.type = TYPE_AGGREGATE;
            break;
        default:
            fprintf(stderr, "Syntax Error at line %d: Expected number string identifer or { but got %s\n", tok()->lineno, tok()->lexeme);
            break;
    }
    return exp;
}

aggregate_s *parse_aggregate(void)
{
    aggregate_s *agg;
    
    if(tok()->type == TOK_TYPE_OPENBRACE) {
        next_tok();
        agg = parse_aggregate_list();
        if(tok()->type == TOK_TYPE_CLOSEBRACE) {
            next_tok();
        }
        else {
            fprintf(stderr, "Syntax Error at line %d: Expected } but got %s\n", tok()->lineno, tok()->lexeme);
        }
    }
    else {
        fprintf(stderr, "Syntax Error at line %d: Expected { but got %s\n", tok()->lineno, tok()->lexeme);
    }
    return NULL;
}

aggregate_s *parse_aggregate_list(void)
{
    exp_s exp;
    aggregate_s *agg = NULL;
    
    switch(tok()->type) {
        case TOK_TYPE_OPENBRACE:
        case TOK_TYPE_STRING:
        case TOK_TYPE_NUM:
        case TOK_TYPE_ID:
            exp = parse_expression();
            agg = alloc(sizeof(*agg));
            agg->head = alloc(sizeof(*agg->head));
            agg->head->exp = exp;
            agg->tail = agg->head;
            agg->tail->next = NULL;
            agg->size = 1;
            parse_aggregate_list_(agg);
            break;
        case TOK_TYPE_CLOSEBRACE:
        case TOK_TYPE_CLOSEPAREN:
            agg = alloc(sizeof(*agg));
            agg->head = NULL;
            agg->tail = NULL;
            agg->size = 0;
            break;
        default:
            fprintf(stderr, "Syntax Error at line %d: Expected { string number identifier } or ) but got %s\n", tok()->lineno, tok()->lexeme);
            break;
    }
    return agg;
}

void parse_aggregate_list_(aggregate_s *agg)
{
    switch(tok()->type) {
        case TOK_TYPE_COMMA:
            next_tok();
            parse_expression();
            parse_aggregate_list_(agg);
            break;
        case TOK_TYPE_CLOSEBRACE:
        case TOK_TYPE_CLOSEPAREN:
            break;
        default:
            fprintf(stderr, "Syntax Error at line %d: Expected , } or ) but got %s\n", tok()->lineno, tok()->lexeme);
            break;
    }
}

scope_s *make_scope(scope_s *parent, char *ident)
{
    scope_s *s = alloc(sizeof(*s));
    
    s->children = NULL;
    s->nchildren = 0;
    s->parent = parent;
    s->ident = ident;
    if(parent) {
        parent->nchildren++;
        parent->children = ralloc(parent->children, parent->nchildren*sizeof(*parent->children));
        parent->children[parent->nchildren-1] = s;
    }
    return s;
}

void print_accesslist(access_list_s *list)
{
    access_list_s *l;
    
    for(l = list; l; l = l->next) {
        if(l->isindex) {
            printf("[%d]", l->index);
        }
        else {
            if(l == list)
                printf("%s", l->name);
            else
                printf("->%s", l->name);
        }
    }
    putchar('\n');
}


bool ident_add(char *key, int att)
{
    uint16_t index = hash_pjw(key);
    sym_record_s *rec = symtable.table[index];
    
    if(rec) {
        while(rec->next) {
            if(!strcmp(rec->string, key))
                return false;
            rec = rec->next;
        }
        if(!strcmp(rec->string, key))
            return false;
        rec->next = alloc(sizeof(*rec));
        rec = rec->next;
    }
    else {
        rec = alloc(sizeof(*rec));
        symtable.table[index] = rec;
    }
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
    
    fd = open(name, O_RDWR);
    if(fd < 0) {
        perror("Failed to open file");
        exit(EXIT_FAILURE);
    }
    source_fd = fd;
    status = fstat(fd, &fstats);
    if(status < 0) {
        perror("Failed to obtain file info");
        exit(EXIT_FAILURE);
    }
    source = mmap(0, fstats.st_size, PROT_WRITE, MAP_PRIVATE, fd, 0);
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

