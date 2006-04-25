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

MODS:=$(filter-out $(MENUSELECT_PBX),$(patsubst %.c,%.so,$(wildcard pbx_*.c)))

MOC=$(QTDIR)/bin/moc
KDE_FLAGS=-I$(KDEDIR)/include -I$(KDEDIR)/include/kde -I$(QTDIR)/include
KDE_LIBS=-L$(KDEDIR)/lib -L$(QTDIR)/lib -lqt -lkdecore -lkdeui

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

pbx_dundi.so: pbx_dundi.o dundi-parser.o
	$(CC) $(SOLINK) -o $@ $< dundi-parser.o $(Z_LIB)

pbx_dundi.o: pbx_dundi.c
	$(CC) -c -o $@ $(CFLAGS) $(Z_INCLUDE) $<

pbx_ael.o: ael/aelflex.o ael/aelbison.o ../include/asterisk/ael_structs.h

pbx_ael.so: pbx_ael.o ael/aelbison.o ael/aelflex.o
	$(CC) $(SOLINK) -o $@ pbx_ael.o ael/aelbison.o ael/aelflex.o

ael/aelflex.o: ael/ael_lex.c ../include/asterisk/ael_structs.h ael/ael.tab.h
	$(CC) $(CFLAGS) -I. -c -o ael/aelflex.o ael/ael_lex.c

ael/aelbison.o: ael/ael.tab.c ael/ael.tab.h ../include/asterisk/ael_structs.h
	$(CC) $(CFLAGS) -I. -c -o ael/aelbison.o ael/ael.tab.c

ael/ael_lex.c: ael/ael.flex
	(cd ael; flex ael.flex)

ael/ael.tab.c ael/ael.tab.h: ael/ael.y
	(cd ael; bison -v -d ael.y)

%.moc : %.h
	$(MOC) $< -o $@

%.so : %.o
	$(CC) $(SOLINK) -o $@ $<

ifneq ($(wildcard .depend),)
  include .depend
endif

install: all
	for x in $(MODS); do $(INSTALL) -m 755 $$x $(DESTDIR)$(MODULES_DIR) ; done

uninstall:

depend: .depend

.depend:
	../build_tools/mkdep $(CFLAGS) `ls *.c`
