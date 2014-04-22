#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <math.h>
#include <assert.h>
#include <stdint.h>

#include <zlib.h>
#include <termios.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include "parse.h"
#include "ap.h"
#include "shared.h"

#define CLIENT_PATH "./client"

typedef struct station_s station_s;

struct station_s
{
    pid_t pid;
    int pipe[2];
};

sym_table_s station_table;
pthread_mutex_t station_table_lock = PTHREAD_MUTEX_INITIALIZER;

static int shm_medium;
static char *medium_status;
static int pipe_fd[2];

static void process_tasks(void);
static void create_node(char *id, char *ifs);
static void send_message(send_s *send);
static void *process_request(void *);
static void kill_child(station_s *s);
static void kill_childid(char *id);

static void sigUSR1(int sig);
static void sigUSR2(int sig);
static void sigINT(int sig);

int main(int argc, char *argv[])
{
    int c, status;
    char *src;
    buf_s *in;
    struct sigaction sa;
    sym_record_s *rec, *recb;
    pthread_t req_thread;
    sigset_t mask;
    
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

    sa.sa_handler = sigUSR2;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    status = sigaction(SIGUSR2, &sa, NULL);
    if(status < 0) {
        perror("Error installing handler for SIGUSR2");
        exit(EXIT_FAILURE);
    }
    
    status = pipe(pipe_fd);
    if(status < 0) {
        perror("Error Creating Pipe");
        exit(EXIT_FAILURE);
    }
    
    shm_medium = shmget(SHM_KEY, sizeof(char), IPC_CREAT|IPC_R|IPC_W);
    if(shm_medium < 0) {
        perror("Failed to set up shared memory segment");
        exit(EXIT_FAILURE);
    }
    
    medium_status = shmat(shm_medium, NULL, 0);
    if(medium_status == (char *)-1) {
        perror("Failed to attached shared memory segment.");
        exit(EXIT_FAILURE);
    }
    *medium_status = '\0';
    
    status = pthread_create(&req_thread, NULL, process_request, NULL);
    if(status) {
        perror("Failure to set up thread");
        exit(EXIT_FAILURE);
    }
    
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR2);
    status = pthread_sigmask(SIG_BLOCK, &mask, NULL);
    if(status) {
        perror("Failure to mask SIGUSR2 in parent thread.");
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
    
    /* kill all children */
    for(c = 0; c < SYM_TABLE_SIZE; c++) {
        rec = station_table.table[c];
        while(rec) {
            kill_child(rec->data.ptr);
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
                create_node(*(char **)(t + 1), *((char **)(t + 1) + 1));
                break;
            case FNET_SEND:
                send_message((send_s *)t);
                break;
            case FNET_KILL:
                kill_childid(*(char **)(t + 1));
            default:
                break;
        }
        free(t);
    }
}

void create_node(char *id, char *ifs)
{
    int status;
    pid_t pid;
    char *argv[5];
    char fd_buf[4*sizeof(int)+4];
    int fd[2];
    station_s *station;
    
    if(!sym_lookup(&station_table, id)) {
        status = pipe(fd);
        if(status < 0) {
            perror("Error Creating Pipe");
            exit(EXIT_FAILURE);
        }
        sprintf(fd_buf, "%d.%d.%d.%d", pipe_fd[0], pipe_fd[1], fd[0], fd[1]);
        argv[0] = CLIENT_PATH;
        argv[1] = id;
        argv[2] = ifs;
        argv[3] = fd_buf;
        argv[4] = NULL;
        
        pid = fork();
        if(pid) {
            /* wait for SIGUSR1 from child */
            pause();
            
            station = alloc(sizeof(*station));
            station->pid = pid;
            station->pipe[0] = fd[0];
            station->pipe[1] = fd[1];
            
            pthread_mutex_lock(&station_table_lock);
            sym_insert(&station_table, id, (sym_data_u){.ptr = station});
            pthread_mutex_unlock(&station_table_lock);
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

void send_message(send_s *send)
{
    sym_record_s *rec;
    station_s *station;
    size_t dlen, plen;
    
    rec = sym_lookup(&station_table, send->src);
    if(rec) {
        dlen = strlen(send->dst);
        plen = strlen(send->period);

        station = rec->data.ptr;
        write(station->pipe[1], &send->super.func, sizeof(send->super.func));
        write(station->pipe[1], &dlen, sizeof(dlen));
        write(station->pipe[1], send->dst, strlen(send->dst));
        write(station->pipe[1], &send->size, sizeof(send->size));
        write(station->pipe[1], send->payload, send->size);
        write(station->pipe[1], &plen, sizeof(plen));
        write(station->pipe[1], send->period, plen);
        write(station->pipe[1], &send->repeat, sizeof(send->repeat));
        kill(station->pid, SIGUSR1);
    }
}

void kill_child(station_s *s)
{
    close(s->pipe[0]);
    close(s->pipe[1]);
    kill(s->pid, SIGTERM);
}

void kill_childid(char *id)
{
    sym_record_s *rec;
    
    rec = sym_lookup(&station_table, id);
    if(rec)
        kill_child(rec->data.ptr);
}

void *process_request(void *arg)
{
    while(true) {
        sleep(1);
        *medium_status = rand()%2;
    }
}

void sigUSR1(int sig)
{
}

void sigUSR2(int sig)
{
    puts("SIGUSR2 called");
}

void sigINT(int sig)
{
    
}


