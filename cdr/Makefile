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

MODS:=$(patsubst %.c,%.so,$(wildcard cdr_*.c))

ifeq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/include/odbcinst.h $(CROSS_COMPILE_TARGET)/usr/local/include/odbcinst.h),)
  MODS:=$(filter-out cdr_odbc.so,$(MODS))
endif

ifeq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/include/postgresql $(CROSS_COMPILE_TARGET)/usr/local/pgsql/include $(CROSS_COMPILE_TARGET)/usr/include/pgsql $(CROSS_COMPILE_TARGET)/usr/local/include/pgsql $(CROSS_COMPILE_TARGET)/opt/pgsql/include $(CROSS_COMPILE_TARGET)/usr/include/libpq-fe.h),)
  MODS:=$(filter-out cdr_pgsql.so,$(MODS))
endif

ifeq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/include/sqlite.h),)
  MODS:=$(filter-out cdr_sqlite.so,$(MODS))
endif

ifeq ($(wildcard $(CROSS_COMPILE_TARGET)/usr/include/tds.h $(CROSS_COMPILE_TARGET)/usr/local/include/tds.h $(CROSS_COMPILE_TARGET)/usr/include/freetds/tds.h),)
  MODS:=$(filter-out cdr_tds.so,$(MODS))
  NOTDS=1
endif

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

ifeq (${NOTDS},)
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

all: depend $(MODS)

install: all
	for x in $(MODS); do $(INSTALL) -m 755 $$x $(DESTDIR)$(MODULES_DIR) ; done

uninstall:

clean-depend:
	rm -f .depend

clean: clean-depend
	rm -f *.so *.o

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
