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


#define TIMER_TIME 1.5

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
static pthread_t timer;
static pthread_t main_thread;
static volatile sig_atomic_t pipe_full;
static volatile sig_atomic_t timed_out;

static void parse_send(void);
static void *send_thread(void *);
static void doCSMACA(send_s *s);
static void sendRTS(send_s *s);
static void *timer_thread(void *);
static void logevent(char *, ...);

static void sigUSR1(int sig);
static void sigTERM(int sig);
static void sigALARM(int sig);

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
    
    main_thread = pthread_self();
    
    sscanf(argv[3], "%d.%d.%d.%d", &medium[0], &medium[1], &tasks[0], &tasks[1]);
    
    status = pthread_create(&timer, NULL, timer_thread, NULL);
    if(status) {
        perror("Failed to create timer thread");
        exit(EXIT_FAILURE);
    }
    
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
        perror("Error installing handler for SIGTERM");
        exit(EXIT_FAILURE);
    }
    
    sa.sa_handler = sigALARM;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    status = sigaction(SIGALRM, &sa, NULL);
    if(status < 0) {
        perror("Error installing handler for SIGALRM");
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
    int status;
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
    
    status = pthread_create(&s->thread, NULL, send_thread, s);
    if(status) {
        perror("Failed to create thread for sending.");
        exit(EXIT_FAILURE);
    }
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
    
    pthread_kill(timer, SIGALRM);
   // while(read(medium, <#void *#>, <#size_t#>))
}

void sendRTS(send_s *s)
{
    rts_s frame;
    size_t size = RTS_SIZE + CTS_ACK_SIZE + s->size;
    char *fptr = (char *)&frame;
    
    frame.FC = RTS_SUBTYPE;
    frame.D = (size * 1000000) / BPS + !!((size * 1000000) % BPS);
    
    memcpy(frame.addr1, name, name_len);
    memcpy(frame.addr1, &s->dst[1], s->dlen-2);
    
    frame.FCS = (uint32_t)crc32(CRC_POLYNOMIAL, (Bytef *)&frame, sizeof(frame)-sizeof(uint32_t));
    
    while(fptr - (char *)&frame < sizeof(frame)) {
        write(medium[1], fptr, 1);
        fptr++;
        sched_yield(); /* make this even "more" of a race condition */
    }
    logevent("%s sent RTS", name_stripped);
}

void *timer_thread(void *arg)
{
    struct timespec ts;
    struct timeval tp;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    double tmp;
    
    pthread_mutex_init(&lock, NULL);
    pthread_cond_init(&cond, NULL);

    
    pthread_mutex_lock(&lock);
    
    while(true) {
        pause();
        timed_out = 0;
        
        gettimeofday(&tp, NULL);
        
        tmp = TIMER_TIME + tp.tv_sec + tp.tv_usec/1e6;
        ts.tv_sec = (long)tmp;
        ts.tv_nsec = (long)((tmp - ts.tv_sec)*1e9);

        pthread_cond_timedwait(&cond, &lock, &ts);
        
        pthread_kill(main_thread, SIGALRM);
    }
}

void logevent(char *fs, ...)
{
    va_list args;
    time_t t;
    struct tm tm_time;
    char timestamp[16];
    
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

void sigALARM(int sig)
{
    timed_out = 1;
    puts("Got sig alarm");
}

