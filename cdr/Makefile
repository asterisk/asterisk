#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile for CDR backends (dynamically loaded)
#
# Copyright (C) 1999-2005, Digium
#
# Mark Spencer <markster@digium.com>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

MODS=cdr_csv.so cdr_manager.so cdr_custom.so

ifeq (${OSARCH},CYGWIN)
CYGSOLINK=-Wl,--out-implib=lib$@.a -Wl,--export-all-symbols
CYGSOLIB=-L.. -L. -lasterisk.dll
else
CFLAGS+=-fPIC
endif

ifeq ($(findstring BSD,${OSARCH}),BSD)
  CFLAGS+=-I$(CROSS_COMPILE_TARGET)/usr/local/include
  SOLINK+=-L$(CROSS_COMPILE_TARGET)/usr/local/lib
endif

#The problem with sparc is the best stuff is in newer versions of gcc (post 3.0) only.
#This works for even old (2.96) versions of gcc and provides a small boost either way.
#A ultrasparc cpu is really v9 but the stock debian stable 3.0 gcc doesn.t support it.
#So we go lowest common available by gcc and go a step down, still a step up from
#the default as we now have a better instruction set to work with. - Belgarath
ifeq ($(PROC),sparc64)
 PROC=ultrasparc
  CFLAGS += -mtune=$(PROC) -O3 -pipe -fomit-frame-pointer -mcpu=v8
endif

#
# unixODBC stuff...
#
ifneq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/include/odbcinst.h)$(wildcard $(CROSS_COMPILE_TARGET)/usr/local/include/odbcinst.h),)
  MODS+=cdr_odbc.so
endif

#
# FreeTDS stuff...
#
ifneq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/include/tds.h)$(wildcard $(CROSS_COMPILE_TARGET)/usr/local/include/tds.h)$(wildcard $(CROSS_COMPILE_TARGET)/usr/include/freetds/tds.h),)
  ifeq ($(shell grep -s TDS_VERSION_NO $(CROSS_COMPILE_TARGET)/usr/include/tdsver.h $(CROSS_COMPILE_TARGET)/usr/local/include/tdsver.h $(CROSS_COMPILE_TARGET)/usr/include/freetds/tdsver.h | grep -c 0.63),1)
    CFLAGS += -DFREETDS_0_63
  else
	ifeq ($(shell grep -s TDS_VERSION_NO $(CROSS_COMPILE_TARGET)/usr/include/tdsver.h $(CROSS_COMPILE_TARGET)/usr/local/include/tdsver.h $(CROSS_COMPILE_TARGET)/usr/include/freetds/tdsver.h | grep -c 0.62),1)
      CFLAGS += -DFREETDS_0_62
    else
      CFLAGS += -DFREETDS_PRE_0_62
    endif
  endif
  MODS+=cdr_tds.so
endif

#
# PGSQL stuff...  Autoconf anyone??
#
ifneq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/include/postgresql)$(wildcard $(CROSS_COMPILE_TARGET)/usr/local/pgsql/include)$(wildcard $(CROSS_COMPILE_TARGET)/usr/include/pgsql)$(wildcard $(CROSS_COMPILE_TARGET)/usr/local/include/pgsql)$(wildcard $(CROSS_COMPILE_TARGET)/opt/pgsql/include)$(wildcard $(CROSS_COMPILE_TARGET)/usr/include/libpq-fe.h),)
  MODS+=cdr_pgsql.so
endif

MLFLAGS=

ifneq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/include/postgresql),)
  CFLAGS+=-I$(CROSS_COMPILE_TARGET)/usr/include/postgresql
  MLFLAGS+=-L$(CROSS_COMPILE_TARGET)/usr/lib/postgresql
endif

ifneq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/local/pgsql/include),)
  CFLAGS+=-I$(CROSS_COMPILE_TARGET)/usr/local/pgsql/include
  MLFLAGS+=-L$(CROSS_COMPILE_TARGET)/usr/local/pgsql/lib
endif

ifneq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/include/pgsql),)
  CFLAGS+=-I$(CROSS_COMPILE_TARGET)/usr/include/pgsql
  MLFLAGS+=-L$(CROSS_COMPILE_TARGET)/usr/lib/pgsql
endif

ifneq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/local/include/pgsql),)
  CFLAGS+=-I$(CROSS_COMPILE_TARGET)/usr/local/include/pgsql
  MLFLAGS+=-L$(CROSS_COMPILE_TARGET)/usr/local/lib/pgsql
endif

ifneq ($(wildcard $(CROSS_COMPILE_TARGET)/opt/pgsql/include),)
  CFLAGS+=-I$(CROSS_COMPILE_TARGET)/opt/pgsql/include
  MLFLAGS+=-L$(CROSS_COMPILE_TARGET) /opt/pgsql/lib
endif

ifneq ($(wilcard $(CROSS_COMPILE_TARGET)/usr/include/postgresql),)
  CFLAGS+=-I$(CROSS_COMPILE_TARGET)/usr/include/postgresql
endif

ifneq ($(wilcard $(CROSS_COMPILE_TARGET)/usr/lib/libpq.so),)
  MLFLAGS+=-L$(CROSS_COMPILE_TARGET)/usr/lib
endif

#
# SQLite stuff...
#
ifneq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/include/sqlite.h),)
  MODS+=cdr_sqlite.so
endif

all: depend $(MODS)

install: all
	for x in $(MODS); do $(INSTALL) -m 755 $$x $(DESTDIR)$(MODULES_DIR) ; done

clean:
	rm -f *.so *.o .depend

%.so : %.o
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB}

ifneq ($(wildcard .depend),)
  include .depend
endif

cdr_odbc.so: cdr_odbc.o
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB} -lodbc $(MLFLAGS)

cdr_tds.so: cdr_tds.o
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB} -ltds $(MLFLAGS)

cdr_pgsql.so: cdr_pgsql.o
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB} -lpq -lz $(MLFLAGS)

cdr_sqlite.so: cdr_sqlite.o
	$(CC) $(SOLINK) -o $@ ${CYGSOLINK} $< ${CYGSOLIB} -lsqlite $(MLFLAGS)

depend: .depend

.depend:
	../build_tools/mkdep $(CFLAGS) `ls *.c`
