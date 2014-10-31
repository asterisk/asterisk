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
	if [ -n "$(findstring REF_DEBUG,$(MENUSELECT_CFLAGS))" ]; then \
		$(INSTALL) -d "$(DESTDIR)$(ASTDATADIR)/scripts"; \
		$(INSTALL) -m 755 scripts/refcounter.py "$(DESTDIR)$(ASTDATADIR)/scripts/refcounter.py"; \
	fi

uninstall:
	rm -f "$(DESTDIR)$(ASTDATADIR)/scripts/refcounter.py"
