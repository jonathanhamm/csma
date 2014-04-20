#include "shared.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>

#include <signal.h>
#include <unistd.h>

static int medium[2];
static int tasks[2];
static char *name;

static volatile sig_atomic_t pipe_full;

static void sigUSR1(int sig);
static void sigTERM(int sig);

static void parse_send(void);

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
    bool repeat;
    size_t dlen, plen, size;
    char *dst, *payload, *period;
    
    read(tasks[0], &dlen, sizeof(dlen));
    
    dst = alloc(dlen+1);
    dst[dlen] = '\0';
    read(tasks[0], dst, dlen);
    
    read(tasks[0], &size, sizeof(size));
    payload = alloc(size+1);
    payload[size] = '\0';
    read(tasks[0], payload, size);
    
    read(tasks[0], &plen, sizeof(plen));
    period = alloc(plen+1);
    period[plen] = '\0';
    read(tasks[0], period, plen);
    
    read(tasks[0], &repeat, sizeof(repeat));
    
    printf("Processed a send with paylod: %s\n", payload);
    kill(getppid(), SIGUSR2);
}

void sigUSR1(int sig)
{
    pipe_full = 1;
}

void sigTERM(int sig)
{
    exit(EXIT_SUCCESS);
}

