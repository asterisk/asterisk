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

$(call MOD_ADD_C,chan_iax2,$(wildcard iax2/*.c))
$(call MOD_ADD_C,chan_sip,$(wildcard sip/*.c))
$(call MOD_ADD_C,chan_pjsip,$(wildcard pjsip/*.c))
$(call MOD_ADD_C,chan_dahdi,$(wildcard dahdi/*.c) sig_analog.c sig_pri.c sig_ss7.c)
$(call MOD_ADD_C,chan_misdn,misdn_config.c misdn/isdn_lib.c misdn/isdn_msg_parser.c)

chan_dahdi.o: _ASTCFLAGS+=$(call get_menuselect_cflags,LOTS_OF_SPANS)
chan_mgcp.o: _ASTCFLAGS+=$(AST_NO_FORMAT_TRUNCATION)
chan_unistim.o: _ASTCFLAGS+=$(AST_NO_FORMAT_TRUNCATION)
chan_phone.o: _ASTCFLAGS+=$(AST_NO_FORMAT_TRUNCATION)
chan_sip.o: _ASTCFLAGS+=$(AST_NO_FORMAT_TRUNCATION)

chan_misdn.o: _ASTCFLAGS+=-Imisdn
misdn_config.o: _ASTCFLAGS+=-Imisdn
misdn/isdn_lib.o: _ASTCFLAGS+=-Wno-strict-aliasing

$(call MOD_ADD_C,chan_oss,console_video.c vgrabbers.c console_board.c)
