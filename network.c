#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#include <math.h>

#include <pthread.h>

#include "parse.h"
#include "network.h"

int main(int argc, char *argv[])
{
    buf_s *in = buf_init();
    
    if(argc > 1) {
        
    }
    parse("test");

}