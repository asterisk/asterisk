#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for PBX modules
#
# Copyright (C) 1999-2006, Digium, Inc.
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

ifneq ($(wildcard ../menuselect.makeopts),)
  include ../menuselect.makeopts
endif

SELECTED_MODS:=$(filter-out $(MENUSELECT_PBX),$(patsubst %.c,%,$(wildcard pbx_*.c)))

MODS:=$(patsubst %,%.so,$(SELECTED_MODS))

all: _all

include ../Makefile.rules

clean::
	rm -f ael/*.o

ael/aelflex.o: ael/ael_lex.c ../include/asterisk/ael_structs.h ael/ael.tab.h
	$(CC) $(CFLAGS) -I. -c -o $@ $<

ael/aelbison.o: ael/ael.tab.c ael/ael.tab.h ../include/asterisk/ael_structs.h
	$(CC) $(CFLAGS) -I. -c -o $@ $<

pbx_ael.so: pbx_ael.o ael/aelbison.o ael/aelflex.o

ael/ael_lex.c:
	(cd ael; flex ael.flex; sed -i -e "/begin standard C headers/i#include \"asterisk.h\"" ael_lex.c)

ael/ael.tab.c ael/ael.tab.h:
	(cd ael; bison -v -d ael.y)
