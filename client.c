#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <stdarg.h>
#include <assert.h>
#include <errno.h>

#include <zlib.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/time.h>
#include <sys/stat.h>

#include "shared.h"

#define REDIRECT_OUTPUT

#define TIMER_TIME 0.5

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

static int tasks[2];
static int shm_mediums;
static int shm_mediumc;
static struct timespec ifs;
static pthread_t main_thread;
static volatile sig_atomic_t pipe_full;

static void parse_send(void);
static void parse_receive(void);
static void *send_thread(void *arg);
static void doCSMACA(send_s *s);
static void sendRTS(send_s *s);
static void send_frame(send_s *s);
static bool check_ack_cts(cts_ack_s *data);

static void sigUSR1(int sig);
static void sigTERM(int sig);

int main(int argc, char *argv[])
{
    int status;
    funcs_e f;
    ssize_t rstatus;
    double ifs_d;
    struct sigaction sa;
    char outfile[16] = "out/";
    
    if(argc != 4) {
        fprintf(stderr, "Client expects 3 parameters. Only received %d.\n", argc);
        exit(EXIT_FAILURE);
    }

    name = argv[1];
    name_len = strlen(name);
    
    /* Strip out quotes from node name and initialize it */
    name_stripped = alloc(name_len - 2);
    strncpy(name_stripped, &name[1], name_len-2);
    
    /* Create file for logging */
    if(access("out/", F_OK)) {
        if(errno == ENOENT)
            mkdir("out", S_IRWXU);
        else {
            perror("Directory Access");
            exit(EXIT_FAILURE);
        }
    }
    
    strcpy(&outfile[4], name_stripped);
    logfile = fopen(outfile, "w");
    if(!logfile) {
        perror("Error Creating file for redirection");
        exit(EXIT_FAILURE);
    }
    
    /* set ifs time for this station */
    ifs_d = strtod(argv[2], NULL);
    ifs.tv_sec = (long)ifs_d;
    ifs.tv_nsec = (long)((ifs_d - ifs.tv_sec)*1e9);
    
    /* get thread 'id' */
    main_thread = pthread_self();
    
    /* Get pipes so this process can communicate with command line process */
    sscanf(argv[3], "%d.%d", &tasks[0], &tasks[1]);
    
    /* Set up handler for SIGUSR1 */
    sa.sa_handler = sigUSR1;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    status = sigaction(SIGUSR1, &sa, NULL);
    if(status < 0) {
        perror("Error installing handler for SIGUSR1");
        exit(EXIT_FAILURE);
    }

    /* Handler for releasing some resources on SIGTERM */
    sa.sa_handler = sigTERM;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    status = sigaction(SIGTERM, &sa, NULL);
    if(status < 0) {
        perror("Error installing handler for SIGTERM");
        exit(EXIT_FAILURE);
    }
    
    /* Handler for SIGALRM used in timer */
    sa.sa_handler = sigALARM;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    status = sigaction(SIGALRM, &sa, NULL);
    if(status < 0) {
        perror("Error installing handler for SIGTERM");
        exit(EXIT_FAILURE);
    }
    
    /* Shared memory used for medium to send to access point */
    shm_mediums = shmget(SHM_KEY_S, sizeof(char), SHM_R);
    if(shm_mediums < 0) {
        perror("Failed to locate shared memory segment.");
        exit(EXIT_FAILURE);
    }
    
    mediums = shmat(shm_mediums, NULL, 0);
    if(mediums == (medium_s *)-1) {
        perror("Failed to attached shared memory segment.");
        exit(EXIT_FAILURE);
    }
    
    /* Shared memory used for medium to receive from access point */
    shm_mediumc = shmget(SHM_KEY_C, sizeof(char), SHM_R);
    if(shm_mediumc < 0) {
        perror("Failed to locate shared memory segment.");
        exit(EXIT_FAILURE);
    }
    
    mediumc = shmat(shm_mediumc, NULL, 0);
    if(mediumc == (medium_s *)-1) {
        perror("Failed to attached shared memory segment.");
        exit(EXIT_FAILURE);
    }
    
    printf("Successfully Started Station: %s\n", name);
    
    /* Notify parent process */
    kill(getppid(), SIGUSR1);
    
    /* Wait for commands from command line process */
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
                    case FNET_RECEIVE:
                        parse_receive();
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

    exit(EXIT_SUCCESS);
}

/* 
 Parses a message from command line process
 that creates a sending thread. 
 */
void parse_send(void)
{
    int status;
    send_s *s = alloc(sizeof(*s));
    
    read(tasks[0], &s->dlen, sizeof(s->dlen));
    
    s->dst = alloc(s->dlen+1);
    s->dst[s->dlen] = '\0';
    read(tasks[0], s->dst, s->dlen);
    
    read(tasks[0], &s->size, sizeof(s->size));
    s->payload = alloc(s->size+sizeof(uint32_t)+1);
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

/* Receive a message and display it */
void parse_receive(void)
{
    size_t size;
    char *payload;
    char src[6];
    
    read(tasks[0], &size, sizeof(size));
    
    payload = alloc(size+1);
    payload[size] = '\0';
    
    read(tasks[0], payload, size);
    read(tasks[0], src, sizeof(src));
    
    logevent("Received Message %s from %.6s", payload, src);
    free(payload);
}

/* Thread For Proccesses Attempting to Send */
void *send_thread(void *arg)
{
    send_s *s = arg;
    struct timespec t;
    double wait_d = strtod(s->period, NULL);
    double actual;
    
    /* seed random number generator */
    srand((int)time(NULL));
    
    do {
        actual = wait_d * (double)rand()/(RAND_MAX);
        t.tv_sec = (long)actual;
        t.tv_nsec = (long)((actual - t.tv_sec)*1e9);
        nanosleep(&t, NULL);
        doCSMACA(s);
    }
    while(s->repeat);
    
    free(s->dst);
    free(s->period);
    free(s->payload);
    free(s);
        
    pthread_exit(NULL);
}

/* Main CSMA/CA Function */
void doCSMACA(send_s *s)
{
    ssize_t status;
    struct timespec ts;
    cts_ack_s ackcts;
    int K = 0, R;
    
    while(K != 32) {
        /* wait until idle and waste tons of cycles in the process */
        while(mediums->isbusy)
            sched_yield();
        
        /* wait ifs time */
        nanosleep(&ifs, NULL);
        
        if(mediums->isbusy)
            continue;
        
        /* pick random number between 0 and 2^K - 1 */
        R = rand() % (1 << K);
        
        /* Send Request to send */
        sendRTS(s);
                
        status = slowread(mediumc, &ackcts, sizeof(ackcts));
        /* if not timed out timed out */
        if(status != EINTR)
        if(ackcts.FC & CTS_SUBTYPE) {
            if(check_ack_cts(&ackcts)) {
                mediumc->size = 0;
                logevent("GOT CTS");
                
                /* wait ifs time */
                nanosleep(&ifs, NULL);
                                
                send_frame(s);
                status = slowread(mediumc, &ackcts, sizeof(ackcts));
                if(status != EINTR)
                if(ackcts.FC & ACK_SUBTYPE) {
                    if(check_ack_cts(&ackcts)) {
                        mediumc->size = 0;
                        logevent("Got Ack");
                        return;
                    }
                }
            }
            logevent("Timed out: K is now: %d and R is: %d", K, R);
            K++;
            ts.tv_nsec = R*TIME_SLOT;
            ts.tv_sec = 0;
            nanosleep(&ts, NULL);
        }
    }
    logevent("Number of attempts exceeded 32");
}

/* Send Request To Send */
void sendRTS(send_s *s)
{
    rts_s frame = {0};
    
    frame.FC = RTS_SUBTYPE;
    frame.D = s->size;
    
    memcpy(frame.addr1, name_stripped, name_len-2);
    memcpy(frame.addr2, &s->dst[1], s->dlen-2);
    
    frame.FCS = (uint32_t)crc32(CRC_POLYNOMIAL, (Bytef *)&frame, sizeof(frame)-sizeof(uint32_t));
    
    slowwrite(mediums, &frame, sizeof(frame));
    logevent("%s sent RTS", name_stripped);
}

/* Send Payload */
void send_frame(send_s *s)
{
    uint32_t *checkptr;
    
    checkptr = (uint32_t *)&s->payload[s->size];
    *checkptr = (uint32_t)crc32(CRC_POLYNOMIAL, (Bytef *)s->payload, (int)s->size);
     
    slowwrite(mediums, s->payload, s->size + sizeof(uint32_t));
    logevent("Sent Payload");
}

/* Check if CTS or ACK are valid */
bool check_ack_cts(cts_ack_s *data)
{
    uint32_t checksum;
    
    if(addr_cmp(name_stripped, data->addr1)) {
        checksum = (uint32_t)crc32(CRC_POLYNOMIAL, (Bytef *)data, sizeof(*data)-sizeof(uint32_t));
        if(checksum == data->FCS)
            return true;
    }
    return false;
}

/* Signal Handlers */

void sigUSR1(int sig)
{
    pipe_full = 1;
}

void sigTERM(int sig)
{
    free(name_stripped);
    fclose(logfile);
    exit(EXIT_SUCCESS);
}
