#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>

#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/time.h>

#include "shared.h"

typedef struct send_s send_s;

struct send_s
{
    pthread_t thread;
    size_t dlen;
    char *dst;
    size_t size;
    char *payload;
    size_t plen;
    char *period;
    bool repeat;
};

static int medium[2];
static int tasks[2];
static char *name;
static size_t name_len;
static int shm_medium;
static char *medium_status;
static struct timespec ifs;

static volatile sig_atomic_t pipe_full;
static void sigUSR1(int sig);
static void sigTERM(int sig);
static void parse_send(void);
static void *send_thread(void *);
static void doCSMACA(send_s *s);

int main(int argc, char *argv[])
{
    int status;
    funcs_e f;
    ssize_t rstatus;
    double ifs_d;
    struct sigaction sa;
    
    if(argc != 4) {
        fprintf(stderr, "Client expects 3 parameters. Only receive %d.\n", argc);
        exit(EXIT_FAILURE);
    }

    /* seed random number generator */
    srand((int)time(NULL));
    
    /* set ifs time for this station */
    ifs_d = strtod(argv[2], NULL);
    ifs.tv_sec = (long)ifs_d;
    ifs.tv_nsec = (long)((ifs_d - ifs.tv_sec)*1e9);
    
    sscanf(argv[3], "%d.%d.%d.%d", &medium[0], &medium[1], &tasks[0], &tasks[1]);
    
    sa.sa_handler = sigUSR1;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    status = sigaction(SIGUSR1, &sa, NULL);
    if(status < 0) {
        perror("Error installing handler for SIGUSR1");
        exit(EXIT_FAILURE);
    }

    sa.sa_handler = sigTERM;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    status = sigaction(SIGTERM, &sa, NULL);
    if(status < 0) {
        perror("Error installing handler for SIGUSR1");
        exit(EXIT_FAILURE);
    }
    
    shm_medium = shmget(SHM_KEY, sizeof(char), IPC_R);
    if(shm_medium < 0) {
        perror("Failed to locate shared memory segment.");
        exit(EXIT_FAILURE);
    }
    
    medium_status = shmat(shm_medium, NULL, 0);
    if(medium_status == (char *)-1) {
        perror("Failed to attached shared memory segment.");
        exit(EXIT_FAILURE);
    }
    
    name = argv[1];
    name_len = strlen(name);
    printf("Successfully Started Station: %s\n", name);
    
    kill(getppid(), SIGUSR1);
    
    while(true) {
        if(pipe_full) {
            rstatus = read(tasks[0], &f, sizeof(f));
            if(rstatus < sizeof(f)) {
                if(rstatus == EAGAIN) {
                    pipe_full = 0;
                }
            }
            else {
                switch(f) {
                    case FNET_SEND:
                        parse_send();
                        break;
                    default:
                        fprintf(stderr, "Unknown Data Type Send %d\n", f);
                        break;
                }
            }
        }
        else {
            pause();
        }
    }
    return 0;
}

void parse_send(void)
{
    send_s *s = alloc(sizeof(*s));
    
    read(tasks[0], &s->dlen, sizeof(s->dlen));
    
    s->dst = alloc(s->dlen+1);
    s->dst[s->dlen] = '\0';
    read(tasks[0], s->dst, s->dlen);
    
    read(tasks[0], &s->size, sizeof(s->size));
    s->payload = alloc(s->size+1);
    s->payload[s->size] = '\0';
    read(tasks[0], s->payload, s->size);
    
    read(tasks[0], &s->plen, sizeof(s->plen));
    s->period = alloc(s->plen+1);
    s->period[s->plen] = '\0';
    read(tasks[0], s->period, s->plen);
    
    read(tasks[0], &s->repeat, sizeof(s->repeat));
    
    pthread_create(&s->thread, NULL, send_thread, s);
    
}

void *send_thread(void *arg)
{
    send_s *s = arg;
    struct timespec time;
    double wait_d = strtod(s->period, NULL);

    time.tv_sec = (long)wait_d;
    time.tv_nsec = (long)((wait_d - time.tv_sec)*1e9);
    
    do {
        nanosleep(&time, NULL);
        doCSMACA(s);
    }
    while(s->repeat);
    
    free(s->dst);
    free(s->period);
    free(s->payload);
    free(s);
    
    pthread_exit(NULL);
}

void doCSMACA(send_s *s)
{
    int K = 0;
    
not_idle:
    while(*medium_status);
    
    //wait IFS time
    nanosleep(&ifs, NULL);
    
    if(*medium_status)
        goto not_idle;
    
    
}


void sigUSR1(int sig)
{
    pipe_full = 1;
}

void sigTERM(int sig)
{
    exit(EXIT_SUCCESS);
}

