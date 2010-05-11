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

-include ../menuselect.makeopts ../menuselect.makedeps ../makeopts

MENUSELECT_CATEGORY=PBX
MENUSELECT_DESCRIPTION=PBX Modules

ALL_C_MODS:=$(patsubst %.c,%,$(wildcard pbx_*.c))
ALL_CC_MODS:=$(patsubst %.cc,%,$(wildcard pbx_*.cc))

C_MODS:=$(filter-out $(MENUSELECT_PBX),$(ALL_C_MODS))
CC_MODS:=$(filter-out $(MENUSELECT_PBX),$(ALL_CC_MODS))

LOADABLE_MODS:=$(C_MODS) $(CC_MODS)

ifneq ($(findstring pbx,$(MENUSELECT_EMBED)),)
  EMBEDDED_MODS:=$(LOADABLE_MODS)
  LOADABLE_MODS:=
endif

all: _all

include $(ASTTOPDIR)/Makefile.moddir_rules

clean::
	rm -f ael/*.o ael/*.i

ael/ael_lex.o: ael/ael_lex.c ../include/asterisk/ael_structs.h ael/ael.tab.h
ael/ael_lex.o: _ASTCFLAGS+=-I. -Wno-unused

ael/ael.tab.o: ael/ael.tab.c ael/ael.tab.h ../include/asterisk/ael_structs.h
ael/ael.tab.o: _ASTCFLAGS+=-I.

ael/ael.tab.o ael/ael_lex.o: _ASTCFLAGS+=$(MENUSELECT_OPTS_pbx_ael:%=-D%) $(foreach dep,$(MENUSELECT_DEPENDS_pbx_ael),$(value $(dep)_INCLUDE))

$(if $(filter pbx_ael,$(EMBEDDED_MODS)),modules.link,pbx_ael.so): ael/ael.tab.o ael/ael_lex.o

ifneq ($(findstring REBUILD_PARSERS,$(MENUSELECT_CFLAGS)),)
ael/ael_lex.c: ael/ael.flex
else
ael/ael_lex.c:
endif
	$(ECHO_PREFIX) echo "   [FLEX] $< -> $@"
	$(CMD_PREFIX) (cd ael; $(FLEX) ael.flex)
	$(CMD_PREFIX) sed 's@#if __STDC_VERSION__ >= 199901L@#if !defined __STDC_VERSION__ || __STDC_VERSION__ >= 199901L@' $@ > $@.fix
	$(CMD_PREFIX) echo "#include \"asterisk.h\"" > $@
	$(CMD_PREFIX) echo >> $@
	$(CMD_PREFIX) cat $@.fix >> $@
	$(CMD_PREFIX) rm $@.fix

ifneq ($(findstring REBUILD_PARSERS,$(MENUSELECT_CFLAGS)),)
ael/ael.tab.c ael/ael.tab.h: ael/ael.y
else
ael/ael.tab.c ael/ael.tab.h:
endif
	$(ECHO_PREFIX) echo "   [BISON] $< -> $@"
	$(CMD_PREFIX) (cd ael; $(BISON) -v -d ael.y)

dundi-parser.o: dundi-parser.h
dundi-parser.o: _ASTCFLAGS+=-I.

dundi-parser.o: _ASTCFLAGS+=$(MENUSELECT_OPTS_pbx_dundi:%=-D%) $(foreach dep,$(MENUSELECT_DEPENDS_pbx_dundi),$(value $(dep)_INCLUDE))

$(if $(filter pbx_dundi,$(EMBEDDED_MODS)),modules.link,pbx_dundi.so): dundi-parser.o

pbx_gtkconsole.o: _ASTCFLAGS+=-Wno-strict-prototypes
