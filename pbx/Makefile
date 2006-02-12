#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for PBX modules
#
# Copyright (C) 1999-2006, Digium, Inc.
#
# Mark Spencer <markster@digium.com>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

MODS:=$(patsubst %.c,%.so,$(wildcard pbx_*.c))

#GTK=$(shell $(CROSS_COMPILE_BIN)gtk-config --cflags >/dev/null 2>/dev/null && echo "OK")
ifeq (${GTK},)
  MODS:=$(filter-out pbx_gtkconsole.so,$(MODS))
endif

#KDE=$(shell [ "$$QTDIR" != "" ] && echo "OK")
ifeq (${GTK},)
  MODS:=$(filter-out pbx_kdeconsole.so,$(MODS))
endif

GTK_FLAGS=`${CROSS_COMPILE_BIN}gtk-config --cflags gthread`
GTK_LIBS=`${CROSS_COMPILE_BIN}gtk-config --libs gthread`
MOC=$(QTDIR)/bin/moc
KDE_FLAGS=-I$(KDEDIR)/include -I$(KDEDIR)/include/kde -I$(QTDIR)/include
KDE_LIBS=-L$(KDEDIR)/lib -L$(QTDIR)/lib -lqt -lkdecore -lkdeui

ifeq (${OSARCH},CYGWIN)
  CYGSOLINK=-Wl,--out-implib=lib$@.a -Wl,--export-all-symbols
  CYGSOLIB=-L.. -L. -L../res -lasterisk.dll 
else
  CFLAGS+=-fPIC
endif

KDE_CONSOLE_OBJS=pbx_kdeconsole_main.o pbx_kdeconsole.o 

all: depend $(MODS)

clean-depend:
	rm -f .depend

clean: clean-depend
	rm -f *.so *.o

pbx_gtkconsole.o: pbx_gtkconsole.c
	$(CC) $(CFLAGS) $(GTK_FLAGS) -c -o $@ $<

pbx_gtkconsole.so: pbx_gtkconsole.o
	$(CC) $(SOLINK) -o $@ $< $(GTK_LIBS)

pbx_kdeconsole.o: pbx_kdeconsole.cc pbx_kdeconsole.moc
	$(CXX) $(CFLAGS) $(KDE_FLAGS) -c -o $@ $<

pbx_kdeconsole_main.o: pbx_kdeconsole_main.cc pbx_kdeconsole.h
	$(CXX) $(CFLAGS) $(KDE_FLAGS) -c -o $@ $<

pbx_kdeconsole.so: $(KDE_CONSOLE_OBJS)
	$(CC) $(SOLINK) -o $@ $(KDE_CONSOLE_OBJS) $(KDE_LIBS)

pbx_dundi.so: dundi-parser.o pbx_dundi.o
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} pbx_dundi.o dundi-parser.o -lz ${CYGSOLIB}

%.moc : %.h
	$(MOC) $< -o $@

%.so : %.o
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB}

ifneq ($(wildcard .depend),)
  include .depend
endif

install: all
	for x in $(MODS); do $(INSTALL) -m 755 $$x $(DESTDIR)$(MODULES_DIR) ; done

uninstall:

depend: .depend

.depend:
	../build_tools/mkdep $(CFLAGS) `ls *.c`
