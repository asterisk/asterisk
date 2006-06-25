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

SELECTED_MODS:=$(filter-out $(MENUSELECT_CODECS),$(patsubst %.c,%,$(wildcard codec_*.c)))

ifeq ($(wildcard ilbc/iLBC_decode.h),)
  MODS:=$(filter-out codec_ilbc,$(MODS))
else
  LIBILBC:=ilbc/libilbc.a
endif

LIBLPC10:=lpc10/liblpc10.a

all: _all

ifeq ($(LIBGSM_LIB),internal)
LIBGSM_INCLUDE:=-Igsm/inc
LIBGSM_LIB:=

codec_gsm.so: gsm/lib/libgsm.a
endif

include ../Makefile.rules

clean::
	$(MAKE) -C gsm clean
	$(MAKE) -C lpc10 clean
	$(MAKE) -C ilbc clean

gsm/lib/libgsm.a:
	CFLAGS="$(ASTCFLAGS) -I." $(MAKE) -C gsm lib/libgsm.a

$(LIBLPC10):
	CFLAGS="$(ASTCFLAGS)" $(MAKE) -C lpc10 all

codec_lpc10.so: $(LIBLPC10)

$(LIBILBC):
	CFLAGS="$(ASTCFLAGS)" $(MAKE) -C ilbc all

codec_ilbc.so: $(LIBILBC)
