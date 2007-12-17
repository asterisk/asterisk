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

-include ../menuselect.makeopts ../menuselect.makedeps

MENUSELECT_CATEGORY=RES
MENUSELECT_DESCRIPTION=Resource Modules

ALL_C_MODS:=$(patsubst %.c,%,$(wildcard res_*.c))
ALL_CC_MODS:=$(patsubst %.cc,%,$(wildcard res_*.cc))

C_MODS:=$(filter-out $(MENUSELECT_RES),$(ALL_C_MODS))
CC_MODS:=$(filter-out $(MENUSELECT_RES),$(ALL_CC_MODS))

LOADABLE_MODS:=$(C_MODS) $(CC_MODS)

ifneq ($(findstring res,$(MENUSELECT_EMBED)),)
  EMBEDDED_MODS:=$(LOADABLE_MODS)
  LOADABLE_MODS:=
endif

all: _all

include $(ASTTOPDIR)/Makefile.moddir_rules

$(if $(filter res_snmp,$(EMBEDDED_MODS)),modules.link,res_snmp.so): snmp/agent.o

clean::
	rm -f snmp/*.o
