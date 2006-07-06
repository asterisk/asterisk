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

SELECTED_MODS:=$(filter-out $(MENUSELECT_RES),$(patsubst %.c,%,$(wildcard res_*.c)))

ifeq ($(OSARCH),FreeBSD)
  SELECTED_MODS:=$(filter-out $(shell if test ${BSDVERSION} -lt 500000 ; then echo "res_config_odbc"; fi),$(SELECTED_MODS))
endif

all: _all

include $(ASTTOPDIR)/Makefile.moddir_rules

res_snmp.so: res_snmp.o snmp/agent.o

clean::
	rm -f snmp/*.o

