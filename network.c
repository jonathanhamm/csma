#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#include <math.h>

#include <signal.h>
#include <unistd.h>

#include "parse.h"
#include "network.h"

static sym_table_s active;

static void process_tasks(void);

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
        switch(t->func) {
            case FNET_NODE:
                
            default:
                break;
        }
    }
}
