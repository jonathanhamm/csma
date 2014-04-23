#include "shared.h"
#include <stdarg.h>
#include <time.h>

#include <sys/time.h>

FILE *logfile;
char *name;
char *name_stripped;
size_t name_len;
volatile sig_atomic_t timed_out;

static void *timer_thread(void *arg);


bool addr_cmp(char *addr1, char *addr2)
{
    uint32_t *a32 = (uint32_t *)addr1,
             *b32 = (uint32_t *)addr2;
    uint16_t *a16 = (uint16_t *)(a32 + 1);
    uint16_t *b16 = (uint16_t *)(b32 + 1);
    
    return (*a32 == *b32) && (*a16 == *b16);
}

void *timer_thread(void *arg)
{
    struct timespec ts;
    struct timeval tp;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    double tmp;
    timerarg_s *targ = arg;
    
    pthread_mutex_init(&lock, NULL);
    pthread_cond_init(&cond, NULL);
    
    timed_out = false;
    
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
    pthread_t timer;
    
    t->time = time;
    t->sender = pthread_self();
    status = pthread_create(&timer, NULL, timer_thread, t);
    if(status) {
        perror("Failed to create timer thread");
        exit(EXIT_FAILURE);
    }
    while(timed_out);
}

void sigALARM(int sig)
{
    timed_out = true;
}

void logevent(char *fs, ...)
{
    va_list args;
    size_t i, diff;
    time_t t;
    struct tm tm_time;
    char timestamp[16];
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    
    pthread_mutex_lock(&lock);
    
    fprintf(logfile, "%6s", name_stripped);
    diff = 6 - (name_len - 2);
    for(i = 0; i < diff; i++)
        fputc('.', logfile);
    fputc(' ', logfile);
    
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