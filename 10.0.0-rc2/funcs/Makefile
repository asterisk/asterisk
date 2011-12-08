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

# the SPRINTF() function in func_sprintf accepts format specifiers
# and thus passes them to snprintf() as non-literal strings; the compiler
# can't check the string and arguments to ensure they match, so this
# warning must be disabled; for safety reasons, SPRINTF() is kept in
# a separate module so that as little code as possible is left unchecked
func_sprintf.o: _ASTCFLAGS+=-Wno-format-nonliteral
