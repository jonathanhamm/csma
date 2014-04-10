/* Parser for Reading Network File */
#include "parse.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <assert.h>
#include <stdint.h>

#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

#define INIT_BUF_SIZE 256
#define SYM_TABLE_SIZE 53
#define N_FUNCS 6

#define next_tok() (tokcurr = tokcurr->next)
#define tok() (tokcurr)

typedef enum type_e type_e;

typedef struct sym_record_s sym_record_s;
typedef struct sym_table_s sym_table_s;
typedef struct object_s object_s;
typedef struct exp_s exp_s;
typedef struct scope_s scope_s;
typedef struct access_list_s access_list_s;
typedef struct func_s func_s;
typedef struct optfollow_s optfollow_s;
typedef struct params_s params_s;
typedef struct check_s check_s;

enum funcs {
    FNET_SEND,
    FNET_NODE,
    FNET_RAND,
    FNET_SIZE,
    FNET_KILL,
    FNET_PRINT
};

enum type_e
{
    TYPE_INT,
    TYPE_REAL,
    TYPE_INF,
    TYPE_STRING,
    TYPE_NODE,
    TYPE_ARGLIST,
    TYPE_AGGREGATE,
    TYPE_NULL,
    TYPE_ANY
};

struct sym_record_s
{
    char *key;
    void *object;
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
        scope_s *child;
    };
    type_e type;
    bool islazy;
};

struct exp_s
{
    access_list_s *acc;
    object_s obj;
};

struct scope_s
{
    char *id;
    int size;
    scope_s *parent;
    sym_table_s table;
    object_s **object;
};

struct access_list_s
{
    bool isindex;
    int index;
    token_s *tok;
    bool islazy;
    access_list_s *next;
};

struct params_s
{
    char *name;
    type_e type;
};

struct func_s
{
    char *name;
    type_e type;
    void *(*func)(void *);
};

struct optfollow_s
{
    bool isassign;
    exp_s exp;
};

struct check_s
{
    bool found;
    bool lastfailed;
    access_list_s *last;
    object_s *result;
    scope_s *scope;
};

static token_s *head;
static token_s *tokcurr;
static token_s *tail;

static scope_s *scope_root;

static char *source;
static int source_fd;
static struct stat fstats;

static func_s funcs[] = {
    {"send", TYPE_NODE},
    {"node", TYPE_NULL},
    {"rand", TYPE_NULL},
    {"size", TYPE_ANY},
    {"kill", TYPE_NODE},
    {"print", TYPE_ANY}
};

static void readfile(const char *name);
static void lex(const char *name);
static void add_token(char *lexeme, tok_types_e type, tok_att_s att, int lineno);
static void print_tokens(void);

static void sym_insert(sym_table_s *table, char *key, void *object);
static sym_record_s *sym_lookup(sym_table_s *table, char *key);
static char *sym_get(sym_table_s *table, void *obj);
static uint16_t hash_pjw(char *key);

static void parse_statement(void);
static access_list_s *parse_id(void);
static void parse_idsuffix(access_list_s **acc);
static void parse_index(access_list_s **acc);
static optfollow_s parse_idfollow(access_list_s *acc);
static optfollow_s parse_optfollow(access_list_s *acc);
static exp_s parse_assignment(void);
static exp_s parse_expression(void);
static void parse_aggregate(object_s *obj);
static void parse_aggregate_list(object_s *obj);
static void parse_aggregate_list_(object_s *obj);

static scope_s *make_scope(scope_s *parent, char *id);
static void scope_add(scope_s *scope, object_s obj, char *id);
static check_s check_entry(scope_s *root, access_list_s *acc);

static bool function_check(check_s check, object_s *args);


static void print_accesslist(access_list_s *list);
static void print_object(void *obj);

static void clear_scope(scope_s *root);
static void free_accesslist(access_list_s *l);
static void free_tokens(void);

static char *strclone(char *str);

void parse(const char *file)
{

    /* Link Language Functions */
    funcs[FNET_SEND].func = net_send;
    funcs[FNET_NODE].func = net_node;
    funcs[FNET_RAND].func = net_rand;
    funcs[FNET_SIZE].func = net_size;
    funcs[FNET_KILL].func = net_kill;
    funcs[FNET_PRINT].func = net_print;
    
    lex(file);
    tokcurr = head;
    scope_root = make_scope(NULL, "_root");
    parse_statement();
}

void lex(const char *name)
{
    int lineno = 1;
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
                    if(!strcmp(bptr, "inf"))
                        add_token(bptr, TOK_TYPE_NUM, TOK_ATT_INF, lineno);
                    else
                        add_token(bptr, TOK_TYPE_ID, TOK_ATT_DEFAULT, lineno);
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
    token_s *id;
    check_s check;
    optfollow_s opt;
    
    if(tok()->type == TOK_TYPE_ID) {
        opt.exp.obj.tok = id = tok();
        list = parse_id();
        opt = parse_idfollow(list);
        
        check = check_entry(scope_root, list);
        if(opt.exp.obj.type == TYPE_ARGLIST) {
            if(check.lastfailed) {
                function_check(check, &opt.exp.obj);
            }
            else {
                fprintf(stderr, "Error near line %d: Access to undeclared object in %s\n", id->lineno, check.last->tok->lexeme);
            }
        }
        else {
            if(check.found) {
                *check.result = opt.exp.obj;
            }
            else if(check.lastfailed) {
                scope_add(check.scope, opt.exp.obj, check.last->tok->lexeme);
            }
            else {
                fprintf(stderr, "Error near line %d: Access to undeclared object in %s\n", id->lineno, check.last->tok->lexeme);
            }
        }
        
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
        list->tok = tok();
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
                (*acc)->tok = tok();
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
    token_s *t;
    exp_s exp;
    
    switch(tok()->type) {
        case TOK_TYPE_OPENBRACKET:
            t = next_tok();
            exp = parse_expression();
            if(tok()->type == TOK_TYPE_CLOSE_BRACKET) {
                if(exp.obj.type == TYPE_INT) {
                    (*acc)->next = alloc(sizeof(**acc));
                    *acc = (*acc)->next;
                    (*acc)->index = atoi(exp.obj.tok->lexeme);
                    (*acc)->isindex = true;
                    (*acc)->tok = t;
                    (*acc)->next = NULL;
                }
                else {
                    fprintf(stderr, "Error: invalid Type Used to index aggregate object near line %d. Expected integer but got ", t->lineno);
                    switch(exp.obj.type) {
                        case TYPE_REAL:
                            puts("real type.");
                            break;
                        case TYPE_STRING:
                            puts("string type.");
                            break;
                        case TYPE_INF:
                            puts("infinite");
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
            free_accesslist(exp.acc);
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

optfollow_s parse_idfollow(access_list_s *acc)
{
    optfollow_s opt;
    
    switch(tok()->type) {
        case TOK_TYPE_OPENPAREN:
            next_tok();
            opt.exp.obj.type = TYPE_ARGLIST;
            opt.isassign = false;
            opt.exp.acc = NULL;
            parse_aggregate_list(&opt.exp.obj);
            if(tok()->type == TOK_TYPE_CLOSEPAREN) {
                next_tok();
            }
            else {
                fprintf(stderr, "Syntax Error at line %d: Exprected ) but got %s\n", tok()->lineno,  tok()->lexeme);
            }
            break;
        case TOK_TYPE_ASSIGNOP:
            opt.isassign = true;
            opt.exp = parse_assignment();
            break;
        default:
            fprintf(stderr, "Syntax Error at line %d: Expected ( = or += but got: %s\n", tok()->lineno, tok()->lexeme);
            break;
    }
    return opt;
}

optfollow_s parse_optfollow(access_list_s *acc)
{
    switch(tok()->type) {
        case TOK_TYPE_ASSIGNOP:
        case TOK_TYPE_OPENPAREN:
            return parse_idfollow(acc);
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
    return (optfollow_s){.isassign = false, .exp = {NULL, .obj = {.type = TYPE_NULL}}};
}

exp_s parse_assignment(void)
{
    exp_s exp;
    
    if(tok()->type == TOK_TYPE_ASSIGNOP) {
        next_tok();
        exp = parse_expression();
    }
    else {
        exp.acc = NULL;
        exp.obj.type = TYPE_NULL;
        fprintf(stderr, "Syntax Error at line %d: Expected += or = but got %s\n", tok()->lineno, tok()->lexeme);
    }
    return exp;
}

exp_s parse_expression(void)
{
    exp_s exp;
    access_list_s *acc;
    check_s check;
    optfollow_s opt;
    
    exp.acc = NULL;
    switch(tok()->type) {
        case TOK_TYPE_NUM:
            exp.obj.tok = tok();
            if(tok()->att == TOK_ATT_INT)
                exp.obj.type = TYPE_INT;
            else if(tok()->att == TOK_ATT_REAL)
                exp.obj.type = TYPE_REAL;
            else
                exp.obj.type = TYPE_INF;
            next_tok();
            break;
        case TOK_TYPE_STRING:
            exp.obj.tok = tok();
            exp.obj.type = TYPE_STRING;
            next_tok();
            break;
        case TOK_TYPE_ID:
            acc = parse_id();
            opt = parse_optfollow(acc);

            if(opt.isassign) {
                exp.acc = acc;
                exp.obj = opt.exp.obj;
            }
            else {
                check = check_entry(scope_root, acc);
                if(check.found) {
                    exp.obj = *check.result;
                }
                else {
                    if(opt.exp.obj.type == TYPE_ARGLIST) {
                        function_check(check, &opt.exp.obj);
                    }
                    else {
                        fprintf(stderr, "Error: access to undeclared identifier %s at line %u\n", check.last->tok->lexeme, check.last->tok->lineno);
                    }

                }
            }
            break;
        case TOK_TYPE_OPENBRACE:
            exp.obj.tok = tok();
            exp.obj.type = TYPE_AGGREGATE;
            parse_aggregate(&exp.obj);
            break;
        default:
            fprintf(stderr, "Syntax Error at line %d: Expected number string identifer or { but got %s\n", tok()->lineno, tok()->lexeme);
            break;
    }
    return exp;
}

void parse_aggregate(object_s *obj)
{
    if(tok()->type == TOK_TYPE_OPENBRACE) {
        next_tok();
        parse_aggregate_list(obj);
        if(tok()->type == TOK_TYPE_CLOSEBRACE) {
            next_tok();
        }
        else {
            obj->type = TYPE_NULL;
            fprintf(stderr, "Syntax Error at line %d: Expected } but got %s\n", tok()->lineno, tok()->lexeme);
        }
    }
    else {
        obj->type = TYPE_NULL;
        fprintf(stderr, "Syntax Error at line %d: Expected { but got %s\n", tok()->lineno, tok()->lexeme);
    }
}

void parse_aggregate_list(object_s *obj)
{
    exp_s exp;
    check_s check;
    token_s *t;
    
    switch(tok()->type) {
        case TOK_TYPE_OPENBRACE:
        case TOK_TYPE_STRING:
        case TOK_TYPE_NUM:
        case TOK_TYPE_ID:
            t = tok();
            exp = parse_expression();
            obj->child = make_scope(NULL, "_anonymous");
            if(exp.acc) {
                if(!exp.acc->next) {
                    check = check_entry(obj->child, exp.acc);
                    if(check.found) {
                        fprintf(stderr, "Error: Redeclaration of aggregate members within same initializer not permitted: %s at line %u\n", exp.acc->tok->lexeme,  t->lineno);
                    }
                    else {
                        scope_add(obj->child, exp.obj, exp.acc->tok->lexeme);
                    }
                }
            }
            else {
                scope_add(obj->child, exp.obj, NULL);
            }
            parse_aggregate_list_(obj);
            break;
        case TOK_TYPE_CLOSEBRACE:
        case TOK_TYPE_CLOSEPAREN:
            obj->child = make_scope(NULL, "_anonymous");
            break;
        default:
            fprintf(stderr, "Syntax Error at line %d: Expected { string number identifier } or ) but got %s\n", tok()->lineno, tok()->lexeme);
            break;
    }
}

void parse_aggregate_list_(object_s *obj)
{
    exp_s exp;
    check_s check;
    token_s *t;
    
    switch(tok()->type) {
        case TOK_TYPE_COMMA:
            t = next_tok();
            exp = parse_expression();
            if(exp.acc) {
                if(!exp.acc->next) {
                    check = check_entry(obj->child, exp.acc);
                    if(check.found) {
                        fprintf(stderr, "Error: Redeclaration of aggregate members within same initializer not permitted: %s at line %u\n", exp.acc->tok->lexeme,  t->lineno);
                    }
                    else {
                        scope_add(obj->child, exp.obj, exp.acc->tok->lexeme);
                    }
                }
            }
            else {
                scope_add(obj->child, exp.obj, NULL);
            }
            parse_aggregate_list_(obj);
            break;
        case TOK_TYPE_CLOSEBRACE:
        case TOK_TYPE_CLOSEPAREN:
            break;
        default:
            fprintf(stderr, "Syntax Error at line %d: Expected , } or ) but got %s\n", tok()->lineno, tok()->lexeme);
            break;
    }
}

scope_s *make_scope(scope_s *parent, char *id)
{
    object_s obj;
    scope_s *s = allocz(sizeof(*s));
    
    s->id = id;
    s->size = 0;
    s->object = NULL;
    if(parent) {
        s->parent = parent;
        obj.type = TYPE_AGGREGATE;
        obj.child = s;
        obj.islazy = false;
        scope_add(parent, obj, id);
    }
    return s;
}

void scope_add(scope_s *scope, object_s obj, char *id)
{
    object_s *obj_alloc;
    
    scope->object = ralloc(scope->object, (scope->size + 1)*sizeof(*scope->object));
    obj_alloc = alloc(sizeof(obj));
    
    *obj_alloc = obj;
    scope->object[scope->size] = obj_alloc;
    scope->size++;
    
    if(id)
        sym_insert(&scope->table, id, obj_alloc);
}

check_s check_entry(scope_s *root, access_list_s *acc)
{
    sym_record_s *rec;
    check_s check;

    check.scope = root;
    while(true) {
        if(acc->isindex) {
            if(acc->index > check.scope->size) {
                check.found = false;
                check.last = acc;
                check.result = NULL;
                check.lastfailed = false;
                fprintf(stderr, "Error: Index Out of Bounds at line %u\n", acc->tok->lineno);
                return check;
            }
            else if (acc->index == check.scope->size) {
                if(!acc->next) {
                    check.found = false;
                    check.lastfailed = true;
                    check.last = acc;
                }
                else {
                    fprintf(stderr, "Error: Attempt to access uninitialized array element at line %u\n", acc->tok->lineno);
                    check.found = false;
                    check.lastfailed = false;
                    check.last = acc;
                }
                return check;
            }
            check.result = check.scope->object[acc->index];
        }
        else {
            rec = sym_lookup(&check.scope->table, acc->tok->lexeme);
            if(rec)
                check.result = rec->object;
            else {
                check.found = false;
                check.last = acc;
                check.result = NULL;
                if(!acc->next)
                    check.lastfailed = true;
                return check;
            }
        }
        if(acc->next) {
            if(check.result->type == TYPE_AGGREGATE) {
                acc = acc->next;
                check.scope = check.result->child;
            }
            else {
                check.found = false;
                check.last = acc;
                check.result = NULL;
                if(!acc->next)
                    check.lastfailed = true;
                return check;
            }
        }
        else {
            check.found = true;
            check.lastfailed = false;
            return check;
        }
     }
}


bool function_check(check_s check, object_s *args)
{
    int i;
    char *str = check.last->tok->lexeme;
    
    for(i = 0; i < N_FUNCS; i++) {
        if(!strcmp(funcs[i].name, str)) {
            switch(funcs[i].type) {
                case TYPE_NULL:
                   // if(check. != global) {
                      //  fprintf(stderr, "Cannot call function %s on object types.\n", str);
                    //}
                    funcs[i].func(NULL);
                    break;
                case TYPE_ANY:
                    funcs[i].func(args);
                    break;
                default:
                    //if(funcs[i].type == check.last)

                    break;
            }
            return true;
        }
    }
    return false;
}

void *net_send(void *arg)
{
    
}

void *net_node(void *arg)
{
    
}

void *net_rand(void *arg)
{
    printf("Calling Rand\n");
}

void *net_size(void *arg)
{
    
}

void *net_kill(void *arg)
{
    
}

void *net_clear(void *arg)
{
    //clear_scope(global);
}


void *net_print(void *arg)
{
    int i;
    object_s *obj = arg;
    
    if(obj->child->size > 0) {
        for(i = 0; i < obj->child->size; i++) {
            print_object(obj->child->object[i]);
        }
    }
    else {
        
    }
    
    return NULL;
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
                printf("%s", l->tok->lexeme);
            else
                printf("->%s", l->tok->lexeme);
        }
    }
    putchar('\n');
}

void print_object(void *object)
{
    int i;
    object_s *obj = object;
    
    switch(obj->type) {
        case TYPE_INT:
        case TYPE_REAL:
        case TYPE_INF:
        case TYPE_STRING:
        case TYPE_NODE:
        case TYPE_NULL:
        case TYPE_ANY:
            printf("%s", obj->tok->lexeme);
            break;
        case TYPE_AGGREGATE:
            printf("{ ");
            for(i = 0; i < obj->child->size-1; i++) {
                print_object(obj->child->object[i]);
                printf(", ");
            }
            print_object(obj->child->object[i]);
            printf(" }");
            break;
        case TYPE_ARGLIST:
        default:
            puts("illegal state");
            assert(false);
            break;
    }
}

void free_accesslist(access_list_s *l)
{
    access_list_s *lb;
    
    while(l) {
        lb = l->next;
        free(l);
        l = lb;
    }
}

void free_tokens(void)
{
    token_s *tb;
    
    while(head) {
        tb = head->next;
        free(head->lexeme);
        free(head);
        head = tb;
    }
}

void sym_insert(sym_table_s *table, char *key, void *object)
{
    uint16_t index = hash_pjw(key);
    sym_record_s *rec = alloc(sizeof(*rec));
    
    rec->next = table->table[index];
    table->table[index] = rec;
    rec->key = key;
    rec->object = object;
    rec->next = NULL;
}

sym_record_s *sym_lookup(sym_table_s *table, char *key)
{
    sym_record_s *rec = table->table[hash_pjw(key)];
    
    while(rec) {
        if(!strcmp(rec->key, key))
            return rec;
        rec = rec->next;
    }
    return NULL;
}

char *sym_get(sym_table_s *table, void *obj)
{
    int i;
    sym_record_s **rec = table->table, *ref;
    
    for(i = 0; i < SYM_TABLE_SIZE; i++) {
        if(*rec) {
            for(ref = *rec; ref; ref = ref->next) {
                if(ref->object == obj)
                    return ref->key;
            }
        }
        rec++;
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
    buf_s *b = alloc(sizeof(*b) + INIT_BUF_SIZE);
    b->bsize = INIT_BUF_SIZE;
    b->size = 0;
    return b;
}

void buf_addc(buf_s **b, int c)
{
    register buf_s *bb = *b;
    
    if(bb->size == bb->bsize) {
        bb->bsize *= 2;
        bb = *b = ralloc(bb, sizeof(*bb) + bb->bsize);
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

void buf_reset(buf_s **b)
{
    *b = ralloc(*b, sizeof(**b) + INIT_BUF_SIZE);
    (*b)->bsize = INIT_BUF_SIZE;
    (*b)->size = 0;
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

