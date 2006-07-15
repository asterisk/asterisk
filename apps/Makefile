#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for PBX applications
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

C_MODS:=$(filter-out $(MENUSELECT_APPS),$(patsubst %.c,%,$(wildcard app_*.c)))
CC_MODS:=$(filter-out $(MENUSELECT_APPS),$(patsubst %.cc,%,$(wildcard app_*.cc)))

SELECTED_MODS:=$(C_MODS) $(CC_MODS)

ifneq ($(findstring ODBC_STORAGE,$(MENUSELECT_OPTS_app_voicemail)),)
MENUSELECT_DEPENDS_app_voicemail+=$(MENUSELECT_DEPENDS_ODBC_STORAGE)
endif
ifneq ($(findstring EXTENDED_ODBC_STORAGE,$(MENUSELECT_OPTS_app_voicemail)),)
MENUSELECT_DEPENDS_app_voicemail+=$(MENUSELECT_DEPENDS_EXTENDED_ODBC_STORAGE)
endif
ifneq ($(findstring IMAP_STORAGE,$(MENUSELECT_OPTS_app_voicemail)),)
MENUSELECT_DEPENDS_app_voicemail+=$(MENUSELECT_DEPENDS_IMAP_STORAGE)
endif

all: _all

include $(ASTTOPDIR)/Makefile.moddir_rules

ifeq (SunOS,$(shell uname))
app_chanspy.so: LIBS+=-lrt
endif
