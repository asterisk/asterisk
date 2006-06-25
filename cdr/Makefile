#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for CDR backends
#
# Copyright (C) 1999-2006, Digium, Inc.
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

ifneq ($(wildcard ../menuselect.makeopts),)
  include ../menuselect.makeopts
  include ../menuselect.makedeps
endif

SELECTED_MODS:=$(filter-out $(MENUSELECT_CDR),$(patsubst %.c,%,$(wildcard cdr_*.c)))

ifneq ($(FREETDS_LIB),)
  ifeq ($(shell grep -s TDS_VERSION_NO $(CROSS_COMPILE_TARGET)/usr/include/tdsver.h $(CROSS_COMPILE_TARGET)/usr/local/include/tdsver.h $(CROSS_COMPILE_TARGET)/usr/include/freetds/tdsver.h | grep -c 0.63),1)
    CFLAGS += -DFREETDS_0_63
  else
	ifeq ($(shell grep -s TDS_VERSION_NO $(CROSS_COMPILE_TARGET)/usr/include/tdsver.h $(CROSS_COMPILE_TARGET)/usr/local/include/tdsver.h $(CROSS_COMPILE_TARGET)/usr/include/freetds/tdsver.h | grep -c 0.62),1)
      CFLAGS += -DFREETDS_0_62
    else
      CFLAGS += -DFREETDS_PRE_0_62
    endif
  endif
endif

all: _all

include ../Makefile.rules
