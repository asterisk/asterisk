#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for resource modules
#
# Copyright (C) 1999-2006, Digium
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

SELECTED_MODS:=$(filter-out $(MENUSELECT_RES),$(patsubst %.c,%,$(wildcard res_*.c)))

ifeq ($(OSARCH),FreeBSD)
  SELECTED_MODS:=$(filter-out $(shell if test ${BSDVERSION} -lt 500000 ; then echo "res_config_odbc"; fi),$(SELECTED_MODS))
endif

MODS:=$(patsubst %,%.so,$(SELECTED_MODS))

all: $(MODS)

install: all
	for x in $(MODS); do $(INSTALL) -m 755 $$x $(DESTDIR)$(MODULES_DIR) ; done
	@if [ x`which mpg123 2>/dev/null | grep -v '^no'` != x ] ; then \
	  if mpg123 --longhelp 2>&1 | grep -q .59r 2>&1 >/dev/null ; then echo ; else \
			echo "*************************************************************";\
			echo "*** You have the WRONG version of mpg123... you need .59r ***";\
			echo "*** Use 'make mpg123' to get the right verison            ***";\
			echo "*************************************************************";\
	  fi ;\
	else \
		echo "*** You don't have mpg123 installed. You may need ***";\
		echo "***       it if you want to use MusicOnHold       ***";\
	fi

uninstall:

res_crypto.so: res_crypto.o
	$(CC) $(SOLINK) -o $@ $< $(SSL_LIB)

res_crypto.o: res_crypto.c
	$(CC) -c -o $@ $(CFLAGS) $(SSL_INCLUDE) $<

clean-depend:
	rm -f .depend

clean: clean-depend
	rm -f *.so *.o
	rm -f snmp/*.o

res_odbc.so: res_odbc.o
	$(CC) $(SOLINK) -o $@ $< $(ODBC_LIB) 

res_odbc.o: res_odbc.c
	$(CC) -c -o $@ $(CFLAGS) $(ODBC_INCLUDE) $<

res_jabber.o: res_jabber.c
	$(CC) -c -o $@ $(CFLAGS) $(IKSEMEL_INCLUDE) $<

res_jabber.so: res_jabber.o
	$(CC) $(SOLINK) -o $@ $< $(IKSEMEL_LIB) 

res_osp.so: res_osp.o
	$(CC) $(SOLINK) -o $@ $< $(OSPTK_LIB)

res_osp.o: res_osp.c
	$(CC) -c -o $@ $(CFLAGS) $(OSPTK_INCLUDE) $<

%.so : %.o
	$(CC) -o $@ $(SOLINK) $<

res_config_odbc.o: res_config_odbc.c
	$(CC) -c -o $@ $(CFLAGS) $(ODBC_INCLUDE) $<

res_snmp.so: res_snmp.o snmp/agent.o
	$(CC) $(SOLINK) -o $@ $^ $(NETSNMP_LIBS)

res_config_pgsql.so: res_config_pgsql.o
	$(CC) $(SOLINK) -o $@ $< $(PGSQL_LIB)

res_config_pgsql.o: res_config_pgsql.c
	$(CC) -c -o $@ $(CFLAGS) $(PGSQL_INCLUDE) $<

ifneq ($(wildcard .depend),)
  include .depend
endif

depend: .depend

.depend:
	../build_tools/mkdep $(CFLAGS) `ls *.c`	

env:
	env
