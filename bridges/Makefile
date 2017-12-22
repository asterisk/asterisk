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
MODULE_EXCLUDE=binaural_rendering_in_bridge_softmix

all: _all

ifeq ($(findstring binaural_rendering_in_bridge_softmix,$(MENUSELECT_BRIDGES)),)
bridge_softmix.o bridge_softmix/bridge_softmix_binaural.o: _ASTCFLAGS+=-DBINAURAL_RENDERING
bridge_softmix.so: LIBS+=$(FFTW3_LIB)
endif

include $(ASTTOPDIR)/Makefile.moddir_rules

$(call MOD_ADD_C,bridge_softmix,$(wildcard bridge_softmix/*.c))
