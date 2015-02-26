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

-include $(ASTTOPDIR)/menuselect.makeopts $(ASTTOPDIR)/menuselect.makedeps

MODULE_PREFIX=chan
MENUSELECT_CATEGORY=CHANNELS
MENUSELECT_DESCRIPTION=Channel Drivers

ifeq ($(OSARCH),OpenBSD)
  PTLIB=-lpt
  H323LIB=-lh323
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
  MODULE_EXCLUDE += chan_h323
endif

ifndef OPENH323DIR
  OPENH323DIR=$(HOME)/openh323
endif

ifndef PWLIBDIR
  PWLIBDIR=$(HOME)/pwlib
endif

all: _all

include $(ASTTOPDIR)/Makefile.moddir_rules

ifneq ($(findstring $(OSARCH), mingw32 cygwin ),)
  LIBS+= -lres_monitor.so
endif

ifneq ($(wildcard h323/Makefile.ast),)
include h323/Makefile.ast
endif

clean::
	$(MAKE) -C misdn clean
	rm -f sip/*.o sip/*.i
	rm -f h323/libchanh323.a h323/Makefile.ast h323/*.o h323/*.dep

dist-clean::
	rm -f h323/Makefile

$(if $(filter chan_iax2,$(EMBEDDED_MODS)),modules.link,chan_iax2.so): iax2-parser.o iax2-provision.o
iax2-parser.o iax2-provision.o: _ASTCFLAGS+=$(call MOD_ASTCFLAGS,chan_iax2)
$(if $(filter chan_sip,$(EMBEDDED_MODS)),modules.link,chan_sip.so): $(subst .c,.o,$(wildcard sip/*.c))
$(subst .c,.o,$(wildcard sip/*.c)): _ASTCFLAGS+=$(call MOD_ASTCFLAGS,chan_sip)
$(if $(filter chan_dahdi,$(EMBEDDED_MODS)),modules.link,chan_dahdi.so): sig_analog.o sig_pri.o sig_ss7.o
sig_analog.o sig_pri.o sig_ss7.o: _ASTCFLAGS+=$(call MOD_ASTCFLAGS,chan_dahdi)

ifneq ($(filter chan_h323,$(EMBEDDED_MODS)),)
modules.link: h323/libchanh323.a
else
ifeq ($(OSARCH),linux-gnu)
chan_h323.so: chan_h323.o h323/libchanh323.a
	$(ECHO_PREFIX) echo "   [LD] $^ -> $@"
	$(CMD_PREFIX) $(CXX) $(PTHREAD_CFLAGS) $(_ASTLDFLAGS) $(ASTLDFLAGS) $(SOLINK) -o $@ $< h323/libchanh323.a $(H323LDLIBS)
else
chan_h323.so: chan_h323.o h323/libchanh323.a
	$(ECHO_PREFIX) echo "   [LD] $^ -> $@"
	$(CMD_PREFIX) $(CXX) $(PTHREAD_CFLAGS) $(_ASTLDFLAGS) $(ASTLDFLAGS) $(SOLINK) -o $@ $< h323/libchanh323.a $(CHANH323LIB) -L$(PWLIBDIR)/lib $(PTLIB) -L$(OPENH323DIR)/lib $(H323LIB) -L/usr/lib -lcrypto -lssl -lexpat
endif
endif

chan_misdn.o: _ASTCFLAGS+=-Imisdn

misdn_config.o: _ASTCFLAGS+=-Imisdn

misdn/isdn_lib.o: _ASTCFLAGS+=-Wno-strict-aliasing

$(if $(filter chan_misdn,$(EMBEDDED_MODS)),modules.link,chan_misdn.so): misdn_config.o misdn/isdn_lib.o misdn/isdn_msg_parser.o
misdn_config.o misdn/isdn_lib.o misdn/isdn_msg_parser.o: _ASTCFLAGS+=$(call MOD_ASTCFLAGS,chan_misdn)

$(if $(filter chan_oss,$(EMBEDDED_MODS)),modules.link,chan_oss.so): console_video.o vgrabbers.o console_board.o
console_video.o vgrabbers.o console_board.o: _ASTCFLAGS+=$(call MOD_ASTCFLAGS,chan_oss)

chan_usbradio.o: ./xpmr/xpmr.c ./xpmr/xpmr.h ./xpmr/xpmr_coef.h

chan_usbradio.so: LIBS+=-lusb -lasound
chan_usbradio.so: _ASTCFLAGS+=-DNDEBUG

h323/Makefile.ast:
	$(CMD_PREFIX) $(MAKE) -C h323 Makefile.ast

h323/libchanh323.a: h323/Makefile.ast
	$(CMD_PREFIX) $(MAKE) -C h323 libchanh323.a
