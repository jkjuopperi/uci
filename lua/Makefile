include ../Makefile.inc
LUA_VERSION=5.1
PREFIX_SEARCH=/usr /usr/local /opt/local
LUA_PLUGINDIR=$(firstword \
	$(foreach ldir,$(subst ;, ,$(shell lua -e 'print(package.cpath)')), \
		$(if $(findstring lib/lua/,$(ldir)),$(patsubst %/?.so,%,$(ldir))) \
	) \
)

# find lua prefix
LUA_PREFIX=$(firstword \
	$(foreach prefix,$(PREFIX_SEARCH),\
		$(if $(wildcard $(prefix)/include/lua.h),$(prefix)) \
	) \
)

libdir=$(prefix)/libs
luadir=$(if $(LUA_PLUGINDIR),$(LUA_PLUGINDIR),$(libdir)/lua/$(LUA_VERSION))
luainc=$(shell pkg-config --silence-errors --cflags lua$(LUA_VERSION))

CPPFLAGS=-I.. $(if $(luainc),$(luainc), -I$(LUA_PREFIX)/include)
LIBS=-L.. -luci $(shell pkg-config --silence-errors --libs lua$(LUA_VERSION))

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
