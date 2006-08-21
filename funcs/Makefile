#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for dialplan functions
#
# Copyright (C) 2005-2006, Digium, Inc.
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

ifneq ($(wildcard $(ASTTOPDIR)/menuselect.makeopts),)
  include ../menuselect.makeopts
  include ../menuselect.makedeps
endif

C_MODS:=$(filter-out $(MENUSELECT_FUNCS),$(patsubst %.c,%,$(wildcard func_*.c)))
CC_MODS:=$(filter-out $(MENUSELECT_FUNCS),$(patsubst %.cc,%,$(wildcard func_*.cc)))

LOADABLE_MODS:=$(C_MODS) $(CC_MODS)

ifneq ($(findstring funcs,$(MENUSELECT_EMBED)),)
  EMBEDDED_MODS:=$(LOADABLE_MODS)
  LOADABLE_MODS:=
endif

all: _all

include $(ASTTOPDIR)/Makefile.moddir_rules
