#
# Asterisk -- A telephony toolkit for Linux.
#
# Makefile for Add-on Modules
#
# Copyright (C) 2009, Digium, Inc.
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

-include $(ASTTOPDIR)/menuselect.makeopts $(ASTTOPDIR)/menuselect.makedeps

MENUSELECT_CATEGORY=ADDONS
MENUSELECT_DESCRIPTION=Add-ons (See README-addons.txt)

H323OBJS:=ooCmdChannel.o ooLogChan.o ooUtils.o ooGkClient.o context.o \
	ooDateTime.o decode.o dlist.o encode.o errmgmt.o \
	memheap.o ootrace.o oochannels.o ooh245.o ooports.o \
	ooq931.o ooCapability.o ooSocket.o perutil.o eventHandler.o \
	ooCalls.o ooStackCmds.o ooh323.o ooh323ep.o printHandler.o \
	rtctype.o ooTimer.o h323/H235-SECURITY-MESSAGESDec.o \
	h323/H235-SECURITY-MESSAGESEnc.o h323/H323-MESSAGES.o h323/H323-MESSAGESDec.o \
	h323/H323-MESSAGESEnc.o h323/MULTIMEDIA-SYSTEM-CONTROL.o \
	h323/MULTIMEDIA-SYSTEM-CONTROLDec.o h323/MULTIMEDIA-SYSTEM-CONTROLEnc.o

H323CFLAGS:=-Iooh323c/src -Iooh323c/src/h323

ALL_C_MODS:=app_mysql \
            app_saycountpl \
            cdr_mysql \
            chan_mobile \
            chan_ooh323 \
            format_mp3 \
            res_config_mysql

all: _all

include $(ASTTOPDIR)/Makefile.moddir_rules

clean::
	$(MAKE) -C mp3 clean
	rm -f $(addprefix ooh323c/src/,$(H323OBJS))

$(if $(filter format_mp3,$(EMBEDDED_MODS)),modules.link,format_mp3.so): mp3/common.o mp3/dct64_i386.o mp3/decode_ntom.o mp3/layer3.o mp3/tabinit.o mp3/interface.o

chan_ooh323.o: _ASTCFLAGS+=$(H323CFLAGS)

$(if $(filter chan_ooh323,$(EMBEDDED_MODS)),modules.link,chan_ooh323.so): _ASTCFLAGS+=$(H323CFLAGS)
$(if $(filter chan_ooh323,$(EMBEDDED_MODS)),modules.link,chan_ooh323.so): $(addprefix ooh323c/src/,$(H323OBJS)) chan_ooh323.o ooh323cDriver.o
