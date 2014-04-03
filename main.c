#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#define SH_SIZE 4096
#define SH_KEY 8888

#define N_CLIENTS 10

static int shmid;
static void *shptr;

void server(void);
void client(int i);

int main(void)
{
    int i;
    pid_t pid;
    
    shmid = shmget(SH_KEY, SH_SIZE, IPC_CREAT | 0666);
    if(shmid < 0) {
        perror("Failed To Create Shared Memory Segment");
        exit(EXIT_FAILURE);
    }
    
    shptr = shmat(shmid, NULL, 0);
    if(shptr == (void *)-1) {
        perror("Client Failed to Attach Shared Memory Segment");
        exit(EXIT_FAILURE);
    }
    
    for(i = 0; i < N_CLIENTS; i++) {
        pid = fork();
        if(pid) {
        }
        else {
            client(i);
            exit(EXIT_SUCCESS);
        }
    }
    sleep(3);
    printf("final %c\n", *(char *)shptr);
}

void server(void)
{
}

void client(int i)
{
}