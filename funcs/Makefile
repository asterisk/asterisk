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

MODS:=$(filter-out $(MENUSELECT_FUNCS),$(patsubst %.c,%.so,$(wildcard func_*.c)))

# TODO This check needs to be handled with autoconf
ifeq ($(OSARCH),SunOS)
  ifeq ($(shell if [ 0x`$(CROSS_COMPILE_BIN)curl-config --vernum` -ge 0x70907 ]; then echo "OK" ; fi),)
    MODS:=$(filter-out func_curl.so,$(MODS))
  endif
else
  ifeq ($(shell if [[ 0x`$(CROSS_COMPILE_BIN)curl-config --vernum` -ge 0x70907 ]]; then echo "OK" ; fi),)
    MODS:=$(filter-out func_curl.so,$(MODS)) 
  endif
endif

all: $(MODS)

clean-depend:
	rm -f .depend

clean: clean-depend
	rm -f *.so *.o

%.so : %.o
	$(CC) $(SOLINK) -o $@ $<

func_curl.so: func_curl.o
	$(CC) $(SOLINK) -o $@ $< $(CURL_LIB)

func_curl.o: func_curl.c
	$(CC) -c -o $@ $(CFLAGS) $(CURL_INCLUDE) $<

func_odbc.so: func_odbc.o
	$(CC) $(SOLINK) -o $@ $< $(ODBC_LIB)

func_odbc.o: func_odbc.c
	$(CC) -c -o $@ $(CFLAGS) $(ODBC_INCLUDE) $<

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
