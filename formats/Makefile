#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for file format modules
#
# Copyright (C) 1999-2006, Digium, Inc.
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

-include $(ASTTOPDIR)/menuselect.makeopts $(ASTTOPDIR)/menuselect.makedeps

C_MODS:=$(filter-out $(MENUSELECT_FORMATS),$(patsubst %.c,%,$(wildcard format_*.c)))
CC_MODS:=$(filter-out $(MENUSELECT_FORMATS),$(patsubst %.cc,%,$(wildcard format_*.cc)))

LOADABLE_MODS:=$(C_MODS) $(CC_MODS)

ifneq ($(findstring formats,$(MENUSELECT_EMBED)),)
  EMBEDDED_MODS:=$(LOADABLE_MODS)
  LOADABLE_MODS:=
endif

all: _all

include $(ASTTOPDIR)/Makefile.moddir_rules
