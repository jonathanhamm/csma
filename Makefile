-all:
	gcc -ggdb -lm -pthread -fno-strict-aliasing network.c parse.c -o csma
