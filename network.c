#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#include <math.h>

#include <signal.h>

#include "parse.h"
#include "network.h"

int main(int argc, char *argv[])
{
    int c;
    char *src;
    buf_s *in;
    char buf[100] = {0};
    
    if(argc > 1) {
        parse(argv[1]);
    }
        
    parse("test");
    
    in = buf_init();
    return 0;
    
    printf("> ");
    while((c = getchar()) != EOF) {
        buf_addc(&in, c);
        if(c == '\n') {
            buf_addc(&in, '\0');
            buf_reset(&in);
            printf("> ");
        }
    }
    buf_free(in);
}