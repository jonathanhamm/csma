#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <stdarg.h>
#include <errno.h>

#include <zlib.h>
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
static char *name, *name_stripped;
static size_t name_len;
static int shm_medium;
static char *medium_busy;
static struct timespec ifs;
static FILE *logf;

static volatile sig_atomic_t pipe_full;
static void sigUSR1(int sig);
static void sigTERM(int sig);
static void parse_send(void);
static void *send_thread(void *);
static void doCSMACA(send_s *s);
static void sendRTS(send_s *s);
static void logevent(char *, ...);

int main(int argc, char *argv[])
{
    int status;
    funcs_e f;
    ssize_t rstatus;
    double ifs_d;
    struct sigaction sa;
    
    logf = stdout;
    
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
    
    medium_busy = shmat(shm_medium, NULL, 0);
    if(medium_busy == (char *)-1) {
        perror("Failed to attached shared memory segment.");
        exit(EXIT_FAILURE);
    }
    
    name = argv[1];
    name_len = strlen(name);
    
    name_stripped = alloc(name_len - 2);
    strncpy(name_stripped, &name[1], name_len-2);
    
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
    free(name_stripped);
    exit(EXIT_SUCCESS);
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
    int K = 0, R;
    
not_idle:
    /* wait until idle and waste tons of cycles in the process */
    while(*medium_busy);
    
    /* wait ifs time */
    nanosleep(&ifs, NULL);
    
    if(*medium_busy)
        goto not_idle;
    
    /* pick random number between 0 and 2^k - 1 */
    R = rand() % (1 << K);
    
    sendRTS(s);
    
}

void sendRTS(send_s *s)
{
#define RTS_SUBTYPE 0x0b00;
    frame_s frame = {0};
    char *fptr = (char *)&frame;
    
    frame.FC = RTS_SUBTYPE;
    
    memcpy(frame.addr1, name, name_len);
    memcpy(frame.addr1, s->dst, s->dlen);
    
    frame.FCS = (uint32_t)crc32(CRC_POLYNOMIAL, (Bytef *)&frame, sizeof(frame)-sizeof(uint32_t));
    
    while(fptr - (char *)&frame < sizeof(frame)) {
        write(medium[1], fptr, 1);
        fptr++;
        sched_yield(); /* make this even "more" of a race condition */
    }
    logevent("%s sent RTS", name_stripped);
}

void logevent(char *fs, ...)
{
    va_list args;
    time_t t;
    struct tm tm_time;
    char timestamp[16] = {0};
    
    time(&t);
    localtime_r(&t, &tm_time);
    strftime(timestamp, 16, "%T:\t", &tm_time);
    
    fprintf(logf, "%s", timestamp);
    
    va_start(args, fs);
    vfprintf(logf, fs, args);
    va_end(args);

    putchar('\n');
}

void sigUSR1(int sig)
{
    pipe_full = 1;
}

void sigTERM(int sig)
{
    exit(EXIT_SUCCESS);
}

