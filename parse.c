/* Parser for Reading Network File */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include <stdint.h>
#include <stdarg.h>

#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

#include "parse.h"

#define INIT_BUF_SIZE 256
#define N_FUNCS 6

#define next_tok() (tokcurr = tokcurr->next)
#define tok() (tokcurr)

typedef enum type_e type_e;

typedef struct object_s object_s;
typedef struct exp_s exp_s;
typedef struct scope_s scope_s;
typedef struct access_list_s access_list_s;
typedef struct func_s func_s;
typedef struct optfollow_s optfollow_s;
typedef struct params_s params_s;
typedef struct check_s check_s;
typedef struct arg_s arg_s;
typedef struct arglist_s arglist_s;

enum type_e
{
    TYPE_INT,
    TYPE_REAL,
    TYPE_INF,
    TYPE_STRING,
    TYPE_NODE,
    TYPE_ARGLIST,
    TYPE_AGGREGATE,
    TYPE_ERROR,
    TYPE_NULL,
    TYPE_ANY
};

struct object_s
{
    union {
        token_s *tok;
        scope_s *child;
    };
    type_e type;
    arglist_s *arglist;
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

static token_s *head;
static token_s *tokcurr;
static token_s *tail;

static scope_s *scope_root;

static char *source;
static int source_fd;
static struct stat fstats;
static int printtabs;

static bool parse_success;

static func_s funcs[] = {
    {"send", TYPE_NODE},
    {"node", TYPE_NULL},
    {"rand", TYPE_NULL},
    {"size", TYPE_ANY},
    {"kill", TYPE_NODE},
    {"print", TYPE_ANY}
};

static void lex(char *src);
static void add_token(char *lexeme, tok_types_e type, tok_att_s att, int lineno);
static void print_tokens(void);

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
static void parse_aggregate_list(object_s *obj, arglist_s **args);
static void parse_aggregate_list_(object_s *obj, arglist_s *args);

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

bool parse(char *src)
{
    parse_success = true;

    /* Link Language Functions */
    funcs[FNET_SEND].func = net_send;
    funcs[FNET_NODE].func = net_node;
    funcs[FNET_RAND].func = net_rand;
    funcs[FNET_SIZE].func = net_size;
    funcs[FNET_KILL].func = net_kill;
    funcs[FNET_PRINT].func = net_print;
    
    lex(src);
    tokcurr = head;
    if(!scope_root)
        scope_root = make_scope(NULL, "_root");
    parse_statement();
    free_tokens();
    head = NULL;
    tokcurr = NULL;
    tail = NULL;
    
    return parse_success;
}

void error(const char *fs, ...)
{
    va_list args;
    
    va_start(args, fs);
    vfprintf(stderr, fs, args);
    va_end(args);
    
    putchar('\n');
    
    parse_success = false;
}

void lex(char *src)
{
    int lineno = 1;
    char *bptr, *fptr, c;
    
    bptr = fptr = src;
    
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
                        error("Improperly closed double quote");
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
                    error("Stray '+'");
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
                    error("Lexical Error at line %d: Unknown symbol %c", lineno, *fptr);
                    fptr++;
                }
                break;
        }
    }
    add_token("EOF", TOK_TYPE_EOF, TOK_ATT_DEFAULT, lineno);
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
    t->marked = false;
    
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
        id = tok();
        id->marked = true;
        list = parse_id();
        opt = parse_idfollow(list);
        opt.exp.obj.tok = id;
        
        check = check_entry(scope_root, list);
        if(opt.exp.obj.type == TYPE_ARGLIST) {
            if(check.lastfailed) {
                if(!function_check(check, &opt.exp.obj)) {
                    error(
                          "Error: Call to unkown function %s at line %u",
                          check.last->tok->lexeme, check.last->tok->lineno
                          );
                }
            }
            else {
                error(
                      "Error near line %d: Access to undeclared object in %s",
                      id->lineno, check.last->tok->lexeme
                      );
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
                error(
                      "Error near line %d: Access to undeclared object in %s",
                      id->lineno, check.last->tok->lexeme
                      );
            }
        }
        parse_statement();
    }
    else if(tok()->type == TOK_TYPE_EOF) {
        
    }
    else {
        error(
              "Syntax Error at line %d: Expected EOF but got %s",
              tok()->lineno, tok()->lexeme
              );
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
        error(
              "Syntax Error at line %d: Expected identifier but got %s",
              tok()->lineno, tokcurr->lexeme
              );
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
                error(
                      "Syntax Error at line %d: Expected identifier, but got %s",
                      tok()->lineno, tok()->lexeme
                      );
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
            error(
                  "Syntax Error at line %d: Expected . { } string number = += ) ( \
                  identifier , or EOF but got %s",
                  tok()->lineno, tok()->lexeme
                  );
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
                    error(
                          "Error: invalid Type Used to index aggregate object near line %d. \
                          Expected integer but got ",
                          t->lineno
                          );
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
                error(
                      "Syntax Error at line %d: Expected [ but got %s",
                      tok()->lineno, tok()->lexeme
                      );
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
            error(
                  "Syntax Error at line %d: Expected [ , } = += ) ( ] . \
                  identifier or EOF but got %s",
                  tok()->lineno, tok()->lexeme
                  );
            break;
    }
}

optfollow_s parse_idfollow(access_list_s *acc)
{
    token_s *t;
    optfollow_s opt;
    arglist_s *args = NULL;
    arglist_s **arggs = &args;
    
    switch(tok()->type) {
        case TOK_TYPE_OPENPAREN:
            t = next_tok();
            opt.exp.obj.type = TYPE_ARGLIST;
            opt.isassign = false;
            opt.exp.acc = NULL;
            opt.exp.obj.child = NULL;
            opt.exp.obj.tok = t;
            parse_aggregate_list(NULL, arggs);
            opt.exp.obj.arglist = args;
            if(tok()->type == TOK_TYPE_CLOSEPAREN) {
                next_tok();
            }
            else {
                error(
                      "Syntax Error at line %d: Exprected ) but got %s",
                      tok()->lineno,  tok()->lexeme
                      );
            }
            break;
        case TOK_TYPE_ASSIGNOP:
            opt.isassign = true;
            opt.exp = parse_assignment();
            break;
        default:
            opt.isassign = false;
            opt.exp.acc = NULL;
            opt.exp.obj.type = TYPE_ERROR;
            error(
                  "Syntax Error at line %d: Expected ( = or += but got: %s",
                  tok()->lineno, tok()->lexeme
                  );
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
            error(
                  "Syntax Error at line %d: Expected = += ( } { string number ) \
                  id , or EOF but got %s",
                  tok()->lineno, tok()->lexeme
                  );
            break;
    }
    return (optfollow_s){.isassign = false, .exp = {NULL, .obj = {.type = TYPE_ERROR}}};
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
        exp.obj.type = TYPE_ERROR;
        error(
              "Syntax Error at line %d: Expected += or = but got %s",
              tok()->lineno, tok()->lexeme
              );
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
            exp.obj.tok->marked = true;
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
            exp.obj.tok->marked = true;
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
                        if(function_check(check, &opt.exp.obj))
                            exp.obj.type = TYPE_ANY;
                        else
                            exp.obj.type = TYPE_ERROR;
                        exp.obj.tok = check.last->tok;
                        exp.obj.tok->marked = true;
                        exp.obj.islazy = false;
                    }
                    else {
                        exp.obj.type = TYPE_ERROR;
                        error(
                              "Error: access to undeclared identifier %s at line %u",
                              check.last->tok->lexeme, check.last->tok->lineno
                              );
                    }

                }
            }
            break;
        case TOK_TYPE_OPENBRACE:
            exp.obj.tok = tok();
            exp.obj.tok->marked = true;
            exp.obj.type = TYPE_AGGREGATE;
            parse_aggregate(&exp.obj);
            break;
        default:
            error(
                  "Syntax Error at line %d: Expected number string \
                  identifer or { but got %s",
                  tok()->lineno, tok()->lexeme
                  );
            break;
    }
    return exp;
}

void parse_aggregate(object_s *obj)
{
    if(tok()->type == TOK_TYPE_OPENBRACE) {
        next_tok();
        parse_aggregate_list(obj, NULL);
        if(tok()->type == TOK_TYPE_CLOSEBRACE) {
            next_tok();
        }
        else {
            obj->type = TYPE_ERROR;
            error(
                  "Syntax Error at line %d: Expected } but got %s",
                  tok()->lineno, tok()->lexeme
                  );
        }
    }
    else {
        obj->type = TYPE_ERROR;
        error(
              "Syntax Error at line %d: Expected { but got %s",
              tok()->lineno, tok()->lexeme
              );
    }
}

void parse_aggregate_list(object_s *obj, arglist_s **args)
{
    exp_s exp;
    check_s check;
    token_s *t;
    arglist_s *arggs = NULL;
    
    switch(tok()->type) {
        case TOK_TYPE_OPENBRACE:
        case TOK_TYPE_STRING:
        case TOK_TYPE_NUM:
        case TOK_TYPE_ID:
            t = tok();
            exp = parse_expression();
            if(obj) {
                obj->child = make_scope(NULL, "_anonymous");
                if(exp.acc) {
                    if(!exp.acc->next) {
                        check = check_entry(obj->child, exp.acc);
                        if(check.found) {
                            error(
                                  "Error: Redeclaration of aggregate members within same \
                                  initializer not permitted: %s at line %u",
                                  exp.acc->tok->lexeme,  t->lineno
                                  );
                        }
                        else {
                            scope_add(obj->child, exp.obj, exp.acc->tok->lexeme);
                        }
                    }
                }
                else {
                    scope_add(obj->child, exp.obj, NULL);
                }
            }
            else {
                *args = alloc(sizeof(**args));
                arggs = *args;
                arggs->size = 1;
                arggs->head = alloc(sizeof(*arggs->head));
                arggs->head->next = NULL;
                arggs->head->obj = exp.obj;
                arggs->tail = arggs->head;
            }
            return parse_aggregate_list_(obj, arggs);
            break;
        case TOK_TYPE_CLOSEBRACE:
        case TOK_TYPE_CLOSEPAREN:
            obj->child = make_scope(NULL, "_anonymous");
            break;
        default:
            error(
                  "Syntax Error at line %d: Expected { string number identifier } \
                  or ) but got %s",
                  tok()->lineno, tok()->lexeme
                  );
            break;
    }
}

void parse_aggregate_list_(object_s *obj, arglist_s *args)
{
    exp_s exp;
    check_s check;
    token_s *t;
    
    switch(tok()->type) {
        case TOK_TYPE_COMMA:
            t = next_tok();
            exp = parse_expression();
            if(obj) {
                if(exp.acc) {
                    if(!exp.acc->next) {
                        check = check_entry(obj->child, exp.acc);
                        if(check.found) {
                            error(
                                  "Error: Redeclaration of aggregate members within same initializer \
                                  not permitted: %s at line %u",
                                  exp.acc->tok->lexeme,  t->lineno
                                  );
                        }
                        else {
                            scope_add(obj->child, exp.obj, exp.acc->tok->lexeme);
                        }
                    }
                }
                else {
                    scope_add(obj->child, exp.obj, NULL);
                }
            }
            else {
                args->tail->next = alloc(sizeof(*args->tail));
                args->tail = args->tail->next;
                args->size++;
                args->tail->next = NULL;
                args->tail->obj = exp.obj;
            }
            parse_aggregate_list_(obj, args);
            break;
        case TOK_TYPE_CLOSEBRACE:
        case TOK_TYPE_CLOSEPAREN:
            break;
        default:
            error(
                  "Syntax Error at line %d: Expected , } or ) but got %s",
                  tok()->lineno, tok()->lexeme
                  );
            break;
    }
}

token_s *tok_clone(token_s *t)
{
    token_s *clone = alloc(sizeof(*clone));
    
    *clone = *t;
    return clone;
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
    
    if(obj.type != TYPE_ERROR) {
        scope->object = ralloc(scope->object, (scope->size + 1)*sizeof(*scope->object));
        obj_alloc = alloc(sizeof(obj));
        
        *obj_alloc = obj;
        scope->object[scope->size] = obj_alloc;
        scope->size++;
        
        if(id)
            sym_insert(&scope->table, id, (sym_data_u){.ptr = obj_alloc});
    }
}

check_s check_entry(scope_s *root, access_list_s *acc)
{
    sym_record_s *rec = NULL;
    check_s check;

    
    check.scope = root;
    while(true) {
        if(acc->isindex) {
            if(acc->index > check.scope->size) {
                check.found = false;
                check.last = acc;
                check.result = NULL;
                check.lastfailed = false;
                error("Error: Index Out of Bounds at line %u", acc->tok->lineno);
                return check;
            }
            else if (acc->index == check.scope->size) {
                if(!acc->next) {
                    check.found = false;
                    check.lastfailed = true;
                    check.last = acc;
                }
                else {
                    error(
                          "Error: Attempt to access uninitialized array element at line %u",
                          acc->tok->lineno
                          );
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
                check.result = rec->data.ptr;
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
            check.last = acc;
            check.result = rec->data.ptr;
            return check;
        }
     }
}


bool function_check(check_s check, object_s *args)
{
    int i;
    char *str = check.last->tok->lexeme;
    
    if(args->type != TYPE_ERROR) {
        for(i = 0; i < N_FUNCS; i++) {
            if(!strcmp(funcs[i].name, str)) {
                switch(funcs[i].type) {
                    case TYPE_NULL:
                       // if(check. != global) {
                          //  fprintf(stderr, "Cannot call function %s on object types.\n", str);
                        //}
                        funcs[i].func(args);
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
    }
    return false;
}

void *net_send(void *arg)
{
    object_s *args = arg;
    
}

void *net_node(void *arg)
{
    task_s *t;
    object_s *obj = arg;
    
    
    
    if(obj->arglist->size > 1) {
        error(
              "Error: Invalid number of arguments passed to function node at line %u. \
              Expected node(string)",
              obj->tok->lineno
             );
    }
    else {
        if(obj->arglist->head->obj.type == TYPE_STRING) {
            t = allocz(sizeof(*t) + sizeof(char *));
            t->func = FNET_NODE;
            t->next = NULL;
            *(char **)(t + 1) = obj->arglist->head->obj.tok->lexeme;
            task_enqueue(t);
        }
        else {
            error(
                  "Error at line %u: Expected string type argument for node(string).",
                  obj->tok->lineno
                  );
            
        }
    }
    
    return NULL;
    //task_enqueue(
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
    arg_s *a;
    
    if(obj->arglist->size > 1) {
        for(a = obj->arglist->head; a->next; a = a->next) {
            print_object(&a->obj);
            printf(", ");
        }
        print_object(&a->obj);
    }
    /*if(obj->arglist->size > 0) {
        for(i = 0; i < obj->child->size; i++) {
            print_object(obj->arglist->object[i]);
        }
    }
    else {
        if(scope_root->size > 0) {
            for(i = 0; i < scope_root->size-1; i++) {
                print_object(scope_root->object[i]);
                printf(", ");
            }
            print_object(scope_root->object[i]);
        }
    }*/
    putchar('\n');
    printtabs = 0;
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
        case TYPE_ANY:
            printf("%s", obj->tok->lexeme);
            break;
        case TYPE_AGGREGATE:
            printf("{ ");
            if(obj->child->size > 0) {
                for(i = 0; i < obj->child->size-1; i++) {
                    print_object(obj->child->object[i]);
                    printf(", ");
                }
                print_object(obj->child->object[i]);
            }
            printf(" }");
            break;
        case TYPE_NULL:
            printf("null");
            break;
        case TYPE_ERROR:
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
        if(head->marked) {
            head->next = NULL;
            head->prev = NULL;
            head = head->next;
        }
        else {
            tb = head->next;
            free(head->lexeme);
            free(head);
            head = tb;
        }
    }
}

void sym_insert(sym_table_s *table, char *key, sym_data_u data)
{
    uint16_t index = hash_pjw(key);
    sym_record_s *rec = alloc(sizeof(*rec));
    
    rec->next = table->table[index];
    table->table[index] = rec;
    rec->key = key;
    rec->data = data;
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
                if(ref->data.ptr == obj)
                    return ref->key;
            }
        }
        rec++;
    }
    return NULL;
}

void sym_delete(sym_table_s *table, char *key)
{
    uint16_t index = hash_pjw(key);
    sym_record_s *last = NULL;
    sym_record_s *rec = table->table[index];

    if(rec) {
        last = rec;
        while(rec) {
            if(!strcmp(rec->key, key)) {
                if(last)
                    last->next = rec->next;
                else
                    table->table[index] = rec->next;
                free(rec);
                return;
            }
            last = rec;
            rec = rec->next;
        }
    }
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

char *readfile(const char *fname)
{
    char *src;
    int fd, status;
    
    fd = open(fname, O_RDWR);
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
    src = mmap(0, fstats.st_size, PROT_WRITE, MAP_PRIVATE, fd, 0);
    if(source == MAP_FAILED) {
        perror("Failed to read file");
        exit(EXIT_FAILURE);
    }
    return src;
}

void closefile(void)
{
    munmap(source, fstats.st_size);
    close(source_fd);
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

void buf_free(buf_s *b)
{
    free(b);
}

void task_enqueue(task_s *t)
{
    if(tqueue.head)
        tqueue.tail->next = t;
    else
        tqueue.head = t;
    tqueue.tail = t;
}

task_s *task_dequeue(void)
{
    task_s *t = tqueue.head;
    if(t)
        tqueue.head = tqueue.head->next;
    return t;
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

