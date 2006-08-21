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

ifneq ($(wildcard $(ASTTOPDIR)/menuselect.makeopts),)
  include ../menuselect.makeopts
  include ../menuselect.makedeps
endif

C_MODS:=$(filter-out $(MENUSELECT_RES),$(patsubst %.c,%,$(wildcard res_*.c)))
CC_MODS:=$(filter-out $(MENUSELECT_RES),$(patsubst %.cc,%,$(wildcard res_*.cc)))

LOADABLE_MODS:=$(C_MODS) $(CC_MODS)

ifneq ($(findstring res,$(MENUSELECT_EMBED)),)
  EMBEDDED_MODS:=$(LOADABLE_MODS)
  LOADABLE_MODS:=
endif

all: _all

include $(ASTTOPDIR)/Makefile.moddir_rules

$(res_snmp): snmp/agent.o

clean::
	rm -f snmp/*.o
