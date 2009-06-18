prefix=/usr
DESTDIR=
LIBDIRNAME=lib

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
  ifeq ($(shell gcc -dumpmachine),i686-apple-darwin10)
    SHLIB_FLAGS += -arch x86_64
  endif
else
  LINK=$(CC)
  SHLIB_EXT=so
  SHLIB_FLAGS=-shared -Wl,-soname,$(SHLIB_FILE)
endif

