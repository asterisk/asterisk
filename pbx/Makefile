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

-include $(ASTTOPDIR)/menuselect.makeopts $(ASTTOPDIR)/menuselect.makedeps

C_MODS:=$(filter-out $(MENUSELECT_PBX),$(patsubst %.c,%,$(wildcard pbx_*.c)))
CC_MODS:=$(filter-out $(MENUSELECT_PBX),$(patsubst %.cc,%,$(wildcard pbx_*.cc)))

LOADABLE_MODS:=$(C_MODS) $(CC_MODS)

ifneq ($(findstring pbx,$(MENUSELECT_EMBED)),)
  EMBEDDED_MODS:=$(LOADABLE_MODS)
  LOADABLE_MODS:=
endif

all: _all

include $(ASTTOPDIR)/Makefile.moddir_rules

ifneq ($(findstring $(OSARCH), mingw32 cygwin ),)
  LIBS+= -lres_ael_share.so -lres_monitor.so
endif

clean::
	rm -f ael/*.o

dundi-parser.o: dundi-parser.h
dundi-parser.o: ASTCFLAGS+=-I.

$(if $(filter pbx_dundi,$(EMBEDDED_MODS)),modules.link,pbx_dundi.so): dundi-parser.o
