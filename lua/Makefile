CC=gcc
LD=ld
CFLAGS=-O2 -Wall -Werror -pedantic --std=gnu99 -Wno-unused
CPPFLAGS=-I..
LIBS=-L.. -luci
OS=$(shell uname)
LINK=$(CC)
ifeq ($(OS),Darwin)
  SHLIB_FLAGS=-bundle -undefined dynamic_lookup
else
  SHLIB_FLAGS=-shared -Wl,-soname,$(SHLIB_FILE)
endif

all: uci.so

uci.so: uci.o
	$(LINK) $(SHLIB_FLAGS) -o $@ $^ $(LIBS)

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

install:
	mkdir -p $(DESTDIR)/usr/lib/lua
	install -m0644 uci.so $(DESTDIR)/usr/lib/lua/

clean:
	rm -f *.so *.o uci.so
