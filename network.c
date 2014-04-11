#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#include <math.h>

#include <signal.h>
#include <unistd.h>

#include "parse.h"
#include "network.h"

static sym_table_s stations;

static void process_tasks(void);
static void create_node(char *id);

int main(int argc, char *argv[])
{
    int c;
    char *src;
    buf_s *in;
    
    if(argc > 1) {
        src = readfile(argv[1]);
        parse(src);
        closefile();
    }
    else {
        src = readfile("test");
        parse(src);
        closefile();
    }
    
    in = buf_init();
    
    printf("> ");
    while((c = getchar()) != EOF) {
        buf_addc(&in, c);
        if(c == '\n') {
            buf_addc(&in, '\0');
            parse(in->buf);
            process_tasks();
            buf_reset(&in);
            printf("> ");
        }
    }
    buf_free(in);
}

void process_tasks(void)
{
    task_s *t;
    
    while((t = task_dequeue())) {
        printf("pointer: %p\n", t);
        switch(t->func) {
            case FNET_NODE:
                create_node((char *)(t + 1));
                break;
            default:
                break;
        }
    }
}

void create_node(char *id)
{
    pid_t pid;
    char *argv[3] = {"client", id, NULL};
    
    printf("Processing Node\n");
    
    pid = fork();
    
    if(pid) {
        printf("pid is %d\n", pid);
        sym_insert(&stations, id, (sym_data_u){.pid = pid});
    }
    else {
        execvp("client", argv);
    }
}
