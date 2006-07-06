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

ifneq ($(wildcard ../menuselect.makeopts),)
  include ../menuselect.makeopts
  include ../menuselect.makedeps
endif

SELECTED_MODS:=$(filter-out $(MENUSELECT_FUNCS),$(patsubst %.c,%,$(wildcard func_*.c)))

# TODO This check needs to be handled with autoconf
ifeq ($(OSARCH),SunOS)
  ifeq ($(shell if [ 0x`$(CROSS_COMPILE_BIN)curl-config --vernum` -ge 0x70907 ]; then echo "OK" ; fi),)
    MODS:=$(filter-out func_curl,$(MODS))
  endif
else
  ifeq ($(shell if [[ 0x`$(CROSS_COMPILE_BIN)curl-config --vernum` -ge 0x70907 ]]; then echo "OK" ; fi),)
    MODS:=$(filter-out func_curl,$(MODS)) 
  endif
endif

all: _all

include $(ASTTOPDIR)/Makefile.moddir_rules
