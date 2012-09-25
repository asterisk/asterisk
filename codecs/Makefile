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

MODULE_PREFIX=codec
MENUSELECT_CATEGORY=CODECS
MENUSELECT_DESCRIPTION=Codec Translators

SUB_GSM := gsm

SUB_ILBC := ilbc
LIBILBC := $(SUB_ILBC)/libilbc.a

SUB_LPC10 := lpc10
LIBLPC10 := $(SUB_LPC10)/liblpc10.a

SUB_DIRS := \
	$(SUB_GSM) \
	$(SUB_ILBC) \
	$(SUB_LPC10) \

all: _all

include $(ASTTOPDIR)/Makefile.moddir_rules

ifneq ($(GSM_INTERNAL),no)
GSM_INCLUDE := -I$(SUB_GSM)/inc
endif

$(SUB_GSM):
	@mkdir -p $(SUB_GSM)/lib
	@$(MAKE) -C $(SUB_GSM) lib/libgsm.a

# Don't run the implicit rules for this target.
$(SUB_GSM)/lib/libgsm.a: $(SUB_GSM) ;

ifneq ($(GSM_INTERNAL),no)
$(if $(filter codec_gsm,$(EMBEDDED_MODS)),modules.link,codec_gsm.so): $(SUB_GSM)/lib/libgsm.a
endif

clean::
	for dir in $(SUB_DIRS); do \
		$(MAKE) -C $$dir clean; \
	done
	rm -f g722/*.[oa]

.PHONY: $(SUB_DIRS)

$(SUB_LPC10):
	@$(MAKE) -C $(SUB_LPC10) all

# Don't run the implicit rules for this target.
$(LIBLPC10): $(SUB_LPC10) ;

$(if $(filter codec_lpc10,$(EMBEDDED_MODS)),modules.link,codec_lpc10.so): $(LIBLPC10)


$(SUB_ILBC):
	@$(MAKE) -C $(SUB_ILBC) all _ASTCFLAGS="$(filter-out -Wmissing-prototypes -Wmissing-declarations -Wshadow,$(_ASTCFLAGS)) $(AST_NO_STRICT_OVERFLOW)"

# Don't run the implicit rules for this target.
$(LIBILBC): $(SUB_ILBC) ;

$(if $(filter codec_ilbc,$(EMBEDDED_MODS)),modules.link,codec_ilbc.so): $(LIBILBC)


$(if $(filter codec_g722,$(EMBEDDED_MODS)),modules.link,codec_g722.so): g722/g722_encode.o g722/g722_decode.o

g722/g722_encode.o g722/g722_decode.o: _ASTCFLAGS+=$(call MOD_ASTCFLAGS,codec_g722)
