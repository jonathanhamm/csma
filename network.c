#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#include <math.h>

#include <signal.h>
#include <unistd.h>

#include "parse.h"
#include "network.h"

#define CLIENT_PATH "./client"

//static volatile sig_atomic_t got_SIG
static int pipe_fd[2];
static sym_table_s stations;
static void process_tasks(void);
static void create_node(char *id);

int main(int argc, char *argv[])
{
    int c, status;
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
    
    status = pipe(pipe_fd);
    if(status < 0) {
        perror("Error Creating Pipe");
        exit(EXIT_FAILURE);
    }
        
    process_tasks();
    
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
        switch(t->func) {
            case FNET_NODE:
                create_node(*(char **)(t + 1));
                break;
            default:
                break;
        }
        free(t);
    }
}

void create_node(char *id)
{
    int status;
    pid_t pid;
    char *argv[4];
    char fd_buf[2*sizeof(int)+2];
    
    sprintf(fd_buf, "%d.%d", pipe_fd[0], pipe_fd[1]);
    argv[0] = CLIENT_PATH;
    argv[1] = id;
    argv[2] = fd_buf;
    argv[3] = NULL;
    
    pid = fork();
    
    if(pid) {
        sym_insert(&stations, id, (sym_data_u){.pid = pid});
    }
    else if(pid < 0) {
        perror("Failed to create station process.");
        exit(EXIT_FAILURE);
    }
    else {
        status = execv(CLIENT_PATH, argv);
        
    }
}
