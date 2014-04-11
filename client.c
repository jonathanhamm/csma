#include "shared.h"

#include <stdio.h>
#include <signal.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    printf("Successfully Started: %s\n", argv[1]);
    kill(getppid(), SIGCHLD);
    return 0;
}