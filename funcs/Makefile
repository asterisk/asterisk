#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for dialplan functions
#
# Copyright (C) 2005-2006, Digium, Inc.
#
# Kevin P. Fleming <kpfleming@digium.com>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

MODS:=$(patsubst %.c,%.so,$(wildcard func_*.c))

ifeq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/include/odbcinst.h $(CROSS_COMPILE_TARGET)/usr/local/include/odbcinst.h),)
  MODS:=$(filter-out func_odbc.so,$(MODS))
endif

ifneq ($(shell if [[ 0x`$(CROSS_COMPILE_BIN)curl-config --vernum` -ge 0x70907 ]]; then echo "OK" ; fi),)
  CURLLIBS:=$(shell $(CROSS_COMPILE_BIN)curl-config --libs)
endif

ifeq (${CURLLIBS},)
  MODS:=$(filter-out func_curl.so,$(MODS))
endif

ifeq (${OSARCH},CYGWIN)
  CYGSOLINK=-Wl,--out-implib=lib$@.a -Wl,--export-all-symbols
  CYGSOLIB=-L.. -L. -lasterisk.dll
else
  CFLAGS+=-fPIC
endif

all: $(MODS)

clean-depend:
	rm -f .depend

clean: clean-depend
	rm -f *.so *.o

%.so : %.o
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB}

func_curl.so: func_curl.o
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB} $(CURLLIBS)

func_odbc.so: func_odbc.o
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB} -lodbc

install: all
	for x in $(MODS); do $(INSTALL) -m 755 $$x $(DESTDIR)$(MODULES_DIR) ; done
	rm -f $(DESTDIR)$(MODULES_DIR)/pbx_functions.so

uninstall:

ifneq ($(wildcard .depend),)
  include .depend
endif

depend: .depend

.depend:
	../build_tools/mkdep $(CFLAGS) `ls *.c`

env:
	env
