include ../Makefile.inc

CPPFLAGS=-I.. $(shell pkg-config --silence-errors --cflags lua5.1)
LIBS=-L.. -luci

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
	mkdir -p $(DESTDIR)$(prefix)/lib/lua/5.1
	$(INSTALL) -m0644 uci.so $(DESTDIR)$(prefix)/lib/lua/5.1/

clean:
	rm -f *.so *.o uci.so
