#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for CDR backends
#
# Copyright (C) 1999-2006, Digium, Inc.
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

-include ../menuselect.makeopts ../menuselect.makedeps

MENUSELECT_CATEGORY=CDR
MENUSELECT_DESCRIPTION=Call Detail Recording

ALL_C_MODS:=$(patsubst %.c,%,$(wildcard cdr_*.c))
ALL_CC_MODS:=$(patsubst %.cc,%,$(wildcard cdr_*.cc))

C_MODS:=$(filter-out $(MENUSELECT_CDR),$(ALL_C_MODS))
CC_MODS:=$(filter-out $(MENUSELECT_CDR),$(ALL_CC_MODS))

LOADABLE_MODS:=$(C_MODS) $(CC_MODS)

ifneq ($(findstring cdr,$(MENUSELECT_EMBED)),)
  EMBEDDED_MODS:=$(LOADABLE_MODS)
  LOADABLE_MODS:=
endif

all: _all

include $(ASTTOPDIR)/Makefile.moddir_rules
