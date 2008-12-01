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

-include $(ASTTOPDIR)/menuselect.makeopts $(ASTTOPDIR)/menuselect.makedeps

MODULE_PREFIX=func
MENUSELECT_CATEGORY=FUNCS
MENUSELECT_DESCRIPTION=Dialplan Functions

all: _all

include $(ASTTOPDIR)/Makefile.moddir_rules

func_strings.o: ASTCFLAGS+=-Wno-format-nonliteral
