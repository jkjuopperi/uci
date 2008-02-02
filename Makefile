COPTS=-O2
WOPTS=-pedantic -Wno-unused -Werror
CFLAGS=$(COPTS) -fPIC -Wall -std=gnu99
ifneq ($(DEBUG),)
  COPTS = -O0
  CFLAGS += -g3 -DDEBUG_ALL
endif

AR=ar
CC=gcc
RANLIB=ranlib

all: uci

cli.o: cli.c uci.h
uci: cli.o libuci.a
	$(CC) $(CFLAGS) -o $@ $^

libuci.o: libuci.c file.c uci.h list.c err.h util.c
libuci.a: libuci.o
	rm -f $@
	$(AR) rc $@ $^
	$(RANLIB) $@

clean:
	rm -f uci *.[oa]
