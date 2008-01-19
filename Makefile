CC=gcc
CFLAGS=-O2 -Wall -pedantic -std=gnu99

all: parsetest
parsetest: libuci.o test.o
	$(CC) $(CFLAGS) -o $@ $^

libuci.o: libuci.c parse.c libuci.h
test.o: test.c libuci.h

clean:
	rm -f parsetest *.o
