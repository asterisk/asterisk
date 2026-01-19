#
# Asterisk -- An open source telephony toolkit.
#
# Makefile for CEL backends
#
# Copyright (C) 1999-2008, Digium, Inc.
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

-include $(ASTTOPDIR)/menuselect.makeopts $(ASTTOPDIR)/menuselect.makedeps

MODULE_PREFIX=cel
MENUSELECT_CATEGORY=CEL
MENUSELECT_DESCRIPTION=Channel Event Logging

all: _all

include $(ASTTOPDIR)/Makefile.moddir_rules

# Our build system only allows a source file to be included in one module
# so cel_custom gets it and cel_sqlite3_custom just adds it as
# a regular 'make' dependency.
$(call MOD_ADD_C,cel_custom,custom_common.c)
cel_sqlite3_custom.so: custom_common.o
