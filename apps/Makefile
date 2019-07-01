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

all: _all

include $(ASTTOPDIR)/Makefile.moddir_rules

.app_voicemail.moduleinfo: app_voicemail.c
	@echo "<member name=\"app_voicemail\" displayname=\"$(shell $(GREP) -e AST_MODULE_INFO $< | head -n 1 | cut -d '"' -f 2)\" remove_on_change=\"$(SUBDIR)/$*.o $(SUBDIR)/$*.so\">" > $@
	echo "<support_level>core</support_level>"  >> $@
	echo "<defaultenabled>yes</defaultenabled>" >> $@
	echo "<use type=\"module\">res_adsi</use>" >> $@
	echo "<use type=\"module\">res_smdi</use>" >> $@
	echo "</member>" >> $@

.app_voicemail_imap.moduleinfo: app_voicemail_imap.c
	@echo "<member name=\"app_voicemail_imap\" displayname=\"$(shell $(GREP) -e AST_MODULE_INFO $< | head -n 1 | cut -d '"' -f 2)\" remove_on_change=\"$(SUBDIR)/$*.o $(SUBDIR)/$*.so\">" > $@
	echo "<support_level>core</support_level>"  >> $@
	echo "<defaultenabled>no</defaultenabled>" >> $@
	echo "<depend>imap_tk</depend>" >> $@
	echo "<use type=\"module\">res_adsi</use>" >> $@
	echo "<use type=\"module\">res_smdi</use>" >> $@
	echo "</member>" >> $@

.app_voicemail_odbc.moduleinfo: app_voicemail_odbc.c
	@echo "<member name=\"app_voicemail_odbc\" displayname=\"$(shell $(GREP) -e AST_MODULE_INFO $< | head -n 1 | cut -d '"' -f 2)\" remove_on_change=\"$(SUBDIR)/$*.o $(SUBDIR)/$*.so\">" > $@
	echo "<support_level>core</support_level>"  >> $@
	echo "<defaultenabled>no</defaultenabled>" >> $@
	echo "<depend>generic_odbc</depend>" >> $@
	echo "<use type=\"module\">res_adsi</use>" >> $@
	echo "<use type=\"module\">res_smdi</use>" >> $@
	echo "</member>" >> $@

$(call MOD_ADD_C,app_confbridge,$(wildcard confbridge/*.c))

app_confbridge.o: _ASTCFLAGS+=$(AST_NO_FORMAT_TRUNCATION)
app_meetme.o: _ASTCFLAGS+=$(AST_NO_FORMAT_TRUNCATION)
app_minivm.o: _ASTCFLAGS+=$(AST_NO_FORMAT_TRUNCATION)
app_voicemail.o: _ASTCFLAGS+=$(AST_NO_FORMAT_TRUNCATION) -DFILE_STORAGE
app_voicemail_odbc.o: _ASTCFLAGS+=$(AST_NO_FORMAT_TRUNCATION) -DODBC_STORAGE
app_voicemail_imap.o: _ASTCFLAGS+=$(AST_NO_FORMAT_TRUNCATION) -DIMAP_STORAGE
app_while.o: _ASTCFLAGS+=$(AST_NO_FORMAT_TRUNCATION)

ifneq ($(findstring $(OSARCH), mingw32 cygwin ),)
  LIBS+= -lres_ael_share.so -lres_monitor.so -lres_speech.so
  LIBS+= -lres_smdi.so
endif
