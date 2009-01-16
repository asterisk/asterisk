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

-include ../menuselect.makeopts ../menuselect.makedeps

MENUSELECT_CATEGORY=FUNCS
MENUSELECT_DESCRIPTION=Dialplan Functions

ALL_C_MODS:=$(patsubst %.c,%,$(wildcard func_*.c))
ALL_CC_MODS:=$(patsubst %.cc,%,$(wildcard func_*.cc))

C_MODS:=$(filter-out $(MENUSELECT_FUNCS),$(ALL_C_MODS))
CC_MODS:=$(filter-out $(MENUSELECT_FUNCS),$(ALL_CC_MODS))

LOADABLE_MODS:=$(C_MODS) $(CC_MODS)

ifneq ($(findstring funcs,$(MENUSELECT_EMBED)),)
  EMBEDDED_MODS:=$(LOADABLE_MODS)
  LOADABLE_MODS:=
endif

all: _all

include $(ASTTOPDIR)/Makefile.moddir_rules
