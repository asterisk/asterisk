#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for PBX modules
#
# Copyright (C) 1999-2005, Mark Spencer
#
# Mark Spencer <markster@digium.com>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

PBX_LIBS=pbx_config.so pbx_spool.so pbx_dundi.so pbx_loopback.so pbx_realtime.so \
         pbx_ael.so

ifeq ($(findstring BSD,${OSARCH}),BSD)
  CFLAGS+=-I$(CROSS_COMPILE_TARGET)/usr/local/include -L$(CROSS_COMPILE_TARGET)/usr/local/lib
endif

# Add GTK console if appropriate
#PBX_LIBS+=$(shell $(CROSS_COMPILE_BIN)gtk-config --cflags >/dev/null 2>/dev/null && echo "pbx_gtkconsole.so")
# Add KDE Console if appropriate
#PBX_LIBS+=$(shell [ "$$QTDIR" != "" ] && echo "pbx_kdeconsole.so")

GTK_FLAGS=`${CROSS_COMPILE_BIN}gtk-config --cflags gthread`
GTK_LIBS=`${CROSS_COMPILE_BIN}gtk-config --libs gthread`
MOC=$(QTDIR)/bin/moc
KDE_FLAGS=-I$(KDEDIR)/include -I$(KDEDIR)/include/kde -I$(QTDIR)/include
KDE_LIBS=-L$(KDEDIR)/lib -L$(QTDIR)/lib -lqt -lkdecore -lkdeui
CFLAGS+=-fPIC

KDE_CONSOLE_OBJS=pbx_kdeconsole_main.o pbx_kdeconsole.o 

all: depend $(PBX_LIBS)

clean:
	rm -f *.so *.o .depend

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
	$(CC) $(SOLINK) -o $@ pbx_dundi.o dundi-parser.o -lz

%.moc : %.h
	$(MOC) $< -o $@

%.so : %.o
	$(CC) $(SOLINK) -o $@ $<

ifneq ($(wildcard .depend),)
  include .depend
endif

install: all
	for x in $(PBX_LIBS); do $(INSTALL) -m 755 $$x $(DESTDIR)$(MODULES_DIR) ; done

depend: .depend

.depend:
	../build_tools/mkdep $(CFLAGS) `ls *.c`
