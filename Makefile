VERSION=0.1

COPTS=-O2
WOPTS=-pedantic -Wno-unused -Werror
FPIC=-fPIC
CFLAGS=$(COPTS) $(WOPTS) -Wall -std=gnu99

AR=ar
LD=ld
CC=gcc
LIBS=-lc
RANLIB=ranlib

ifneq ($(DEBUG),)
  COPTS = -O0
  CFLAGS += -g3 -DDEBUG_ALL
endif
OS=$(shell uname)
ifeq ($(OS),Darwin)
  LINK=$(LD)
  SHLIB_EXT=dylib
  SHLIB_FLAGS=-dylib
else
  LINK=$(CC)
  SHLIB_EXT=so
  SHLIB_FLAGS=-shared -Wl,-soname,$(SHLIB_FILE)
endif
SHLIB_FILE=libuci.$(SHLIB_EXT).$(VERSION)

LIBUCI_DEPS=file.c history.c list.c util.c err.h uci.h

all: uci-static uci libuci.$(SHLIB_EXT)

cli.o: cli.c uci.h

uci: cli.o libuci.$(SHLIB_EXT)
	$(CC) -o $@ $< -L. -luci

uci-static: cli.o libuci.a
	$(CC) $(CFLAGS) -o $@ $^

libuci-static.o: libuci.c $(LIBUCI_DEPS)
	$(CC) $(CFLAGS) -c -o $@ $<

libuci-shared.o: libuci.c $(LIBUCI_DEPS)
	$(CC) $(CFLAGS) $(FPIC) -c -o $@ $<

libuci.a: libuci-static.o
	rm -f $@
	$(AR) rc $@ $^
	$(RANLIB) $@

libuci.$(SHLIB_EXT): libuci-shared.o
	$(LINK) $(SHLIB_FLAGS) -o $(SHLIB_FILE) $^ $(LIBS)
	ln -sf $(SHLIB_FILE) $@

clean:
	rm -f uci uci-static *.[oa] *.so* *.dylib*
