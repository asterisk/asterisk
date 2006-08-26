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

-include ../menuselect.makeopts ../menuselect.makedeps

C_MODS:=$(filter-out $(MENUSELECT_CODECS),$(patsubst %.c,%,$(wildcard codec_*.c)))
CC_MODS:=$(filter-out $(MENUSELECT_CODECS),$(patsubst %.cc,%,$(wildcard codec_*.cc)))

LOADABLE_MODS:=$(C_MODS) $(CC_MODS)

ifneq ($(findstring codecs,$(MENUSELECT_EMBED)),)
  EMBEDDED_MODS:=$(LOADABLE_MODS)
  LOADABLE_MODS:=
endif

LIBILBC:=ilbc/libilbc.a
LIBLPC10:=lpc10/liblpc10.a

all: _all

include $(ASTTOPDIR)/Makefile.moddir_rules

ifneq ($(GSM_INTERNAL),)
GSM_INCLUDE:=-Igsm/inc
$(codec_gsm): gsm/lib/libgsm.a
endif

clean::
	$(MAKE) -C gsm clean
	$(MAKE) -C lpc10 clean
	$(MAKE) -C ilbc clean

gsm/lib/libgsm.a:
	@mkdir -p gsm/lib
	@CFLAGS="$(OTHER_SUBDIR_CFLAGS) -I." $(MAKE) -C gsm lib/libgsm.a

$(LIBLPC10):
	@CFLAGS="$(OTHER_SUBDIR_CFLAGS)" $(MAKE) -C lpc10 all

$(codec_lpc10): $(LIBLPC10)

$(LIBILBC):
	@CFLAGS="$(OTHER_SUBDIR_CFLAGS)" $(MAKE) -C ilbc all

$(codec_ilbc): $(LIBILBC)
