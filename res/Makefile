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

ifneq ($(wildcard ../menuselect.makeopts),)
  include ../menuselect.makeopts
  include ../menuselect.makedeps
endif

C_MODS:=$(filter-out $(MENUSELECT_RES),$(patsubst %.c,%,$(wildcard res_*.c)))
CC_MODS:=$(filter-out $(MENUSELECT_RES),$(patsubst %.cc,%,$(wildcard res_*.cc)))

SELECTED_MODS:=$(C_MODS) $(CC_MODS)

all: _all

include $(ASTTOPDIR)/Makefile.moddir_rules

res_snmp.so: snmp/agent.o

clean::
	rm -f snmp/*.o
