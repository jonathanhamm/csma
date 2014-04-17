#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#include <math.h>
#include <assert.h>
#include <stdint.h>

#include <signal.h>
#include <unistd.h>

#include "parse.h"
#include "network.h"

#define CLIENT_PATH "./client"


sym_table_s station_table;

//static volatile sig_atomic_t got_SIG
static int pipe_fd[2];
static void process_tasks(void);
static void create_node(char *id);
static uint32_t crc32(void *data, int size);

static void sigUSR1(int sig);
static void sigINT(int sig);

int main(int argc, char *argv[])
{
    int c, status;
    char *src;
    buf_s *in;
    struct sigaction sa;
    sym_record_s *rec, *recb;
    
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
    
    sa.sa_handler = sigUSR1;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    status = sigaction(SIGUSR1, &sa, NULL);
    if(status < 0) {
        perror("Error installing handler for SIGUSR1");
        exit(EXIT_FAILURE);
    }
    
    status = pipe(pipe_fd);
    if(status < 0) {
        perror("Error Creating Pipe");
        exit(EXIT_FAILURE);
    }
        
    process_tasks();
    
    in = buf_init();
    
    /* Get command line input */
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
    
    /* Slay all children */
    for(c = 0; c < SYM_TABLE_SIZE; c++) {
        rec = station_table.table[c];
        while(rec) {
            kill(rec->data.pid, SIGTERM);
            recb = rec->next;
            free(rec);
            rec = recb;
        }
    }
    exit(EXIT_SUCCESS);
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
    
    if(!sym_lookup(&station_table, id)) {
        sprintf(fd_buf, "%d.%d", pipe_fd[0], pipe_fd[1]);
        argv[0] = CLIENT_PATH;
        argv[1] = id;
        argv[2] = fd_buf;
        argv[3] = NULL;
        
        pid = fork();
        if(pid) {
            pause();
            sym_insert(&station_table, id, (sym_data_u){.pid = pid});
        }
        else if(pid < 0) {
            perror("Failed to create station process.");
            exit(EXIT_FAILURE);
        }
        else {
            status = execv(CLIENT_PATH, argv);
        }
    }
}

uint32_t crc32(void *data, int size)
{
    uint8_t *d = data;
    uint32_t sum = 1;
    
    return sum;
}

void sigUSR1(int sig)
{
}

void sigINT(int sig)
{
    
}


