#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for AGI-related stuff
#
# Copyright (C) 1999-2006, Digium
#
# Mark Spencer <markster@digium.com>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

AGIS=agi-test.agi eagi-test eagi-sphinx-test jukebox.agi

LIBS=
ifeq ($(OSARCH),SunOS)
  LIBS=-lsocket -lnsl ../strcompat.o
endif

all: depend $(AGIS)

install: all
	mkdir -p $(DESTDIR)$(AGI_DIR)
	for x in $(AGIS); do $(INSTALL) -m 755 $$x $(DESTDIR)$(AGI_DIR) ; done

uninstall:
	for x in $(AGIS); do rm -f $(DESTDIR)$(AGI_DIR)/$$x ; done

eagi-test: eagi-test.o
	$(CC) $(CFLAGS) -include ../include/autoconfig.h -o eagi-test eagi-test.o $(LIBS)

eagi-sphinx-test: eagi-sphinx-test.o
	$(CC) $(CFLAGS) -include ../include/autoconfig.h -o eagi-sphinx-test eagi-sphinx-test.o $(LIBS) 

clean-depend:
	rm -f .depend

clean: clean-depend
	rm -f *.so *.o look eagi-test eagi-sphinx-test

%.so : %.o
	$(CC) -shared -Xlinker -x -o $@ $<

ifneq ($(wildcard .depend),)
include .depend
endif

depend: .depend

.depend:
	../build_tools/mkdep $(CFLAGS) `ls *.c`
