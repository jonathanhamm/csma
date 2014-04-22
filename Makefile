-all:
	gcc -ggdb -lm -pthread -lz -fno-strict-aliasing shared.c client.c -o client
	gcc -ggdb -lm -pthread -lz -fno-strict-aliasing shared.c ap.c parse.c -o csma
