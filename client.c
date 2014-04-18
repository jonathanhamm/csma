#include "shared.h"

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

static int medium[2];
static int tasks[2];
static char *name;

static void sigUSR1(int sig);

int main(int argc, char *argv[])
{
    int status;
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
    name = argv[1];
    printf("Successfully Started Station: %s\n", name);
    
    kill(getppid(), SIGUSR1);
    
    while(1) {
        pause();
    }
    
    return 0;
}

void sigUSR1(int sig)
{
    printf("SIGUSR1 called from %s\n", name);
    fflush(stdout);
}
