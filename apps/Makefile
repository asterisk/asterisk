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

.PHONY: clean clean-depend all depend uninstall

ifneq ($(wildcard ../menuselect.makeopts),)
  include ../menuselect.makeopts
endif

SELECTED_MODS:=$(filter-out $(MENUSELECT_APPS),$(patsubst %.c,%,$(wildcard app_*.c)))

MODS:=$(patsubst %,%.so,$(SELECTED_MODS))

app_voicemail.o: CFLAGS+=$(patsubst %,-D%,$(MENUSELECT_app_voicemail))

all: $(MODS)

clean-depend:
	rm -f .depend

clean: clean-depend
	rm -f *.so *.o

%.so: %.o
	$(CC) $(SOLINK) -o $@ $<

app_rpt.so: app_rpt.o
	$(CC) $(SOLINK) -o $@ $< $(ZAPTEL_LIB)

app_rpt.o: app_rpt.c
	$(CC) -c -o $@ $(CFLAGS) $(ZAPTEL_INCLUDE) $<

install: all
	for x in $(MODS); do $(INSTALL) -m 755 $$x $(DESTDIR)$(MODULES_DIR) ; done

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
