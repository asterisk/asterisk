#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for PBX frontends (dynamically loaded)
#
# Copyright (C) 1999, Mark Spencer
#
# Mark Spencer <markster@linux-support.net>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

AGIS=agi-test.agi eagi-test eagi-sphinx-test

CFLAGS+=

all: depend $(AGIS)

install: all
	mkdir -p $(DESTDIR)$(AGI_DIR)
	for x in $(AGIS); do $(INSTALL) -m 755 $$x $(DESTDIR)$(AGI_DIR) ; done

eagi-test: eagi-test.o
	$(CC) -o eagi-test eagi-test.o
	
eagi-sphinx-test: eagi-sphinx-test.o
	$(CC) -o eagi-sphinx-test eagi-sphinx-test.o

clean:
	rm -f *.so *.o look .depend eagi-test eagi-sphinx-test

%.so : %.o
	$(CC) -shared -Xlinker -x -o $@ $<

ifneq ($(wildcard .depend),)
include .depend
endif

depend: .depend

.depend:
	../mkdep $(CFLAGS) `ls *.c`
