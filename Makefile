-all:
	gcc -ggdb -fno-strict-aliasing client.c -o client
	gcc -ggdb -lm -pthread -fno-strict-aliasing network.c parse.c -o csma
