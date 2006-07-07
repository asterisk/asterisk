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
  include ../menuselect.makedeps
endif

SELECTED_MODS:=$(filter-out $(MENUSELECT_PBX),$(patsubst %.c,%,$(wildcard pbx_*.c)) $(patsubst %.cc,%,$(wildcard pbx_*.cc)))

MODS:=$(patsubst %,%.so,$(SELECTED_MODS))

CC_MODS:=pbx_kdeconsole

all: _all

include $(ASTTOPDIR)/Makefile.moddir_rules

clean::
	rm -f ael/*.o

$(eval $(call ast_make_o_c,ael/aelflex.o,ael/ael_lex.c ../include/asterisk/ael_structs.h ael/ael.tab.h))
ael/aelflex.o: CFLAGS+=-I.

$(eval $(call ast_make_o_c,ael/aelbison.o,ael/ael.tab.c ael/ael.tab.h ../include/asterisk/ael_structs.h))
ael/aelbison.o: CFLAGS+=-I.

pbx_ael.so: pbx_ael.o ael/aelbison.o ael/aelflex.o

ael/ael_lex.c:
	(cd ael; flex ael.flex; sed -i -e "/begin standard C headers/i#include \"asterisk.h\"" ael_lex.c)

ael/ael.tab.c ael/ael.tab.h:
	(cd ael; bison -v -d ael.y)

$(eval $(call ast_make_o_c,dundi-parser.o,dundi-parser.c dundi-parser.h))
dundi-parser.o: CFLAGS+=-I.

pbx_dundi.so: pbx_dundi.o dundi-parser.o
