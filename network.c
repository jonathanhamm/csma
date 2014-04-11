#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#include <math.h>

#include <signal.h>
#include <unistd.h>

#include "parse.h"
#include "network.h"

int main(int argc, char *argv[])
{
    int c;
    char *src;
    buf_s *in;
    
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
    
    in = buf_init();
    
    printf("> ");
    while((c = getchar()) != EOF) {
        buf_addc(&in, c);
        if(c == '\n') {
            buf_addc(&in, '\0');
            parse(in->buf);
            buf_reset(&in);
            printf("> ");
        }
    }
    buf_free(in);
}