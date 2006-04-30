#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for CDR backends
#
# Copyright (C) 1999-2006, Digium, Inc.
#
# Mark Spencer <markster@digium.com>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

MODS:=$(filter-out $(MENUSELECT_CDR),$(patsubst %.c,%.so,$(wildcard cdr_*.c)))

#The problem with sparc is the best stuff is in newer versions of gcc (post 3.0) only.
#This works for even old (2.96) versions of gcc and provides a small boost either way.
#A ultrasparc cpu is really v9 but the stock debian stable 3.0 gcc doesn.t support it.
#So we go lowest common available by gcc and go a step down, still a step up from
#the default as we now have a better instruction set to work with. - Belgarath
ifeq ($(PROC),sparc64)
  PROC=ultrasparc
  CFLAGS += -mtune=$(PROC) -pipe -fomit-frame-pointer -mcpu=v8
endif

ifneq ($(TDS_LIB),)
  ifeq ($(shell grep -s TDS_VERSION_NO $(CROSS_COMPILE_TARGET)/usr/include/tdsver.h $(CROSS_COMPILE_TARGET)/usr/local/include/tdsver.h $(CROSS_COMPILE_TARGET)/usr/include/freetds/tdsver.h | grep -c 0.63),1)
    CFLAGS += -DFREETDS_0_63
  else
	ifeq ($(shell grep -s TDS_VERSION_NO $(CROSS_COMPILE_TARGET)/usr/include/tdsver.h $(CROSS_COMPILE_TARGET)/usr/local/include/tdsver.h $(CROSS_COMPILE_TARGET)/usr/include/freetds/tdsver.h | grep -c 0.62),1)
      CFLAGS += -DFREETDS_0_62
    else
      CFLAGS += -DFREETDS_PRE_0_62
    endif
  endif
endif

all: depend $(MODS)

install: all
	for x in $(MODS); do $(INSTALL) -m 755 $$x $(DESTDIR)$(MODULES_DIR) ; done

uninstall:

clean-depend:
	rm -f .depend

clean: clean-depend
	rm -f *.so *.o

%.so : %.o
	$(CC) $(SOLINK) -o $@ $<

cdr_odbc.so: cdr_odbc.o
	$(CC) $(SOLINK) -o $@ $< $(ODBC_LIB)

cdr_odbc.o: cdr_odbc.c
	$(CC) -c -o $@ $(CFLAGS) $(ODBC_INCLUDE) $<

cdr_tds.so: cdr_tds.o
	$(CC) $(SOLINK) -o $@ $< $(TDS_LIB)

cdr_tds.o: cdr_tds.c
	$(CC) -c -o $@ $(CFLAGS) $(TDS_INCLUDE) $<

cdr_pgsql.so: cdr_pgsql.o
	$(CC) $(SOLINK) -o $@ $< $(PGSQL_LIB)

cdr_pgsql.o: cdr_pgsql.c
	$(CC) -c -o $@ $(CFLAGS) $(PGSQL_INCLUDE) $<

cdr_sqlite.so: cdr_sqlite.o
	$(CC) $(SOLINK) -o $@ $< $(SQLITE_LIB)

cdr_sqlite.o: cdr_sqlite.c
	$(CC) -c -o $@ $(CFLAGS) $(SQLITE_INCLUDE) $<

ifneq ($(wildcard .depend),)
  include .depend
endif

depend: .depend

.depend:
	../build_tools/mkdep $(CFLAGS) `ls *.c`
