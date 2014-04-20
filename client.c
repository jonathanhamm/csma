#include "shared.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>

#include <signal.h>
#include <unistd.h>
#include <pthread.h>

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

static volatile sig_atomic_t pipe_full;
static void sigUSR1(int sig);
static void sigTERM(int sig);
static void parse_send(void);
static void *send_thread(void *);

int main(int argc, char *argv[])
{
    int status;
    funcs_e f;
    ssize_t rstatus;
    struct sigaction sa;
    
    if(argc != 3) {
        fprintf(stderr, "Client expects 3 parameters. Only receive %d.\n", argc);
        exit(EXIT_FAILURE);
    }
    sscanf(argv[2], "%d.%d.%d.%d", &medium[0], &medium[1], &tasks[0], &tasks[1]);
    
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
    
    name = argv[1];
    name_len = strlen(name);
    printf("Successfully Started Station: %s\n", name);
    
    kill(getppid(), SIGUSR1);
    
    while(1) {
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
    
    do {
        
    }
    while(s->repeat);
    
    free(s->dst);
    free(s->period);
    free(s->payload);
    free(s);
    
    pthread_exit(NULL);
}

void sigUSR1(int sig)
{
    pipe_full = 1;
}

void sigTERM(int sig)
{
    exit(EXIT_SUCCESS);
}

