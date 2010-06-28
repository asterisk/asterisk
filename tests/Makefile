#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for test modules
#
# Copyright (C) 1999-2010, Digium, Inc.
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

-include ../menuselect.makeopts ../menuselect.makedeps ../makeopts

MENUSELECT_CATEGORY=TESTS
MENUSELECT_DESCRIPTION=Test Modules

ALL_C_MODS:=$(patsubst %.c,%,$(wildcard test_*.c))
ALL_CC_MODS:=$(patsubst %.cc,%,$(wildcard test_*.cc))

C_MODS:=$(filter-out $(MENUSELECT_TESTS),$(ALL_C_MODS))
CC_MODS:=$(filter-out $(MENUSELECT_TESTS),$(ALL_CC_MODS))

LOADABLE_MODS:=$(C_MODS) $(CC_MODS)

ifneq ($(findstring tests,$(MENUSELECT_EMBED)),)
  EMBEDDED_MODS:=$(LOADABLE_MODS)
  LOADABLE_MODS:=
endif

all: _all

include $(ASTTOPDIR)/Makefile.moddir_rules
