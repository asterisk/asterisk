#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for PBX applications
#
# Copyright (C) 1999-2006, Digium, Inc.
#
# Mark Spencer <markster@digium.com>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

MODS:=$(filter-out $(MENUSELECT_APPS),$(patsubst %.c,%.so,$(wildcard app_*.c)))

ifneq ($(WITH_SMDI),)
  CFLAGS+=-DWITH_SMDI
endif

# If you have UnixODBC you can use ODBC voicemail
# storage
#
# Uncomment to use ODBC storage
#CFLAGS+=-DUSE_ODBC_STORAGE
# Uncomment for extended ODBC voicemail storage
#CFLAGS+=-DEXTENDED_ODBC_STORAGE
# See doc/README.odbcstorage for more information

all: $(MODS)

clean-depend:
	rm -f .depend

clean: clean-depend
	rm -f *.so *.o

%.so: %.o
	$(CC) $(SOLINK) -o $@ $<

install: all
	for x in $(MODS); do $(INSTALL) -m 755 $$x $(DESTDIR)$(MODULES_DIR) ; done
	rm -f $(DESTDIR)$(MODULES_DIR)/app_cut.so
	rm -f $(DESTDIR)$(MODULES_DIR)/app_datetime.so
	rm -f $(DESTDIR)$(MODULES_DIR)/app_qcall.so

uninstall:

ifeq (SunOS,$(shell uname))
app_chanspy.so: app_chanspy.o
	$(CC) $(SOLINK) -o $@ $< -lrt
endif

ifneq ($(wildcard .depend),)
 include .depend
endif

depend: .depend

.depend:
	../build_tools/mkdep $(CFLAGS) `ls *.c`

env:
	env
