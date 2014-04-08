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

#define next_tok() (tokcurr = tokcurr->next)
#define tok() (tokcurr)

typedef enum type_e type_e;

typedef struct sym_record_s sym_record_s;
typedef struct sym_table_s sym_table_s;
typedef struct object_s object_s;
typedef struct exp_s exp_s;
typedef struct scope_s scope_s;
typedef struct access_list_s access_list_s;
typedef struct check_s check_s;
typedef struct func_s func_s;
typedef struct optfollow_s optfollow_s;
typedef struct params_s params_s;


enum type_e
{
    TYPE_INT,
    TYPE_REAL,
    TYPE_STRING,
    TYPE_NODE,
    TYPE_ARGLIST,
    TYPE_NULL,
    TYPE_ANY
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
        scope_s *agg;
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

struct check_s
{
    bool found;
    bool lastfailed;
    access_list_s *last;
    scope_s *node;
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
    object_s obj;
};

static token_s *head;
static token_s *tokcurr;
static token_s *tail;

static sym_table_s symtable;

static scope_s *global;

static char *source;
static int source_fd;
static struct stat fstats;

#define N_FUNCS 6

enum funcs {
    FNET_SEND,
    FNET_NODE,
    FNET_RAND,
    FNET_SIZE,
    FNET_KILL,
    FNET_PRINT
};

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

static bool ident_add(char *key, int att);
static sym_record_s *ident_lookup(char *key);
static uint16_t hash_pjw(char *key);

static void parse_statement(void);
static access_list_s *parse_id(void);
static void parse_idsuffix(access_list_s **acc);
static void parse_index(access_list_s **acc);
static optfollow_s parse_idfollow(access_list_s *acc);
static optfollow_s parse_optfollow(access_list_s *acc);
static object_s parse_assignment(void);
static exp_s parse_expression(void);
static scope_s *parse_aggregate(void);
static scope_s *parse_aggregate_list(void);
static void parse_aggregate_list_(scope_s *agg);

static scope_s *make_scope(scope_s *parent, char *ident);
static check_s check_entry(scope_s *root, access_list_s *acc);
static void add_entry(scope_s *root, access_list_s *acc, object_s obj);
static bool function_check(check_s check, scope_s *args);

static void print_accesslist(access_list_s *list);
static void print_object(scope_s *root);

static void clear_scope(scope_s *root);
static void free_accesslist(access_list_s *l);

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
    global = make_scope(NULL, "_root");
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
    token_s *id;
    check_s check;
    optfollow_s opt;
    
    if(tok()->type == TOK_TYPE_ID) {
        id = tok();
        list = parse_id();
        opt = parse_idfollow(list);
        check = check_entry(global, list);
        if(opt.obj.type == TYPE_ARGLIST) {
            if(check.lastfailed) {
                function_check(check, opt.obj.agg);
            }
            else {
                fprintf(stderr, "Error near line %d: Access to undeclared object in %s\n", id->lineno, check.last->name);
            }
        }
        else {
            if(check.found) {
                check.node->object= opt.obj;
            }
            else if(check.lastfailed) {
                add_entry(global, list, opt.obj);
            }
            else {
                fprintf(stderr, "Error near line %d: Access to undeclared object in %s\n", id->lineno, check.last->name);
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

optfollow_s parse_idfollow(access_list_s *acc)
{
    optfollow_s opt;
    scope_s *agg;
    
    opt.isassign = true;
    switch(tok()->type) {
        case TOK_TYPE_OPENPAREN:
            next_tok();
            agg = parse_aggregate_list();
            opt.obj.agg = agg;
            opt.obj.type = TYPE_ARGLIST;
            if(tok()->type == TOK_TYPE_CLOSEPAREN) {
                next_tok();
            }
            else {
                fprintf(stderr, "Syntax Error at line %d: Exprected ) but got %s\n", tok()->lineno,  tok()->lexeme);
            }
            break;
        case TOK_TYPE_ASSIGNOP:
            opt.isassign = true;
            opt.obj = parse_assignment();
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
    return (optfollow_s){.isassign = false, .obj = {.type = TYPE_NULL}};
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
    access_list_s *acc, *accb;
    check_s check;
    optfollow_s opt;
    
    exp.acc = NULL;
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
            opt = parse_optfollow(acc);
            if(opt.isassign) {
                exp.acc = acc;
                exp.obj = opt.obj;
            }
            else {
                check = check_entry(global, acc);
                if(check.found) {
                    exp.obj = check.node->object;
                }
                else {
                    printf("Access to undeclared identifier: %s\n", check.last->name);
                }
            }
            free_accesslist(acc);
            break;
        case TOK_TYPE_OPENBRACE:
            exp.obj.tok = tok();
            exp.obj.agg = parse_aggregate();
            exp.obj.type = TYPE_ANY;
            break;
        default:
            fprintf(stderr, "Syntax Error at line %d: Expected number string identifer or { but got %s\n", tok()->lineno, tok()->lexeme);
            break;
    }
    return exp;
}

scope_s *parse_aggregate(void)
{
    scope_s *agg;
    
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

scope_s *parse_aggregate_list(void)
{
    exp_s exp;
    scope_s *agg = NULL;
    
    switch(tok()->type) {
        case TOK_TYPE_OPENBRACE:
        case TOK_TYPE_STRING:
        case TOK_TYPE_NUM:
        case TOK_TYPE_ID:
            exp = parse_expression();
            agg = make_scope(NULL, "_anonymous");
            if(exp.acc) {
                if(!exp.acc->next) {
                    //puts("Single assignment within initializer");
                    add_entry(agg, exp.acc, exp.obj);
                }
            }
            parse_aggregate_list_(agg);
            break;
        case TOK_TYPE_CLOSEBRACE:
        case TOK_TYPE_CLOSEPAREN:
            break;
        default:
            fprintf(stderr, "Syntax Error at line %d: Expected { string number identifier } or ) but got %s\n", tok()->lineno, tok()->lexeme);
            break;
    }
    return agg;
}

void parse_aggregate_list_(scope_s *agg)
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
                    check = check_entry(agg, exp.acc);
                    if(check.found) {
                        fprintf(stderr, "Error near line %u: Redeclaration of aggregate member: %s\n", t->lineno, exp.acc->name);
                    }
                    else
                        add_entry(agg, exp.acc, exp.obj);
                }
            }
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
        if(parent->nchildren)
            parent->children = ralloc(parent->children, parent->nchildren*sizeof(*parent->children));
        else
            parent->children = alloc(sizeof(*parent->children));
        parent->children[parent->nchildren-1] = s;
    }
    return s;
}

check_s check_entry(scope_s *root, access_list_s *acc)
{
    int i;
    access_list_s *iter;
    check_s res;
    
    iter = acc;
repeat:
    for(i = 0; i < root->nchildren; i++) {
        if(iter->isindex) {
            if(iter->islazy) {
                
            }
            else {
                if(iter->index == i) {
                    root = root->children[i];
                    acc = acc->next;
                    goto repeat;
                }
            }
        }
        else if(!strcmp(root->children[i]->ident, iter->name)) {
            if(acc->next) {
                root = root->children[i];
                acc = acc->next;
                goto repeat;
            }
            else {
                res.found = true;
                res.lastfailed = false;
                res.node = root->children[i];
                return res;
            }
        }
    }
    if(!acc->next)
        res.lastfailed = true;
    else
        res.lastfailed = false;
    res.found = false;
    res.node = root;
    res.last = acc;
    return res;
}

void add_entry(scope_s *root, access_list_s *acc, object_s obj)
{
    int i;
    scope_s *new;
    
    while(acc->next) {
        for(i = 0; i < root->nchildren; i++) {
            if(!strcmp(root->children[i]->ident, acc->name))
                break;
            
        }
        assert(i != root->nchildren);
        acc = acc->next;
        root = root->children[i];
    }
    new = make_scope(root, acc->name);
    new->object = obj;
}

bool function_check(check_s check, scope_s *args)
{
    int i;
    char *str = check.last->name;
    
    for(i = 0; i < N_FUNCS; i++) {
        if(!strcmp(funcs[i].name, str)) {
            switch(funcs[i].type) {
                case TYPE_NULL:
                    if(check.node != global) {
                        fprintf(stderr, "Cannot call function %s on object types.\n", str);
                    }
                    funcs[i].func(NULL);
                    break;
                case TYPE_ANY:
                    funcs[i].func(args);
                    break;
                default:
                    //if(funcs[i].type == check.last)

                    break;
            }
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
    clear_scope(global);
}


void *net_print(void *arg)
{
    print_object(arg);
    putchar('\n');
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
                printf("%s", l->name);
            else
                printf("->%s", l->name);
        }
    }
    putchar('\n');
}

void print_object(scope_s *root)
{
    int i;
    object_s *optr;
    
    for(i = 0; i < root->nchildren; i++) {
        
        if(root->children[i]->nchildren) {
            puts("{ ");
            print_object(root->children[i]);
            puts(" }");
            continue;
        }
        
        optr = &root->children[i]->object;
        switch(optr->type) {
            case TYPE_INT:
            case TYPE_REAL:
            case TYPE_STRING:
                printf("%s", optr->tok->lexeme);
                break;
            case TYPE_NODE:
                break;
            case TYPE_ARGLIST:
                puts("is arglist");
                break;
            case TYPE_NULL:
                printf("null");
                break;
            default:
                printf("unknown");
                break;
        }
    }
}

void clear_scope(scope_s *root)
{
    int i;
    
    for(i = 0; i < root->nchildren; i++) {
        if(root->children[i]->nchildren)
            clear_scope(root->children[i]);
        free(root->children[i]);
    }
    free(root->children);
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

