#
# Asterisk -- An open source telephony toolkit.
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

all: _all

include $(ASTTOPDIR)/Makefile.moddir_rules

ifneq ($(findstring $(OSARCH), mingw32 cygwin ),)
  LIBS+= -lres_monitor.so
endif

clean::
	$(MAKE) -C misdn clean
	rm -f dahdi/*.o dahdi/*.i dahdi/*.gcda dahdi/*.gcno
	rm -f sip/*.o sip/*.i sip/*.gcda sip/*.gcno
	rm -f iax2/*.o iax2/*.i iax2/*.gcda iax2/*.gcno
	rm -f pjsip/*.o pjsip/*.i pjsip/*.gcda pjsip/*.gcno

$(if $(filter chan_iax2,$(EMBEDDED_MODS)),modules.link,chan_iax2.so): $(subst .c,.o,$(wildcard iax2/*.c))
$(subst .c,.o,$(wildcard iax2/*.c)): _ASTCFLAGS+=$(call MOD_ASTCFLAGS,chan_iax2)

$(if $(filter chan_sip,$(EMBEDDED_MODS)),modules.link,chan_sip.so): $(subst .c,.o,$(wildcard sip/*.c))
$(subst .c,.o,$(wildcard sip/*.c)): _ASTCFLAGS+=$(call MOD_ASTCFLAGS,chan_sip)

$(if $(filter chan_pjsip,$(EMBEDDED_MODS)),modules.link,chan_pjsip.so): $(subst .c,.o,$(wildcard pjsip/*.c))
$(subst .c,.o,$(wildcard pjsip/*.c)): _ASTCFLAGS+=$(call MOD_ASTCFLAGS,chan_pjsip)

# Additional objects to combine with chan_dahdi.so
CHAN_DAHDI_OBJS= \
	$(subst .c,.o,$(wildcard dahdi/*.c))	\
	sig_analog.o	\
	sig_pri.o	\
	sig_ss7.o	\

$(if $(filter chan_dahdi,$(EMBEDDED_MODS)),modules.link,chan_dahdi.so): $(CHAN_DAHDI_OBJS)
$(CHAN_DAHDI_OBJS): _ASTCFLAGS+=$(call MOD_ASTCFLAGS,chan_dahdi)

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

