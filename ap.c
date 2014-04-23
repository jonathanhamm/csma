#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <math.h>
#include <assert.h>
#include <stdint.h>
#include <errno.h>

#include <zlib.h>
#include <termios.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include "parse.h"
#include "ap.h"
#include "shared.h"

#define CLIENT_PATH "./client"

typedef struct station_s station_s;

struct station_s
{
    pid_t pid;
    int pipe[2];
};

sym_table_s station_table;
pthread_mutex_t station_table_lock = PTHREAD_MUTEX_INITIALIZER;

static int shm_mediums;
static int shm_mediumc;

static void process_tasks(void);
static void create_node(char *id, char *ifs);
static void send_message(send_s *send);
static void *process_request(void *);
static void kill_child(station_s *s);
static void kill_childid(char *id);
static void send_ack(char *addr1);

static void sigUSR1(int sig);
static void sigUSR2(int sig);

int main(int argc, char *argv[])
{
    int c, status;
    char *src;
    buf_s *in;
    struct sigaction sa;
    sym_record_s *rec, *recb;
    pthread_t req_thread;
    sigset_t mask;
    
    if(argc > 1) {
        src = readfile(argv[1]);
        parse(src);
        closefile();
    }
    else {
        src = readfile("test");
        parse(src);
        closefile();
    }
    
    name = "ap";
    name_stripped = "ap";
    name_len = sizeof("ap")-1;
    logfile = stdout;
    
    sa.sa_handler = sigUSR1;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    status = sigaction(SIGUSR1, &sa, NULL);
    if(status < 0) {
        perror("Error installing handler for SIGUSR1");
        exit(EXIT_FAILURE);
    }

    sa.sa_handler = sigUSR2;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    status = sigaction(SIGUSR2, &sa, NULL);
    if(status < 0) {
        perror("Error installing handler for SIGUSR2");
        exit(EXIT_FAILURE);
    }
    
    sa.sa_handler = sigALARM;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    status = sigaction(SIGALRM, &sa, NULL);
    if(status < 0) {
        perror("Error installing handler for SIGTERM");
        exit(EXIT_FAILURE);
    }

    shm_mediums = shmget(SHM_KEY_S, sizeof(*mediums), IPC_CREAT|SHM_R|SHM_W);
    if(shm_mediums < 0) {
        perror("Failed to set up shared memory segment");
        exit(EXIT_FAILURE);
    }
    
    mediums = shmat(shm_mediums, NULL, 0);
    if(mediums == (medium_s *)-1) {
        perror("Failed to attached shared memory segment.");
        exit(EXIT_FAILURE);
    }
    mediums->isbusy = false;
    pthread_mutex_init(&mediums->lock, NULL);
    
    shm_mediumc = shmget(SHM_KEY_C, sizeof(*mediumc), IPC_CREAT|SHM_R|SHM_W);
    if(shm_mediumc < 0) {
        perror("Failed to set up shared memory segment");
        exit(EXIT_FAILURE);
    }
    
    mediumc = shmat(shm_mediumc, NULL, 0);
    if(mediumc == (medium_s *)-1) {
        perror("Failed to attached shared memory segment.");
        exit(EXIT_FAILURE);
    }
    mediumc->isbusy = false;
    pthread_mutex_init(&mediumc->lock, NULL);
    
    status = pthread_create(&req_thread, NULL, process_request, NULL);
    if(status) {
        perror("Failure to set up thread");
        exit(EXIT_FAILURE);
    }
    
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR2);
    status = pthread_sigmask(SIG_BLOCK, &mask, NULL);
    if(status) {
        perror("Failure to mask SIGUSR2 in parent thread.");
        exit(EXIT_FAILURE);
    }

    process_tasks();
    
    in = buf_init();
    
    /* Get command line input */
    printf("> ");
    while((c = getchar()) != EOF) {
        buf_addc(&in, c);
        if(c == '\n') {
            buf_addc(&in, '\0');
            parse(in->buf);
            process_tasks();
            buf_reset(&in);
            printf("> ");
        }
    }
    buf_free(in);
    
    pthread_mutex_lock(&station_table_lock);
    /* kill all children */
    for(c = 0; c < SYM_TABLE_SIZE; c++) {
        rec = station_table.table[c];
        while(rec) {
            kill_child(rec->data.ptr);
            recb = rec->next;
            free(rec);
            rec = recb;
        }
    }
    pthread_mutex_unlock(&station_table_lock);

    pthread_mutex_destroy(&station_table_lock);
    
    exit(EXIT_SUCCESS);
}

void process_tasks(void)
{
    task_s *t;
    
    while((t = task_dequeue())) {
        switch(t->func) {
            case FNET_NODE:
                create_node(*(char **)(t + 1), *((char **)(t + 1) + 1));
                break;
            case FNET_SEND:
                send_message((send_s *)t);
                break;
            case FNET_KILL:
                kill_childid(*(char **)(t + 1));
            default:
                break;
        }
        free(t);
    }
}

void create_node(char *id, char *ifs)
{
    int status;
    pid_t pid;
    char *argv[5];
    char fd_buf[4*sizeof(int)+4];
    int fd[2];
    station_s *station;
    
    pthread_mutex_lock(&station_table_lock);
    if(!sym_lookup(&station_table, id)) {
        status = pipe(fd);
        if(status < 0) {
            perror("Error Creating Pipe");
            exit(EXIT_FAILURE);
        }
        sprintf(fd_buf, "%d.%d", fd[0], fd[1]);
        argv[0] = CLIENT_PATH;
        argv[1] = id;
        argv[2] = ifs;
        argv[3] = fd_buf;
        argv[4] = NULL;
        
        pid = fork();
        if(pid) {
            /* wait for SIGUSR1 from child */
            pause();
            
            station = alloc(sizeof(*station));
            station->pid = pid;
            station->pipe[0] = fd[0];
            station->pipe[1] = fd[1];
            
            sym_insert(&station_table, id, (sym_data_u){.ptr = station});
        }
        else if(pid < 0) {
            perror("Failed to create station process.");
            exit(EXIT_FAILURE);
        }
        else {
            status = execv(CLIENT_PATH, argv);
        }
    }
    pthread_mutex_unlock(&station_table_lock);
}

void send_message(send_s *send)
{
    sym_record_s *rec;
    station_s *station;
    size_t dlen, plen;
    
    pthread_mutex_lock(&station_table_lock);
    rec = sym_lookup(&station_table, send->src);
    if(rec) {
        dlen = strlen(send->dst);
        plen = strlen(send->period);

        station = rec->data.ptr;
        write(station->pipe[1], &send->super.func, sizeof(send->super.func));
        write(station->pipe[1], &dlen, sizeof(dlen));
        write(station->pipe[1], send->dst, strlen(send->dst));
        write(station->pipe[1], &send->size, sizeof(send->size));
        write(station->pipe[1], send->payload, send->size);
        write(station->pipe[1], &plen, sizeof(plen));
        write(station->pipe[1], send->period, plen);
        write(station->pipe[1], &send->repeat, sizeof(send->repeat));
        kill(station->pid, SIGUSR1);
    }
    pthread_mutex_unlock(&station_table_lock);
}

void kill_child(station_s *s)
{
    close(s->pipe[0]);
    close(s->pipe[1]);
    kill(s->pid, SIGTERM);
}

void kill_childid(char *id)
{
    sym_record_s *rec;
    
    pthread_mutex_lock(&station_table_lock);
    rec = sym_lookup(&station_table, id);
    if(rec) {
        kill_child(rec->data.ptr);
        sym_delete(&station_table, id);
    }
    pthread_mutex_unlock(&station_table_lock);
}

void *process_request(void *arg)
{
    ssize_t status;
    union {
        rts_s rts;
        cts_ack_s ctack;
    }data;
    char *payload;
    uint32_t checksum;
    
    while(true) {
        mediums->size = 0;
        status = slowread(mediums, &data, sizeof(data));
        if(status == EINTR) {
            set_busy(mediums, false);
            logevent("Timed out session");
        }
        else {
            if(data.rts.FC & RTS_SUBTYPE) {
                checksum = (uint32_t)crc32(CRC_POLYNOMIAL, (Bytef *)&data.rts, sizeof(data.rts)-sizeof(uint32_t));
                if(checksum == data.rts.FCS) {
                    set_busy(mediums, true);
                    send_ack(data.rts.addr1);
                    payload = alloc(data.rts.D+1);
                    payload[data.rts.D] = '\0';
                    status = slowread(mediums, payload, data.rts.D);
                    if(status == EINTR) {
                        logevent("timed out waiting on payload");
                    }
                    else {
                        logevent("Got payload: %s", payload);
                        mediums->isbusy = false;
                    }
                }
                else {
                    logevent("Checksum Validation Failed");
                    set_busy(mediums, false);
                }
            }
            else {
                set_busy(mediums, false);
            }
        }
    }
}

void send_ack(char *addr1)
{
    cts_ack_s ack;
    
    ack.FC = ACK_SUBTYPE;
    ack.D = 1;
    memcpy(ack.addr1, addr1, sizeof(ack.addr1));
    ack.FCS = (uint32_t)crc32(CRC_POLYNOMIAL, (Bytef *)&ack, sizeof(ack)-sizeof(uint32_t));
    slowwrite(mediumc, &ack, sizeof(ack));
    logevent("Got Valid RTS and Sent ACK");
}


void sigUSR1(int sig)
{
}

void sigUSR2(int sig)
{
    puts("SIGUSR2 called");
}

void sigINT(int sig)
{
    
}


