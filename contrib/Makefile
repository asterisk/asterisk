#
# Asterisk -- A telephony toolkit for Linux.
#
# Contrib scripts
#
# Copyright (C) 1999-2014, Digium, Inc.
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

ASTTOPDIR?=..
-include $(ASTTOPDIR)/menuselect.makeopts

.PHONY: all clean install uninstall

all:
clean:

include $(ASTTOPDIR)/Makefile.rules

install:
	$(INSTALL) -d "$(DESTDIR)$(ASTDATADIR)/scripts"
	$(INSTALL) -m 755 scripts/ast_logescalator "$(DESTDIR)$(ASTDATADIR)/scripts/ast_logescalator"
	$(INSTALL) -m 755 scripts/ast_loggrabber "$(DESTDIR)$(ASTDATADIR)/scripts/ast_loggrabber"
	$(INSTALL) -m 755 scripts/ast_coredumper "$(DESTDIR)$(ASTDATADIR)/scripts/ast_coredumper"
	$(INSTALL) -m 755 scripts/refcounter.py "$(DESTDIR)$(ASTDATADIR)/scripts/refcounter.py"
	$(INSTALL) -m 755 scripts/refstats.py "$(DESTDIR)$(ASTDATADIR)/scripts/refstats.py"
	$(INSTALL) -m 755 scripts/reflocks.py "$(DESTDIR)$(ASTDATADIR)/scripts/reflocks.py"

uninstall:
	-rm -f "$(DESTDIR)$(ASTDATADIR)/scripts/ast_logescalator"
	-rm -f "$(DESTDIR)$(ASTDATADIR)/scripts/ast_loggrabber"
	-rm -f "$(DESTDIR)$(ASTDATADIR)/scripts/ast_coredumper"
	-rm -f "$(DESTDIR)$(ASTDATADIR)/scripts/refcounter.py"
	-rm -f "$(DESTDIR)$(ASTDATADIR)/scripts/refstats.py"
	-rm -f "$(DESTDIR)$(ASTDATADIR)/scripts/reflocks.py"
