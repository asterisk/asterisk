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

ifneq ($(wildcard ../menuselect.makeopts),)
  include ../menuselect.makeopts
  include ../menuselect.makedeps
endif

SELECTED_MODS:=$(filter-out $(MENUSELECT_CHANNELS),$(patsubst %.c,%,$(wildcard chan_*.c)))

ifeq ($(OSARCH),OpenBSD)
  PTLIB=-lpt_OpenBSD_x86_r
  H323LIB=-lh323_OpenBSD_x86_r
endif

ifeq ($(OSARCH),Linux)
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
  SELECTED_MODS:=$(filter-out chan_h323,$(SELECTED_MODS))
endif

ifeq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/include/mISDNuser/mISDNlib.h),)
  SELECTED_MODS:=$(filter-out chan_misdn,$(SELECTED_MODS))
else
  CFLAGS+=-Imisdn 
endif

ifndef OPENH323DIR
  OPENH323DIR=$(HOME)/openh323
endif

ifndef PWLIBDIR
  PWLIBDIR=$(HOME)/pwlib
endif

MENUSELECT_OPTS_chan_misdn+=CHAN_MISDN_VERSION=\"0.3.0\"

all: _all

include ../Makefile.rules

clean::
	rm -f busy.h ringtone.h gentone gentone-ulaw

ifneq ($(wildcard h323/Makefile.ast),)
  include h323/Makefile.ast
endif

ifneq ($(wildcard misdn/Makefile.ast),)
  include misdn/Makefile.ast
endif

gentone gentone-ulaw: %: %.c
	$(HOST_CC) -o $@ $< -lm

busy.h: gentone
	./gentone busy 480 620

ringtone.h: gentone
	./gentone ringtone 440 480

chan_oss.o: chan_oss.c busy.h ringtone.h

chan_iax2.so: iax2-parser.o iax2-provision.o

chan_alsa.o: busy.h ringtone.h

chan_vpb.o: chan_vpb.c
	$(CXX) -c $(CFLAGS:-Werror=) -o $@ chan_vpb.c

chan_vpb.so: chan_vpb.o
	 $(CXX) $(SOLINK) -o $@ $< -lvpb -lpthread -lm -ldl

ifeq ($(OSARCH),Linux)
chan_h323.so: chan_h323.o h323/libchanh323.a h323/Makefile.ast
	$(CC) $(SOLINK) $(H323LDFLAGS) -o $@ $< h323/libchanh323.a $(H323LDLIBS) -lstdc++
else
chan_h323.so: chan_h323.o h323/libchanh323.a
	$(CC) $(SOLINK) -o $@ $< h323/libchanh323.a $(CHANH323LIB) -L$(PWLIBDIR)/lib $(PTLIB) -L$(OPENH323DIR)/lib $(H323LIB) -L/usr/lib -lcrypto -lssl -lexpat
endif

misdn/chan_misdn_lib.a:
	make -C misdn

chan_misdn.so: misdn_config.o misdn/chan_misdn_lib.a
	$(CC) $(SOLINK) -o $@ $^ -lisdnnet -lmISDN
