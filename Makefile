-all: 
	
	gcc -ggdb -lm -pthread -lz -fno-strict-aliasing shared.c client.c -lz -o client
	gcc -ggdb -lm -pthread -lz -fno-strict-aliasing shared.c ap.c parse.c -lz -o csma	
