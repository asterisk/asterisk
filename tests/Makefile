#
# Asterisk -- An open source telephony toolkit.
#
# Makefile for test modules
#
# Copyright (C) 2008, Digium, Inc.
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

-include $(ASTTOPDIR)/menuselect.makeopts $(ASTTOPDIR)/menuselect.makedeps

MODULE_PREFIX=test
MENUSELECT_CATEGORY=TESTS
MENUSELECT_DESCRIPTION=Test Modules

all: _all

include $(ASTTOPDIR)/Makefile.moddir_rules

test_astobj2.o: _ASTCFLAGS+=$(call get_menuselect_cflags,AO2_DEBUG)
# can't use '%y' in strftime() without warnings since it's not y2k compliant
test_capture.o: _ASTCFLAGS+=$(AST_NO_FORMAT_Y2K)
test_crypto.o: _ASTCFLAGS+=$(AST_NO_FORMAT_TRUNCATION)
test_strings.o: _ASTCFLAGS+=$(AST_NO_FORMAT_TRUNCATION) $(AST_NO_STRINGOP_TRUNCATION)
test_voicemail_api.o: _ASTCFLAGS+=$(AST_NO_FORMAT_TRUNCATION)
