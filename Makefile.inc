prefix=/usr
DESTDIR=

COPTS=-O2
WOPTS=-pedantic -Werror -Wall
FPIC=-fPIC
CFLAGS=$(COPTS) $(WOPTS) -std=gnu99
CPPFLAGS=-I.

AR=ar
LD=ld
CC=gcc
RANLIB=ranlib
INSTALL=install
MKDIR=mkdir

ifeq ($(DEBUG),1)
  COPTS = -O0
  CFLAGS += -g3
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

