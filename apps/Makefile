#
# Asterisk -- An open source telephony toolkit.
#
# Makefile for PBX applications
#
# Copyright (C) 1999-2006, Digium, Inc.
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

-include $(ASTTOPDIR)/menuselect.makeopts $(ASTTOPDIR)/menuselect.makedeps

MODULE_PREFIX=app
MENUSELECT_CATEGORY=APPS
MENUSELECT_DESCRIPTION=Applications

MENUSELECT_OPTS_app_directory:=$(MENUSELECT_OPTS_app_voicemail)
ifneq ($(findstring ODBC_STORAGE,$(MENUSELECT_OPTS_app_voicemail)),)
  MENUSELECT_DEPENDS_app_voicemail+=$(MENUSELECT_DEPENDS_ODBC_STORAGE)
endif
ifneq ($(findstring IMAP_STORAGE,$(MENUSELECT_OPTS_app_voicemail)),)
  MENUSELECT_DEPENDS_app_voicemail+=$(MENUSELECT_DEPENDS_IMAP_STORAGE)
endif

all: _all

include $(ASTTOPDIR)/Makefile.moddir_rules

$(call MOD_ADD_C,app_confbridge,$(wildcard confbridge/*.c))

app_confbridge.o: _ASTCFLAGS+=$(AST_NO_FORMAT_TRUNCATION)
app_meetme.o: _ASTCFLAGS+=$(AST_NO_FORMAT_TRUNCATION)
app_minivm.o: _ASTCFLAGS+=$(AST_NO_FORMAT_TRUNCATION)
app_voicemail.o: _ASTCFLAGS+=$(AST_NO_FORMAT_TRUNCATION)
app_while.o: _ASTCFLAGS+=$(AST_NO_FORMAT_TRUNCATION)

ifneq ($(findstring $(OSARCH), mingw32 cygwin ),)
  LIBS+= -lres_ael_share.so -lres_monitor.so -lres_speech.so
  LIBS+= -lres_smdi.so
endif
