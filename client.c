#include "shared.h"

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

static int pipe_fd[2];

int main(int argc, char *argv[])
{
    if(argc != 3) {
        fprintf(stderr, "Client expects 3 parameters. Only receive %d.\n", argc);
        exit(EXIT_FAILURE);
    }
    sscanf(argv[2], "%d.%d", &pipe_fd[0], &pipe_fd[1]);
    printf("Successfully Started Station: %s\n", argv[1]);
    
    kill(getppid(), SIGUSR1);

    
    while(1) {
        pause();
    }
    
    return 0;
}