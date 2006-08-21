#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for channel drivers
#
# Copyright (C) 1999-2006, Digium, Inc.
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

ifneq ($(wildcard $(ASTTOPDIR)/menuselect.makeopts),)
  include ../menuselect.makeopts
  include ../menuselect.makedeps
endif

C_MODS:=$(filter-out $(MENUSELECT_CHANNELS),$(patsubst %.c,%,$(wildcard chan_*.c)))
CC_MODS:=$(filter-out $(MENUSELECT_CHANNELS),$(patsubst %.cc,%,$(wildcard chan_*.cc)))

ifeq ($(OSARCH),OpenBSD)
  PTLIB=-lpt_OpenBSD_x86_r
  H323LIB=-lh323_OpenBSD_x86_r
endif

ifeq ($(OSARCH),linux-gnu)
  PTLIB=-lpt_linux_x86_r
  H323LIB=-lh323_linux_x86_r
  CHANH323LIB=-ldl
endif

ifeq ($(OSARCH),FreeBSD)
  PTLIB=-lpt_FreeBSD_x86_r
  H323LIB=-lh323_FreeBSD_x86_r
  CHANH323LIB=-pthread
endif

ifeq ($(OSARCH),NetBSD)
  PTLIB=-lpt_NetBSD_x86_r
  H323LIB=-lh323_NetBSD_x86_r
endif

ifeq ($(wildcard h323/libchanh323.a),)
  CC_MODS:=$(filter-out chan_h323,$(CC_MODS))
endif

ifndef OPENH323DIR
  OPENH323DIR=$(HOME)/openh323
endif

ifndef PWLIBDIR
  PWLIBDIR=$(HOME)/pwlib
endif

LOADABLE_MODS:=$(C_MODS) $(CC_MODS)

ifneq ($(findstring channels,$(MENUSELECT_EMBED)),)
  EMBEDDED_MODS:=$(LOADABLE_MODS)
  LOADABLE_MODS:=
endif

all: _all

include $(ASTTOPDIR)/Makefile.moddir_rules

clean::
	rm -f busy.h ringtone.h gentone
	make -C misdn clean

ifneq ($(wildcard $(PWD)/Makefile.ast),)
  include $(PWD)/Makefile.ast
endif

$(eval $(call ast_make_final_host,gentone,gentone.c))
gentone: LIBS+=-lm

busy.h: gentone
	./gentone busy 480 620

ringtone.h: gentone
	./gentone ringtone 440 480

chan_oss.o: busy.h ringtone.h

$(chan_iax2): iax2-parser.o iax2-provision.o

chan_alsa.o: busy.h ringtone.h

ifeq ($(OSARCH),linux-gnu)
chan_h323.so: chan_h323.o h323_module_interface.so h323/libchanh323.a h323/Makefile.ast
	$(CC) $(SOLINK) $(H323LDFLAGS) -o $@ $^ h323/libchanh323.a $(H323LDLIBS) -lstdc++
else
chan_h323.so: chan_h323.o h323_module_interface.so h323/libchanh323.a
	$(CC) $(SOLINK) -o $@ $^ h323/libchanh323.a $(CHANH323LIB) -L$(PWLIBDIR)/lib $(PTLIB) -L$(OPENH323DIR)/lib $(H323LIB) -L/usr/lib -lcrypto -lssl -lexpat
endif

chan_misdn.o: CFLAGS+=-Imisdn -DCHAN_MISDN_VERSION=\"0.3.0\" 

misdn_config.o: CFLAGS+=-Imisdn -DCHAN_MISDN_VERSION=\"0.3.0\" 

$(chan_misdn): chan_misdn.o misdn_config.o misdn/isdn_lib.o misdn/isdn_msg_parser.o
