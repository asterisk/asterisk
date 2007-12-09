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

-include $(ASTTOPDIR)/menuselect.makeopts $(ASTTOPDIR)/menuselect.makedeps

C_MODS:=$(filter-out $(MENUSELECT_CODECS),$(patsubst %.c,%,$(wildcard codec_*.c)))
CC_MODS:=$(filter-out $(MENUSELECT_CODECS),$(patsubst %.cc,%,$(wildcard codec_*.cc)))

LOADABLE_MODS:=$(C_MODS) $(CC_MODS)

ifneq ($(findstring codecs,$(MENUSELECT_EMBED)),)
  EMBEDDED_MODS:=$(LOADABLE_MODS)
  LOADABLE_MODS:=
endif

LIBILBC:=ilbc/libilbc.a
LIBLPC10:=lpc10/liblpc10.a
LIBG722:=g722/libg722.a

all: _all

include $(ASTTOPDIR)/Makefile.moddir_rules

ifneq ($(GSM_INTERNAL),no)
GSM_INCLUDE:=-Igsm/inc
$(if $(filter codec_gsm,$(EMBEDDED_MODS)),modules.link,codec_gsm.so): gsm/lib/libgsm.a
endif

clean::
	$(MAKE) -C gsm clean
	$(MAKE) -C lpc10 clean
	$(MAKE) -C ilbc clean
	$(MAKE) -C g722 clean

gsm/lib/libgsm.a:
	@mkdir -p gsm/lib
	@$(MAKE) -C gsm lib/libgsm.a

$(LIBLPC10):
	@$(MAKE) -C lpc10 all

$(if $(filter codec_lpc10,$(EMBEDDED_MODS)),modules.link,codec_lpc10.so): $(LIBLPC10)

$(LIBILBC):
	@$(MAKE) -C ilbc all

$(if $(filter codec_ilbc,$(EMBEDDED_MODS)),modules.link,codec_ilbc.so): $(LIBILBC)

$(LIBG722):
	@$(MAKE) -C g722 all

$(if $(filter codec_g722,$(EMBEDDED_MODS)),modules.link,codec_g722.so): $(LIBG722)
