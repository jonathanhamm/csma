-all:
	gcc -ggdb -lm -pthread -fno-strict-aliasing shared.c client.c -o client
	gcc -ggdb -lm -pthread -fno-strict-aliasing shared.c network.c parse.c -o csma
