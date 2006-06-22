#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for Build Tools
#
# Copyright (C) 2005-2006, Digium, Inc.
#
# Russell Bryant <russell@digium.com>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

.PHONY: clean dist-clean

MENUSELECT_OBJS=menuselect.o menuselect_curses.o
MENUSELECT_CFLAGS=-g -c -D_GNU_SOURCE -DMENUSELECT -I../ -I../include/
MENUSELECT_LIBS=../mxml/libmxml.a

ifeq ($(OSARCH),SunOS)
  MENUSELECT_OBJS+=../strcompat.o
endif

ifneq ($(NCURSES_LIB),)
  MENUSELECT_LIBS+=$(NCURSES_LIB)
  MENUSELECT_INCLUDE=$(NCURSES_INCLUDE)
else
  MENUSELECT_LIBS+=$(CURSES_LIB)
  MENUSELECT_INCLUDE=$(CURSES_INCLUDE)
endif

menuselect: $(MENUSELECT_OBJS)
	$(CC) -g -Wall -o $@ $(MENUSELECT_OBJS) $(MENUSELECT_LIBS)

menuselect.o: menuselect.c menuselect.h
	$(CC) -Wall -o $@ $(MENUSELECT_CFLAGS) $<

menuselect_curses.o: menuselect_curses.c menuselect.h
	$(CC) -Wall -o $@ $(MENUSELECT_CFLAGS) $(MENUSELECT_INCLUDE) $<

clean:
	rm -f menuselect *.o

dist-clean: clean
	rm -f menuselect-deps
