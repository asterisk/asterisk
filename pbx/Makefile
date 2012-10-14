#
# Asterisk -- An open source telephony toolkit.
# 
# Makefile for PBX modules
#
# Copyright (C) 1999-2006, Digium, Inc.
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

-include $(ASTTOPDIR)/menuselect.makeopts $(ASTTOPDIR)/menuselect.makedeps $(ASTTOPDIR)/makeopts

MODULE_PREFIX=pbx
MENUSELECT_CATEGORY=PBX
MENUSELECT_DESCRIPTION=PBX Modules

all: _all

include $(ASTTOPDIR)/Makefile.moddir_rules

ifneq ($(findstring $(OSARCH), mingw32 cygwin ),)
  LIBS+= -lres_ael_share.so -lres_monitor.so
endif

clean::
	rm -f ael/*.o ael/*.i

dundi-parser.o: dundi-parser.h
dundi-parser.o: _ASTCFLAGS+=-I.

pbx_gtkconsole.o: _ASTCFLAGS+=-Wno-strict-prototypes

$(if $(filter pbx_dundi,$(EMBEDDED_MODS)),modules.link,pbx_dundi.so): dundi-parser.o
dundi-parser.o: _ASTCFLAGS+=$(call MOD_ASTCFLAGS,pbx_dundi)
