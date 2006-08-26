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

-include ../menuselect.makeopts ../menuselect.makedeps

C_MODS:=$(filter-out $(MENUSELECT_PBX),$(patsubst %.c,%,$(wildcard pbx_*.c)))
CC_MODS:=$(filter-out $(MENUSELECT_PBX),$(patsubst %.cc,%,$(wildcard pbx_*.cc)))

LOADABLE_MODS:=$(C_MODS) $(CC_MODS)

ifneq ($(findstring pbx,$(MENUSELECT_EMBED)),)
  EMBEDDED_MODS:=$(LOADABLE_MODS)
  LOADABLE_MODS:=
endif

all: _all

include $(ASTTOPDIR)/Makefile.moddir_rules

clean::
	rm -f ael/*.o

$(eval $(call ast_make_o_c,ael/aelflex.o,ael/ael_lex.c ../include/asterisk/ael_structs.h ael/ael.tab.h))
ael/aelflex.o: CFLAGS+=-I.

$(eval $(call ast_make_o_c,ael/aelbison.o,ael/ael.tab.c ael/ael.tab.h ../include/asterisk/ael_structs.h))
ael/aelbison.o: CFLAGS+=-I.

$(pbx_ael): ael/aelbison.o ael/aelflex.o

ael/ael_lex.c:
	(cd ael; flex ael.flex; sed -i -e "/begin standard C headers/i#include \"asterisk.h\"" ael_lex.c)

ael/ael.tab.c ael/ael.tab.h:
	(cd ael; bison -v -d ael.y)

dundi-parser.o: dundi-parser.h
dundi-parser.o: CFLAGS+=-I.

$(pbx_dundi): dundi-parser.o
