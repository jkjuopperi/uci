include ../Makefile.inc
prefix=/usr/local
libdir=$(prefix)/lib
luadir=$(libdir)/lua/5.1

CPPFLAGS=-I.. $(shell pkg-config --silence-errors --cflags lua5.1)
LIBS=-L.. -luci $(shell pkg-config --silence-errors --libs lua5.1)

PLUGIN_LD=$(CC)
ifeq ($(OS),Darwin)
  PLUGIN_LDFLAGS=-bundle -undefined dynamic_lookup
else
  PLUGIN_LDFLAGS=-shared -Wl,-soname,$(SHLIB_FILE)
endif

all: uci.so

uci.so: uci.o
	$(PLUGIN_LD) $(PLUGIN_LDFLAGS) -o $@ $^ $(LIBS)

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(FPIC) -c -o $@ $<

install:
	mkdir -p $(DESTDIR)$(luadir)
	$(INSTALL) -m0644 uci.so $(DESTDIR)$(luadir)/

clean:
	rm -f *.so *.o uci.so
