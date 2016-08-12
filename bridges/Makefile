#
# Asterisk -- An open source telephony toolkit.
# 
# Makefile for bridging modules
#
# Copyright (C) 2005-2007, Digium, Inc.
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

-include $(ASTTOPDIR)/menuselect.makeopts $(ASTTOPDIR)/menuselect.makedeps

MODULE_PREFIX=bridge
MENUSELECT_CATEGORY=BRIDGES
MENUSELECT_DESCRIPTION=Bridging Modules

all: _all

include $(ASTTOPDIR)/Makefile.moddir_rules

$(call MOD_ADD_C,bridge_softmix,$(wildcard bridge_softmix/*.c))
LIBS+= -lfftw3
