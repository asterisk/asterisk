#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for AGI-related stuff
#
# Copyright (C) 1999-2005, Digium
#
# Mark Spencer <markster@digium.com>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

AGIS=agi-test.agi eagi-test eagi-sphinx-test

CFLAGS+=-DNO_AST_MM

LIBS=
ifeq ($(OSARCH),SunOS)
  LIBS=-lsocket -lnsl ../strcompat.o
endif

ifeq ($(findstring BSD,${OSARCH}),BSD)
  CFLAGS+=-I$(CROSS_COMPILE_TARGET)/usr/local/include -L$(CROSS_COMPILE_TARGET)/usr/local/lib
endif

all: depend $(AGIS)

install: all
	mkdir -p $(DESTDIR)$(AGI_DIR)
	for x in $(AGIS); do $(INSTALL) -m 755 $$x $(DESTDIR)$(AGI_DIR) ; done

eagi-test: eagi-test.o
	$(CC) $(CFLAGS) -o eagi-test eagi-test.o $(LIBS)

eagi-sphinx-test: eagi-sphinx-test.o
	$(CC) $(CFLAGS) -o eagi-sphinx-test eagi-sphinx-test.o $(LIBS) 

clean:
	rm -f *.so *.o look .depend eagi-test eagi-sphinx-test

%.so : %.o
	$(CC) -shared -Xlinker -x -o $@ $<

ifneq ($(wildcard .depend),)
include .depend
endif

depend: .depend

.depend:
	../build_tools/mkdep $(CFLAGS) `ls *.c`
