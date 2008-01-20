CC=gcc
CFLAGS=-O2 -Wall -pedantic -std=gnu99 -Wno-unused -Werror

all: parsetest
parsetest: libuci.o test.o
	$(CC) $(CFLAGS) -o $@ $^

libuci.o: libuci.c parse.c uci.h list.c err.h
test.o: test.c uci.h

clean:
	rm -f parsetest *.o
