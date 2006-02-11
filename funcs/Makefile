#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for dialplan functions
#
# Copyright (C) 2005 - 2006, Digium
#
# Kevin P. Fleming <kpfleming@digium.com>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

BUILD_MODS:=$(patsubst %.c,%.o,$(wildcard func_*.c))

ifeq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/include/odbcinst.h)$(wildcard $(CROSS_COMPILE_TARGET)/usr/local/include/odbcinst.h),)
  BUILD_MODS:=$(filter-out func_odbc.o,$(BUILD_MODS))
endif

MODS:=$(BUILD_MODS:.o=.so)

ifeq (${OSARCH},CYGWIN)
CYGSOLINK=-Wl,--out-implib=lib$@.a -Wl,--export-all-symbols
CYGSOLIB=-L.. -L. -lasterisk.dll
else
CFLAGS+=-fPIC
endif

all: $(MODS)

clean:
	rm -f *.so *.o .depend

%.so : %.o
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB}

func_odbc.so: func_odbc.o
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB} -lodbc

install: all
	for x in $(MODS); do $(INSTALL) -m 755 $$x $(DESTDIR)$(MODULES_DIR) ; done

uninstall:

ifneq ($(wildcard .depend),)
  include .depend
endif

depend: .depend

.depend:
	../build_tools/mkdep $(CFLAGS) `ls *.c`

env:
	env
