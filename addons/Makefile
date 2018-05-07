#
# Asterisk -- An open source telephony toolkit.
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

OOH323C:=ooCmdChannel.c ooLogChan.c ooUtils.c ooGkClient.c context.c \
	ooDateTime.c decode.c dlist.c encode.c errmgmt.c \
	memheap.c ootrace.c oochannels.c ooh245.c ooports.c \
	ooq931.c ooCapability.c ooSocket.c perutil.c eventHandler.c \
	ooCalls.c ooStackCmds.c ooh323.c ooh323ep.c printHandler.c \
	rtctype.c ooTimer.c h323/H235-SECURITY-MESSAGESDec.c \
	h323/H235-SECURITY-MESSAGESEnc.c h323/H323-MESSAGES.c h323/H323-MESSAGESDec.c \
	h323/H323-MESSAGESEnc.c h323/MULTIMEDIA-SYSTEM-CONTROL.c \
	h323/MULTIMEDIA-SYSTEM-CONTROLDec.c h323/MULTIMEDIA-SYSTEM-CONTROLEnc.c

H323SOURCE:=$(addprefix ooh323c/src/,$(OOH323C)) ooh323cDriver.c

H323CFLAGS:=-Iooh323c/src -Iooh323c/src/h323

ALL_C_MODS:=app_mysql \
            cdr_mysql \
            chan_mobile \
            chan_ooh323 \
            format_mp3 \
            res_config_mysql

all: check_mp3 _all

check_mp3:
ifeq ($(filter format_mp3,$(MENUSELECT_ADDONS)),)
	@if [ ! -f mp3/Makefile ] ; then \
		echo ; \
		echo "**************************************************************" ; \
		echo "***                                                        ***" ; \
		echo "***    ---> IMPORTANT INFORMATION ABOUT format_mp3 <---    ***" ; \
		echo "***                                                        ***" ; \
		echo "*** format_mp3 has been selected to be installed, but the  ***" ; \
		echo "*** MP3 decoder library has not yet been downloaded into   ***" ; \
		echo "*** the source tree.  To do so, please run the following   ***" ; \
		echo "*** command:                                               ***" ; \
		echo "***                                                        ***" ; \
		echo "***          $$ contrib/scripts/get_mp3_source.sh           ***" ; \
		echo "***                                                        ***" ; \
		echo "**************************************************************" ; \
		echo ; \
	fi
endif

include $(ASTTOPDIR)/Makefile.moddir_rules

chan_mobile.o: _ASTCFLAGS+=$(AST_NO_FORMAT_TRUNCATION)
chan_ooh323.so: _ASTCFLAGS+=$(H323CFLAGS)
$(call MOD_ADD_C,chan_ooh323,$(H323SOURCE))

ifneq ($(wildcard mp3/Makefile),)
$(call MOD_ADD_C,format_mp3,mp3/common.c mp3/dct64_i386.c mp3/decode_ntom.c mp3/layer3.c mp3/tabinit.c mp3/interface.c)

.PHONY: check_mp3
else
.PHONY: check_mp3 format_mp3.o format_mp3.so
endif
