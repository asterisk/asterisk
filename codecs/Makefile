#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for codec modules
#
# Copyright (C) 1999-2006, Digium, Inc.
#
# Mark Spencer <markster@digium.com>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

ifneq ($(wildcard ../menuselect.makeopts),)
  include ../menuselect.makeopts
  include ../menuselect.makedeps
endif

C_MODS:=$(filter-out $(MENUSELECT_CODECS),$(patsubst %.c,%,$(wildcard codec_*.c)))
CC_MODS:=$(filter-out $(MENUSELECT_CODECS),$(patsubst %.cc,%,$(wildcard codec_*.cc)))

SELECTED_MODS:=$(C_MODS) $(CC_MODS)

LIBILBC:=ilbc/libilbc.a
LIBLPC10:=lpc10/liblpc10.a

all: _all

ifeq ($(GSM_LIB),internal)
GSM_INCLUDE:=-Igsm/inc
GSM_LIB:=

codec_gsm.so: gsm/lib/libgsm.a
endif

include $(ASTTOPDIR)/Makefile.moddir_rules

clean::
	$(MAKE) -C gsm clean
	$(MAKE) -C lpc10 clean
	$(MAKE) -C ilbc clean

gsm/lib/libgsm.a:
	@mkdir -p gsm/lib
	@CFLAGS="$(ASTCFLAGS) -I." $(MAKE) -C gsm lib/libgsm.a

$(LIBLPC10):
	@CFLAGS="$(ASTCFLAGS)" $(MAKE) -C lpc10 all

codec_lpc10.so: $(LIBLPC10)

$(LIBILBC):
	@CFLAGS="$(ASTCFLAGS)" $(MAKE) -C ilbc all

codec_ilbc.so: $(LIBILBC)
