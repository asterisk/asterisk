#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for resource modules
#
# Copyright (C) 1999-2006, Digium, Inc.
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

-include $(ASTTOPDIR)/menuselect.makeopts $(ASTTOPDIR)/menuselect.makedeps

C_MODS:=$(filter-out $(MENUSELECT_RES),$(patsubst %.c,%,$(wildcard res_*.c)))
CC_MODS:=$(filter-out $(MENUSELECT_RES),$(patsubst %.cc,%,$(wildcard res_*.cc)))

LOADABLE_MODS:=$(C_MODS) $(CC_MODS)

ifneq ($(findstring res,$(MENUSELECT_EMBED)),)
  EMBEDDED_MODS:=$(LOADABLE_MODS)
  LOADABLE_MODS:=
endif

all: _all

include $(ASTTOPDIR)/Makefile.moddir_rules

ifneq ($(findstring $(OSARCH), mingw32 cygwin ),)
  # cygwin has some dependencies among res_ things.
  # We use order-only dependencies, and then add the libraries as required.
  res_features.so: | res_monitor.so
  res_features.so_LIBS:= -lres_monitor.so
  #
  res_agi.so: | res_speech.so
  res_agi.so_LIBS:= -lres_speech.so
endif

ael/ael_lex.o: ael/ael_lex.c ../include/asterisk/ael_structs.h ael/ael.tab.h
ael/ael_lex.o: ASTCFLAGS+=-I. -Iael 

ael/ael.tab.o: ael/ael.tab.c ael/ael.tab.h ../include/asterisk/ael_structs.h
ael/ael.tab.o: ASTCFLAGS+=-I. -Iael -DYYENABLE_NLS=0

$(if $(filter res_snmp,$(EMBEDDED_MODS)),modules.link,res_snmp.so): snmp/agent.o

$(if $(filter res_ael_share,$(EMBEDDED_MODS)),modules.link,res_ael_share.so): ael/ael_lex.o ael/ael.tab.o ael/pval.o

ael/ael_lex.c:
	(cd ael; flex ael.flex; sed -i -e "/begin standard C headers/i#include \"asterisk.h\"" ael_lex.c)

ael/ael.tab.c ael/ael.tab.h:
	(cd ael; bison -v -d ael.y)

ael/pval.o: ael/pval.c

clean::
	rm -f snmp/*.o
	rm -f ael/*.o
