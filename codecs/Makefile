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

MENUSELECT_CATEGORY=CODECS
MENUSELECT_DESCRIPTION=Codec Translators

ALL_C_MODS:=$(patsubst %.c,%,$(wildcard codec_*.c))
ALL_CC_MODS:=$(patsubst %.cc,%,$(wildcard codec_*.cc))

C_MODS:=$(filter-out $(MENUSELECT_CODECS),$(ALL_C_MODS))
CC_MODS:=$(filter-out $(MENUSELECT_CODECS),$(ALL_CC_MODS))

LOADABLE_MODS:=$(C_MODS) $(CC_MODS)

ifneq ($(findstring codecs,$(MENUSELECT_EMBED)),)
  EMBEDDED_MODS:=$(LOADABLE_MODS)
  LOADABLE_MODS:=
endif

LIBILBC:=ilbc/libilbc.a
LIBLPC10:=lpc10/liblpc10.a

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

gsm/lib/libgsm.a:
	@mkdir -p gsm/lib
	@$(MAKE) -C gsm lib/libgsm.a

$(LIBLPC10):
	@$(MAKE) -C lpc10 all

$(if $(filter codec_lpc10,$(EMBEDDED_MODS)),modules.link,codec_lpc10.so): $(LIBLPC10)

$(LIBILBC):
	@$(MAKE) -C ilbc all _ASTCFLAGS="$(filter-out -Wmissing-prototypes -Wmissing-declarations,$(_ASTCFLAGS)) $(AST_NO_STRICT_OVERFLOW)"

$(if $(filter codec_ilbc,$(EMBEDDED_MODS)),modules.link,codec_ilbc.so): $(LIBILBC)
