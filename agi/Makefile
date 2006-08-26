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

.PHONY: clean clean-depend all depend uninstall

AGIS=agi-test.agi eagi-test eagi-sphinx-test jukebox.agi

ifeq ($(OSARCH),SunOS)
  LIBS+=-lsocket -lnsl ../main/strcompat.o
endif

include $(ASTTOPDIR)/Makefile.rules

all: $(AGIS)

eagi-test: eagi-test.o

eagi-sphinx-test: eagi-sphinx-test.o

install: all
	mkdir -p $(DESTDIR)$(AGI_DIR)
	for x in $(AGIS); do $(INSTALL) -m 755 $$x $(DESTDIR)$(AGI_DIR) ; done

uninstall:
	for x in $(AGIS); do rm -f $(DESTDIR)$(AGI_DIR)/$$x ; done

clean-depend:
	rm -f .depend

clean: clean-depend
	rm -f *.so *.o look eagi-test eagi-sphinx-test

-include .depend

depend: .depend

.depend:
	../build_tools/mkdep $(CFLAGS) `ls *.c`
