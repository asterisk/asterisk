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

-include ../menuselect.makeopts ../menuselect.makedeps

MENUSELECT_CATEGORY=APPS
MENUSELECT_DESCRIPTION=Applications

ALL_C_MODS:=$(patsubst %.c,%,$(wildcard app_*.c))
ALL_CC_MODS:=$(patsubst %.cc,%,$(wildcard app_*.cc))

C_MODS:=$(filter-out $(MENUSELECT_APPS),$(ALL_C_MODS))
CC_MODS:=$(filter-out $(MENUSELECT_APPS),$(ALL_CC_MODS))

LOADABLE_MODS:=$(C_MODS) $(CC_MODS)

ifneq ($(findstring apps,$(MENUSELECT_EMBED)),)
  EMBEDDED_MODS:=$(LOADABLE_MODS)
  LOADABLE_MODS:=
endif

MENUSELECT_OPTS_app_directory:=$(MENUSELECT_OPTS_app_voicemail)
ifneq ($(findstring ODBC_STORAGE,$(MENUSELECT_OPTS_app_voicemail)),)
MENUSELECT_DEPENDS_app_voicemail+=$(MENUSELECT_DEPENDS_ODBC_STORAGE)
MENUSELECT_DEPENDS_app_directory+=$(MENUSELECT_DEPENDS_ODBC_STORAGE)
endif
ifneq ($(findstring IMAP_STORAGE,$(MENUSELECT_OPTS_app_voicemail)),)
MENUSELECT_DEPENDS_app_voicemail+=$(MENUSELECT_DEPENDS_IMAP_STORAGE)
MENUSELECT_DEPENDS_app_directory+=$(MENUSELECT_DEPENDS_IMAP_STORAGE)
endif

ifeq (SunOS,$(shell uname))
MENUSELECT_DEPENDS_app_chanspy+=RT
RT_LIB=-lrt
endif

all: _all

include $(ASTTOPDIR)/Makefile.moddir_rules
