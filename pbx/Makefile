#
# Asterisk -- An open source telephony toolkit.
# 
# Makefile for PBX modules
#
# Copyright (C) 1999-2006, Digium, Inc.
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

-include $(ASTTOPDIR)/menuselect.makeopts $(ASTTOPDIR)/menuselect.makedeps $(ASTTOPDIR)/makeopts

MODULE_PREFIX=pbx
MENUSELECT_CATEGORY=PBX
MENUSELECT_DESCRIPTION=PBX Modules

all: _all

include $(ASTTOPDIR)/Makefile.moddir_rules

ifneq ($(findstring $(OSARCH), mingw32 cygwin ),)
  LIBS+= -lres_ael_share.so -lres_monitor.so
endif

$(call MOD_ADD_C,pbx_dundi,dundi-parser.c)
