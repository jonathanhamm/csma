#include "shared.h"
#include <stdarg.h>
#include <errno.h>
#include <assert.h>
#include <time.h>

#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>

FILE *logfile;
char *name;
char *name_stripped;
size_t name_len;
medium_s *mediums;
medium_s *mediumc;
pthread_t timer_thread;

static volatile sig_atomic_t timed_out;
static void *timer_threadf(void *arg);


bool addr_cmp(char *addr1, char *addr2)
{
    uint32_t *a32 = (uint32_t *)addr1,
             *b32 = (uint32_t *)addr2;
    uint16_t *a16 = (uint16_t *)(a32 + 1);
    uint16_t *b16 = (uint16_t *)(b32 + 1);
    
    return (*a32 == *b32) && (*a16 == *b16);
}

void *timer_threadf(void *arg)
{
    struct timespec ts;
    struct timeval tp;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    double tmp;
    timerarg_s *targ = arg;
    
    pthread_mutex_init(&lock, NULL);
    pthread_cond_init(&cond, NULL);
    
    gettimeofday(&tp, NULL);
    
    tmp = targ->time + tp.tv_sec + tp.tv_usec/1e6;
    ts.tv_sec = (long)tmp;
    ts.tv_nsec = (long)((tmp - ts.tv_sec)*1e9);
    
    pthread_mutex_lock(&lock);
    pthread_cond_timedwait(&cond, &lock, &ts);
    pthread_mutex_unlock(&lock);
    
    pthread_mutex_destroy(&lock);
    pthread_cond_destroy(&cond);
    
    pthread_kill(targ->sender, SIGALRM);
    
    free(arg);
    
    pthread_exit(NULL);
}

void start_timer(double time)
{
    int status;
    timerarg_s *t = alloc(sizeof(t));
    
    timed_out = 0;
    t->time = time;
    t->sender = pthread_self();
    status = pthread_create(&timer_thread, NULL, timer_threadf, t);
    if(status) {
        perror("Failed to create timer thread");
        exit(EXIT_FAILURE);
    }
}

size_t write_shm(medium_s *medium, char *data, size_t size)
{
    long i;

    for(i = medium->size; i < size+medium->size; i++)
        medium->buf[i % MEDIUM_SIZE] = *data++;
    
    return 0;
}

size_t read_shm(medium_s *medium, char *data, size_t start, size_t size)
{
    long i;
    
    for(i = start; i < start + size; i++) {
        if(timed_out)
            return EINTR;
        if(i >= medium->size) {
            while(i >= medium->size) {
                if(timed_out)
                    return EINTR;
            }
        }
        *data++ = medium->buf[i % MEDIUM_SIZE];
    }
    return 0;
}

void set_busy(medium_s *medium, bool isbusy)
{
    medium->isbusy = isbusy;
}

void sigALARM(int sig)
{
    timed_out = 1;
}

void logevent(char *fs, ...)
{
    va_list args;
    time_t t;
    struct tm tm_time;
    char timestamp[16];
    
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    
    pthread_mutex_lock(&lock);
    
    fprintf(logfile, "%.6s:", name_stripped);
    fputc('\t', logfile);
    
    time(&t);
    localtime_r(&t, &tm_time);
    strftime(timestamp, 16, "%T:\t", &tm_time);
    
    fprintf(logfile, "%s", timestamp);
    
    va_start(args, fs);
    vfprintf(logfile, fs, args);
    va_end(args);
    
    fputc('\n', logfile);
    
    fflush(logfile);
    
    pthread_mutex_unlock(&lock);
}

/* Make reads even "more" of a race condition */
ssize_t slowread(medium_s *medium, void *buf, size_t size)
{
    size_t i;
    ssize_t status;
    
    start_timer(WAIT_TIME);
    for(i = 0; i < size; i++) {
        status = read_shm(medium, buf+i, i, sizeof(char));
        if(status == EINTR) {
            pthread_cancel(timer_thread);
            return EINTR;
        }
        sched_yield();
    }
    pthread_cancel(timer_thread);
    return 0;
}

/* Make writes even "more" of a race condition */
void slowwrite(medium_s *medium, void *buf, size_t size)
{
    size_t i;
    
    medium->size = 0;
    for(i = 0; i < size; i++) {
        write_shm(medium, buf+i, sizeof(char));
        medium->size++;
        sched_yield();
    }
}

void *alloc(size_t size)
{
    void *ptr = malloc(size);
    if(!ptr){
        perror("Memory Allocation Error");
        exit(EXIT_FAILURE);
    }
    return ptr;
}

void *allocz(size_t size)
{
    void *ptr = calloc(size, 1);
    if(!ptr){
        perror("Memory Allocation Error");
        exit(EXIT_FAILURE);
    }
    return ptr;
}

void *ralloc(void *ptr, size_t size)
{
    ptr = realloc(ptr, size);
    if(!ptr){
        perror("Memory Allocation Error");
        exit(EXIT_FAILURE);
    }
    return ptr;
}